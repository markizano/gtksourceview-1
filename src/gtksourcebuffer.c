/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- 
 *  gtksourcebuffer.c
 *
 *  Copyright (C) 1999,2000,2001,2002 by:
 *          Mikael Hermansson <tyan@linux.se>
 *          Chris Phelps <chicane@reninet.com>
 *          Jeroen Zwartepoorte <jeroen@xs4all.nl>
 *          
 *  Copyright (C) 2003 - Paolo Maggi <paolo.maggi@polito.it>    
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <gtk/gtk.h>

#include "gtksourceview-i18n.h"
#include "gtksourcebuffer.h"

#include "gtksourceundomanager.h"
#include "gtksourceview-marshal.h"
#include "gtktextregion.h"

/*
#define ENABLE_DEBUG
#define ENABLE_PROFILE
*/
#undef ENABLE_DEBUG
#undef ENABLE_PROFILE

#ifdef ENABLE_DEBUG
#define DEBUG(x) (x)
#else
#define DEBUG(x)
#endif

#ifdef ENABLE_PROFILE
#define PROFILE(x) (x)
#else
#define PROFILE(x)
#endif

/* define this to always highlight in an idle handler, and not
 * possibly in the expose method of the view */
#undef LAZIEST_MODE

/* in milliseconds */
#define WORKER_TIME_SLICE                   30
#define INITIAL_WORKER_BATCH                40960
#define MINIMUM_WORKER_BATCH                1024

#define MAX_CHARS_BEFORE_FINDING_A_MATCH    2000

typedef struct _MarkerSubList        MarkerSubList;
typedef struct _SyntaxDelimiter      SyntaxDelimiter;
typedef struct _PatternMatch         PatternMatch;

/* Signals */
enum {
	CAN_UNDO = 0,
	CAN_REDO,
	HIGHLIGHT_UPDATED,
	LAST_SIGNAL
};

struct _MarkerSubList 
{
	gint                line;
	GList              *marker_list;
};

struct _SyntaxDelimiter 
{
	gint                offset;
	gint                depth;
	GtkSyntaxTag       *tag;
};

struct _PatternMatch
{
	GtkPatternTag        *tag;
	GtkSourceBufferMatch  match;
};

struct _GtkSourceBufferPrivate 
{
	gint                   highlight:1;
	gint                   check_brackets:1;

	GtkTextTag            *bracket_match_tag;
	GtkTextMark           *bracket_mark;

	GHashTable            *line_markers;

	GList                 *syntax_items;
	GList                 *pattern_items;
	GtkSourceRegex         reg_syntax_all;

	/* Region covering the unhighlighted text */
	GtkTextRegion         *refresh_region;

	/* Syntax regions data */
	GArray                *syntax_regions;
	GArray                *old_syntax_regions;
	gint                   worker_last_offset;
	gint                   worker_batch_size;
	guint                  worker_handler;

	/* views highlight requests */
	GtkTextRegion         *highlight_requests;

	GtkSourceUndoManager  *undo_manager;
};



static GtkTextBufferClass *parent_class = NULL;
static guint 	 buffer_signals[LAST_SIGNAL] = { 0 };

static void 	 gtk_source_buffer_class_init		(GtkSourceBufferClass    *klass);
static void 	 gtk_source_buffer_init			(GtkSourceBuffer         *klass);
static GObject  *gtk_source_buffer_constructor          (GType                    type,
							 guint                    n_construct_properties,
							 GObjectConstructParam   *construct_param);
static void 	 gtk_source_buffer_finalize		(GObject                 *object);

static void 	 gtk_source_buffer_can_undo_handler 	(GtkSourceUndoManager    *um,
							 gboolean                 can_undo,
							 GtkSourceBuffer         *buffer);
static void 	 gtk_source_buffer_can_redo_handler	(GtkSourceUndoManager    *um,
							 gboolean                 can_redo,
							 GtkSourceBuffer         *buffer);

static void 	 gtk_source_buffer_move_cursor		(GtkTextBuffer           *buffer,
							 GtkTextIter             *iter,
							 GtkTextMark             *mark, 
							 gpointer                 data);

static void 	 gtk_source_buffer_real_insert_text 	(GtkTextBuffer           *buffer,
							 GtkTextIter             *iter,
							 const gchar             *text,
							 gint                     len);
static void 	 gtk_source_buffer_real_delete_range 	(GtkTextBuffer           *buffer,
							 GtkTextIter             *iter,
							 GtkTextIter             *end);

static const GtkSyntaxTag *iter_has_syntax_tag 		(GtkTextIter             *iter);

static gboolean	 iter_backward_to_tag_start 		(GtkTextIter             *iter,
							 GtkTextTag              *tag);
static gboolean	 iter_forward_to_tag_end 		(GtkTextIter             *iter,
							 GtkTextTag              *tag);

static void	 hash_remove_func 			(gpointer                 key, 
							 gpointer                 value,
							 gpointer                 user_data);
static void 	 get_tags_func 				(GtkTextTag              *tag, 
		                                         gpointer                 data);

static void	 highlight_region 			(GtkSourceBuffer         *source_buffer,
		   					 GtkTextIter             *start, 
							 GtkTextIter             *end);

static GList 	*gtk_source_buffer_get_syntax_entries 	(const GtkSourceBuffer   *buffer);
static GList 	*gtk_source_buffer_get_pattern_entries 	(const GtkSourceBuffer   *buffer);

static void	 sync_syntax_regex 			(GtkSourceBuffer         *buffer);

static void      build_syntax_regions_table             (GtkSourceBuffer         *buffer,
							 GtkTextIter             *start_at);
static void      update_syntax_regions                  (GtkSourceBuffer         *source_buffer,
							 gint                     start,
							 gint                     delta);

static void      invalidate_syntax_regions              (GtkSourceBuffer         *source_buffer,
							 GtkTextIter             *from,
							 gint                     delta);
static void      refresh_range                          (GtkSourceBuffer         *buffer,
							 GtkTextIter             *start,
							 GtkTextIter             *end);
static void      ensure_highlighted                     (GtkSourceBuffer         *source_buffer,
							 GtkTextIter             *start,
							 GtkTextIter             *end);

static gboolean	 gtk_source_buffer_find_bracket_match_real (GtkTextIter          *orig, 
							    gint                  max_chars);

static void	 gtk_source_buffer_remove_all_source_tags (GtkSourceBuffer   *buffer,
					  		const GtkTextIter *start,
					  		const GtkTextIter *end);

static void	sync_with_tag_table 			(GtkSourceBuffer *buffer);

GType
gtk_source_buffer_get_type (void)
{
	static GType our_type = 0;

	if (!our_type) {
		static const GTypeInfo our_info = {
			sizeof (GtkSourceBufferClass),
			NULL,	/* base_init */
			NULL,	/* base_finalize */
			(GClassInitFunc) gtk_source_buffer_class_init,
			NULL,	/* class_finalize */
			NULL,	/* class_data */
			sizeof (GtkSourceBuffer),
			0,	/* n_preallocs */
			(GInstanceInitFunc) gtk_source_buffer_init
		};

		our_type = g_type_register_static (GTK_TYPE_TEXT_BUFFER,
						   "GtkSourceBuffer",
						   &our_info, 
						   0);
	}
	
	return our_type;
}
	
static void
gtk_source_buffer_class_init (GtkSourceBufferClass *klass)
{
	GObjectClass        *object_class;
	GtkTextBufferClass  *tb_class;

	object_class 	= G_OBJECT_CLASS (klass);
	parent_class 	= g_type_class_peek_parent (klass);
	tb_class	= GTK_TEXT_BUFFER_CLASS (klass);
		
	object_class->constructor = gtk_source_buffer_constructor;
	object_class->finalize	  = gtk_source_buffer_finalize;

	klass->can_undo 	 = NULL;
	klass->can_redo 	 = NULL;
	klass->highlight_updated = NULL;

	/* Do not set these signals handlers directly on the parent_class since
	 * that will cause problems (a loop). */
	tb_class->insert_text 	= gtk_source_buffer_real_insert_text;
	tb_class->delete_range 	= gtk_source_buffer_real_delete_range;

	buffer_signals[CAN_UNDO] =
	    g_signal_new ("can_undo",
			  G_OBJECT_CLASS_TYPE (object_class),
			  G_SIGNAL_RUN_LAST,
			  G_STRUCT_OFFSET (GtkSourceBufferClass, can_undo),
			  NULL, NULL,
			  gtksourceview_marshal_VOID__BOOLEAN,
			  G_TYPE_NONE, 
			  1, 
			  G_TYPE_BOOLEAN);

	buffer_signals[CAN_REDO] =
	    g_signal_new ("can_redo",
			  G_OBJECT_CLASS_TYPE (object_class),
			  G_SIGNAL_RUN_LAST,
			  G_STRUCT_OFFSET (GtkSourceBufferClass, can_redo),
			  NULL, NULL,
			  gtksourceview_marshal_VOID__BOOLEAN,
			  G_TYPE_NONE, 
			  1, 
			  G_TYPE_BOOLEAN);

	buffer_signals[HIGHLIGHT_UPDATED] =
	    g_signal_new ("highlight_updated",
			  G_OBJECT_CLASS_TYPE (object_class),
			  G_SIGNAL_RUN_LAST,
			  G_STRUCT_OFFSET (GtkSourceBufferClass, highlight_updated),
			  NULL, NULL,
			  gtksourceview_marshal_VOID__BOXED_BOXED,
			  G_TYPE_NONE, 
			  2, 
			  GTK_TYPE_TEXT_ITER | G_SIGNAL_TYPE_STATIC_SCOPE,
			  GTK_TYPE_TEXT_ITER | G_SIGNAL_TYPE_STATIC_SCOPE);
}

static void
gtk_source_buffer_init (GtkSourceBuffer *buffer)
{
	GtkSourceBufferPrivate *priv;

	priv = g_new0 (GtkSourceBufferPrivate, 1);

	buffer->priv = priv;

	priv->undo_manager = gtk_source_undo_manager_new (GTK_TEXT_BUFFER (buffer));

	priv->check_brackets = TRUE;
	priv->bracket_mark = NULL;
	
	priv->line_markers = g_hash_table_new (NULL, NULL);

	/* highlight data */
	priv->highlight = TRUE;
	priv->refresh_region =  gtk_text_region_new (GTK_TEXT_BUFFER (buffer));
	priv->syntax_regions =  g_array_new (FALSE, FALSE,
					     sizeof (SyntaxDelimiter));
	priv->highlight_requests = gtk_text_region_new (GTK_TEXT_BUFFER (buffer));
	priv->worker_handler = 0;

	/* initially the buffer is empty so it's entirely analyzed */
	priv->worker_last_offset = -1;
	priv->worker_batch_size = INITIAL_WORKER_BATCH;
	
	g_signal_connect (G_OBJECT (buffer),
			  "mark_set",
			  G_CALLBACK (gtk_source_buffer_move_cursor),
			  NULL);

	g_signal_connect (G_OBJECT (priv->undo_manager),
			  "can_undo",
			  G_CALLBACK (gtk_source_buffer_can_undo_handler),
			  buffer);

	g_signal_connect (G_OBJECT (priv->undo_manager),
			  "can_redo",
			  G_CALLBACK (gtk_source_buffer_can_redo_handler),
			  buffer);

}

static void 
tag_added_or_removed_cb (GtkTextTagTable *table, GtkTextTag *tag, GtkSourceBuffer *buffer)
{
	sync_with_tag_table (buffer);
	
}


static void 
tag_table_changed_cb (GtkSourceTagTable *table, GtkSourceBuffer *buffer)
{
	sync_with_tag_table (buffer);
}

static void
create_empty_tag_table (GtkSourceBuffer *buffer)
{
	GtkSourceTagTable *tag_table;
	
	g_return_if_fail (GTK_TEXT_BUFFER (buffer)->tag_table == NULL);

	tag_table = gtk_source_tag_table_new ();
	g_object_set (G_OBJECT (buffer), "tag-table", tag_table, NULL);
	g_object_unref (tag_table);
}

static GObject *
gtk_source_buffer_constructor (GType                  type,
			       guint                  n_construct_properties,
			       GObjectConstructParam *construct_param)
{
	GObject *g_object;
	
	g_object = G_OBJECT_CLASS (parent_class)->constructor (type, 
							       n_construct_properties,
							       construct_param);
	
	if (g_object) {
		GtkSourceBuffer *source_buffer = GTK_SOURCE_BUFFER (g_object);
		
		if (GTK_TEXT_BUFFER (source_buffer)->tag_table == NULL)
			create_empty_tag_table (source_buffer);

		/* we can't create the tag in gtk_source_buffer_init
		 * since we haven't set a tag table yet, and creating
		 * the tag forces the creation of an empty table */
		source_buffer->priv->bracket_match_tag = 
			gtk_text_buffer_create_tag (GTK_TEXT_BUFFER (source_buffer),
						    NULL,
						    "foreground", "white",
						    "background", "red",
						    "weight", PANGO_WEIGHT_BOLD,
						    NULL);

		if (GTK_IS_SOURCE_TAG_TABLE (GTK_TEXT_BUFFER (source_buffer)->tag_table))
		{
			g_signal_connect (GTK_TEXT_BUFFER (source_buffer)->tag_table ,
					  "changed",
					  G_CALLBACK (tag_table_changed_cb),
					  source_buffer);
		}
		else
		{
			g_assert (GTK_IS_TEXT_TAG_TABLE (GTK_TEXT_BUFFER (source_buffer)->tag_table));

			g_warning ("Please use GtkSourceTagTable with GtkSourceBuffer.");

			g_signal_connect (GTK_TEXT_BUFFER (source_buffer)->tag_table,
					  "tag_added",
					  G_CALLBACK (tag_added_or_removed_cb),
					  source_buffer);

			g_signal_connect (GTK_TEXT_BUFFER (source_buffer)->tag_table,
					  "tag_removed",
					  G_CALLBACK (tag_added_or_removed_cb),
					  source_buffer);				
		}
	}
	
	return g_object;
}

static void
hash_remove_func (gpointer key, gpointer value, gpointer user_data)
{
	GList *iter = value;

	while (iter != NULL) {
		g_free (iter->data);
		iter = g_list_next (iter);
	}
	
	g_list_free (iter);
}

static void
gtk_source_buffer_finalize (GObject *object)
{
	GtkSourceBuffer *buffer;

	g_return_if_fail (object != NULL);
	g_return_if_fail (GTK_IS_SOURCE_BUFFER (object));

	buffer = GTK_SOURCE_BUFFER (object);
	g_return_if_fail (buffer->priv != NULL);
	
	if (buffer->priv->line_markers) {
		g_hash_table_foreach_remove (buffer->priv->line_markers,
					     (GHRFunc)hash_remove_func,
					     NULL);
		g_hash_table_destroy (buffer->priv->line_markers);
	}

	if (buffer->priv->worker_handler) {
		g_source_remove (buffer->priv->worker_handler);
	}

	/* we can't delete marks if we're finalizing the buffer */
	gtk_text_region_destroy (buffer->priv->refresh_region, FALSE);
	gtk_text_region_destroy (buffer->priv->highlight_requests, FALSE);

	g_object_unref (buffer->priv->undo_manager);

	g_array_free (buffer->priv->syntax_regions, TRUE);
	if (buffer->priv->old_syntax_regions)
		g_array_free (buffer->priv->old_syntax_regions, TRUE);
	
	if (buffer->priv->reg_syntax_all.len > 0)
		gtk_source_regex_destroy (&buffer->priv->reg_syntax_all);
	
	g_list_free (buffer->priv->syntax_items);
	g_list_free (buffer->priv->pattern_items);

	g_free (buffer->priv);
	buffer->priv = NULL;
	
	/* TODO: free syntax_items, patterns, etc. - Paolo */
	
	G_OBJECT_CLASS (parent_class)->finalize (object);
}

GtkSourceBuffer *
gtk_source_buffer_new (GtkSourceTagTable *table)
{
	GtkSourceBuffer *buffer;

	buffer = GTK_SOURCE_BUFFER (g_object_new (GTK_TYPE_SOURCE_BUFFER, 
						  "tag-table", table, 
						  NULL));
	
	return buffer;
}


static void
gtk_source_buffer_can_undo_handler (GtkSourceUndoManager  	*um,
				    gboolean			 can_undo,
				    GtkSourceBuffer 		*buffer)
{
	g_return_if_fail (GTK_IS_SOURCE_BUFFER (buffer));

	g_signal_emit (G_OBJECT (buffer),
		       buffer_signals[CAN_UNDO], 
		       0, 
		       can_undo);
}

static void
gtk_source_buffer_can_redo_handler (GtkSourceUndoManager  	*um,
				    gboolean         		 can_redo,
				    GtkSourceBuffer 		*buffer)
{
	g_return_if_fail (GTK_IS_SOURCE_BUFFER (buffer));

	g_signal_emit (G_OBJECT (buffer),
		       buffer_signals[CAN_REDO], 
		       0, 
		       can_redo);
}

static gboolean
iter_backward_to_tag_start (GtkTextIter *iter, GtkTextTag *tag)
{
	gboolean ret;

	g_return_val_if_fail (iter != NULL, FALSE);

	if (gtk_text_iter_begins_tag (iter, tag))
		return TRUE;

	ret = gtk_text_iter_backward_to_tag_toggle (iter, tag);

	if (ret && !gtk_text_iter_begins_tag (iter, tag))
		return iter_backward_to_tag_start (iter, tag);
	else
		return ret;
}

static gboolean
iter_forward_to_tag_end (GtkTextIter *iter, GtkTextTag *tag)
{
	gboolean ret;

	g_return_val_if_fail (iter != NULL, FALSE);

	if (gtk_text_iter_ends_tag (iter, tag))
		return TRUE;

	ret = gtk_text_iter_forward_to_tag_toggle (iter, tag);

	if (ret && !gtk_text_iter_ends_tag (iter, tag))
		return iter_forward_to_tag_end (iter, tag);
	else
		return ret;
}

static void
get_tags_func (GtkTextTag *tag, gpointer data)
{
	g_return_if_fail (data != NULL);

	GSList **list = (GSList **) data;

	if (GTK_IS_SOURCE_TAG (tag))
	{
		*list = g_slist_prepend (*list, tag);
	}
}

static void
gtk_source_buffer_move_cursor (GtkTextBuffer *buffer,
			       GtkTextIter   *iter, 
			       GtkTextMark   *mark, 
			       gpointer       data)
{
	GtkTextIter iter1;

	g_return_if_fail (GTK_IS_SOURCE_BUFFER (buffer));
	g_return_if_fail (iter != NULL);
	g_return_if_fail (mark != NULL);
	g_return_if_fail (gtk_text_iter_get_buffer (iter) == buffer);

	if (mark != gtk_text_buffer_get_insert (buffer))
		return;

	if (GTK_SOURCE_BUFFER (buffer)->priv->bracket_mark) {
		GtkTextIter iter2;

		gtk_text_buffer_get_iter_at_mark (buffer,
						  &iter1,
						  GTK_SOURCE_BUFFER (buffer)->priv->bracket_mark);
		iter2 = iter1;
		gtk_text_iter_forward_char (&iter2);
		gtk_text_buffer_remove_tag (buffer,
					    GTK_SOURCE_BUFFER (buffer)->priv->bracket_match_tag,
					    &iter1, 
					    &iter2);
	}

	if (!GTK_SOURCE_BUFFER (buffer)->priv->check_brackets || iter_has_syntax_tag (iter))
		return;

	if (gtk_source_buffer_find_bracket_match_real (iter, MAX_CHARS_BEFORE_FINDING_A_MATCH)) {
		if (!GTK_SOURCE_BUFFER (buffer)->priv->bracket_mark)
			GTK_SOURCE_BUFFER (buffer)->priv->bracket_mark =
				gtk_text_buffer_create_mark (buffer, 
							     NULL,
							     iter, 
							     FALSE);
		else
			gtk_text_buffer_move_mark (buffer,
						   GTK_SOURCE_BUFFER (buffer)->priv->bracket_mark,
						   iter);

		iter1 = *iter;
		gtk_text_iter_forward_char (&iter1);
		gtk_text_buffer_apply_tag (buffer,
					   GTK_SOURCE_BUFFER (buffer)->priv->bracket_match_tag,
					   iter, 
					   &iter1);
	}
}

static void
gtk_source_buffer_real_insert_text (GtkTextBuffer *buffer,
				    GtkTextIter   *iter,
				    const gchar   *text, 
				    gint           len)
{
	gint start_offset;

	g_return_if_fail (GTK_IS_SOURCE_BUFFER (buffer));
	g_return_if_fail (iter != NULL);
	g_return_if_fail (text != NULL);
	g_return_if_fail (gtk_text_iter_get_buffer (iter) == buffer);

	start_offset = gtk_text_iter_get_offset (iter);

	/*
	 * iter is invalidated when
	 * insertion occurs (because the buffer contents change), but the
	 * default signal handler revalidates it to point to the end of the
	 * inserted text 
	 */
	parent_class->insert_text (buffer, iter, text, len);

	if (!GTK_SOURCE_BUFFER (buffer)->priv->highlight)
		return;

	update_syntax_regions (GTK_SOURCE_BUFFER (buffer), 
			       start_offset,
			       g_utf8_strlen (text, len));
}

static void
gtk_source_buffer_real_delete_range (GtkTextBuffer *buffer,
				     GtkTextIter   *start,
				     GtkTextIter   *end)
{
	gint delta;

	g_return_if_fail (GTK_IS_SOURCE_BUFFER (buffer));
	g_return_if_fail (start != NULL);
	g_return_if_fail (end != NULL);
	g_return_if_fail (gtk_text_iter_get_buffer (start) == buffer);
	g_return_if_fail (gtk_text_iter_get_buffer (end) == buffer);

	if (!GTK_SOURCE_BUFFER (buffer)->priv->highlight) {
		parent_class->delete_range (buffer, start, end);
		return;
	}

	gtk_text_iter_order (start, end);
	delta = gtk_text_iter_get_offset (start) - 
			gtk_text_iter_get_offset (end);
	
	parent_class->delete_range (buffer, start, end);

	update_syntax_regions (GTK_SOURCE_BUFFER (buffer),
			       gtk_text_iter_get_offset (start),
			       delta);
}

static void
add_marker (gpointer data, gpointer user_data)
{
	char *name = data;
	MarkerSubList *sublist = user_data;
	GtkSourceBufferMarker *marker;

	marker = g_new0 (GtkSourceBufferMarker, 1);
	marker->line = sublist->line;
	marker->name = g_strdup (name);

	sublist->marker_list =
	    g_list_append (sublist->marker_list, marker);
}

static void
add_markers (gpointer key, gpointer value, gpointer user_data)
{
	GList **list = user_data;
	GList *markers = value;
	MarkerSubList *sublist;

	sublist = g_new0 (MarkerSubList, 1);
	sublist->line = GPOINTER_TO_INT (key);
	sublist->marker_list = *list;

	g_list_foreach (markers, add_marker, sublist);

	g_free (sublist);
}

static GSList *
gtk_source_buffer_get_source_tags (const GtkSourceBuffer *buffer)
{
	GSList *list = NULL;
	GtkTextTagTable *table;

	g_return_val_if_fail (GTK_IS_SOURCE_BUFFER (buffer), NULL);

	table = gtk_text_buffer_get_tag_table (GTK_TEXT_BUFFER (buffer));
	gtk_text_tag_table_foreach (table, get_tags_func, &list);
	list = g_slist_reverse (list);	
	
	return list;
}



static void
sync_with_tag_table (GtkSourceBuffer *buffer)
{
	GtkTextTagTable *tag_table;
	GSList *entries;
	GSList *list;

	g_return_if_fail (GTK_IS_SOURCE_BUFFER (buffer));

	if (buffer->priv->syntax_items) {
		g_list_free (buffer->priv->syntax_items);
		buffer->priv->syntax_items = NULL;
	}

	if (buffer->priv->pattern_items) {
		g_list_free (buffer->priv->pattern_items);
		buffer->priv->pattern_items = NULL;
	}

	tag_table = gtk_text_buffer_get_tag_table (GTK_TEXT_BUFFER (buffer));
	g_return_if_fail (tag_table != NULL);

	list = entries = gtk_source_buffer_get_source_tags (buffer);
	
	while (entries != NULL) 
	{	
		if (GTK_IS_SYNTAX_TAG (entries->data)) 
		{
			buffer->priv->syntax_items =
			    g_list_prepend (buffer->priv->syntax_items, entries->data);
			
		} 
		else if (GTK_IS_PATTERN_TAG (entries->data)) 
		{
			buffer->priv->pattern_items =
			    g_list_prepend (buffer->priv->pattern_items, entries->data);
			
		}

		entries = g_slist_next (entries);
	}

	g_slist_free (list);

	buffer->priv->syntax_items = g_list_reverse (buffer->priv->syntax_items);
	buffer->priv->pattern_items = g_list_reverse (buffer->priv->pattern_items);
	
	if (buffer->priv->syntax_items != NULL)
	{
		sync_syntax_regex (buffer);
	}
	else
	{
		if (buffer->priv->reg_syntax_all.len > 0)
			gtk_source_regex_destroy (&buffer->priv->reg_syntax_all);
	}


	if (buffer->priv->highlight)
		invalidate_syntax_regions (buffer, NULL, 0);
}

static void
sync_syntax_regex (GtkSourceBuffer *buffer)
{
	GString *str;
	GList *cur;
	GtkSyntaxTag *tag;

	str = g_string_new ("");
	cur = buffer->priv->syntax_items;

	while (cur != NULL) {
		g_return_if_fail (GTK_IS_SYNTAX_TAG (cur->data));

		tag = GTK_SYNTAX_TAG (cur->data);
		g_string_append (str, tag->start);
		
		cur = g_list_next (cur);
		
		if (cur != NULL)
			g_string_append (str, "\\|");
	}

	if (buffer->priv->reg_syntax_all.len > 0)
		gtk_source_regex_destroy (&buffer->priv->reg_syntax_all);
	
	gtk_source_regex_compile (&buffer->priv->reg_syntax_all, str->str);

	g_string_free (str, TRUE);
}

static const GtkSyntaxTag *
iter_has_syntax_tag (GtkTextIter *iter)
{
	const GtkSyntaxTag *tag;
	GSList *list;

	g_return_val_if_fail (iter != NULL, NULL);

	list = gtk_text_iter_get_tags (iter);
	tag = NULL;

	while ((list != NULL) && (tag == NULL)) {
		if (GTK_IS_SYNTAX_TAG (list->data))
			tag = GTK_SYNTAX_TAG (list->data);
		list = g_slist_next (list);
	}

	g_slist_free (list);

	return tag;
}


static GList *
gtk_source_buffer_get_syntax_entries (const GtkSourceBuffer *buffer)
{
	g_return_val_if_fail (GTK_IS_SOURCE_BUFFER (buffer), NULL);

	return buffer->priv->syntax_items;
}

static GList *
gtk_source_buffer_get_pattern_entries (const GtkSourceBuffer *buffer)
{
	g_return_val_if_fail (GTK_IS_SOURCE_BUFFER (buffer), NULL);

	return buffer->priv->pattern_items;
}

static gboolean
gtk_source_buffer_find_bracket_match_real (GtkTextIter *orig, gint max_chars)
{
	GtkTextIter iter;
	
	gunichar base_char;
	gunichar search_char;
	gunichar cur_char;
	gint addition;
	gint char_cont;

	gint counter;
	
	gboolean found;

	iter = *orig;

	gtk_text_iter_backward_char (&iter);
	cur_char = gtk_text_iter_get_char (&iter);

	base_char = search_char = cur_char;
	
	switch ((int) base_char) {
		case '{':
			addition = 1;
			search_char = '}';
			break;
		case '(':
			addition = 1;
			search_char = ')';
			break;
		case '[':
			addition = 1;
			search_char = ']';
			break;
		case '<':
			addition = 1;
			search_char = '>';
			break;
		case '}':
			addition = -1;
			search_char = '{';
			break;
		case ')':
			addition = -1;
			search_char = '(';
			break;
		case ']':
			addition = -1;
			search_char = '[';
			break;
		case '>':
			addition = -1;
			search_char = '<';
			break;
		default:
			addition = 0;
			break;
	}

	if (addition == 0)
		return FALSE;

	counter = 0;
	found = FALSE;
	char_cont = 0;
	
	do {
		gtk_text_iter_forward_chars (&iter, addition);
		cur_char = gtk_text_iter_get_char (&iter);
		++char_cont;
		
		if ((cur_char == search_char) && counter == 0) {
			found = TRUE;
			break;
		}
		if (cur_char == base_char)
			counter++;
		else 
			if (cur_char == search_char)
				counter--;
	} 
	while (!gtk_text_iter_is_end (&iter) && !gtk_text_iter_is_start (&iter) && 
		((char_cont < max_chars) || (max_chars < 0)));

	if (found)
		*orig = iter;

	return found;
}

gboolean
gtk_source_buffer_find_bracket_match (GtkTextIter *orig)
{
	g_return_val_if_fail (orig != NULL, FALSE);

	return gtk_source_buffer_find_bracket_match_real (orig, -1);
}
	
gboolean
gtk_source_buffer_can_undo (const GtkSourceBuffer *buffer)
{
	g_return_val_if_fail (GTK_IS_SOURCE_BUFFER (buffer), FALSE);

	return gtk_source_undo_manager_can_undo (buffer->priv->undo_manager);
}

gboolean
gtk_source_buffer_can_redo (const GtkSourceBuffer *buffer)
{
	g_return_val_if_fail (GTK_IS_SOURCE_BUFFER (buffer), FALSE);

	return gtk_source_undo_manager_can_redo (buffer->priv->undo_manager);
}

void
gtk_source_buffer_undo (GtkSourceBuffer *buffer)
{
	g_return_if_fail (GTK_IS_SOURCE_BUFFER (buffer));
	g_return_if_fail (gtk_source_undo_manager_can_undo (buffer->priv->undo_manager));

	gtk_source_undo_manager_undo (buffer->priv->undo_manager);
}

void
gtk_source_buffer_redo (GtkSourceBuffer *buffer)
{
	g_return_if_fail (GTK_IS_SOURCE_BUFFER (buffer));
	g_return_if_fail (gtk_source_undo_manager_can_redo (buffer->priv->undo_manager));

	gtk_source_undo_manager_redo (buffer->priv->undo_manager);
}

gint
gtk_source_buffer_get_max_undo_levels (const GtkSourceBuffer *buffer)
{
	g_return_val_if_fail (GTK_IS_SOURCE_BUFFER (buffer), 0);

	return gtk_source_undo_manager_get_max_undo_levels (buffer->priv->undo_manager);
}

void
gtk_source_buffer_set_max_undo_levels (GtkSourceBuffer *buffer,
				       gint             max_undo_levels)
{
	g_return_if_fail (GTK_IS_SOURCE_BUFFER (buffer));

	gtk_source_undo_manager_set_max_undo_levels (buffer->priv->undo_manager,
					      max_undo_levels);
}

void
gtk_source_buffer_begin_not_undoable_action (GtkSourceBuffer *buffer)
{
	g_return_if_fail (GTK_IS_SOURCE_BUFFER (buffer));

	gtk_source_undo_manager_begin_not_undoable_action (buffer->priv->undo_manager);
}

void
gtk_source_buffer_end_not_undoable_action (GtkSourceBuffer *buffer)
{
	g_return_if_fail (GTK_IS_SOURCE_BUFFER (buffer));

	gtk_source_undo_manager_end_not_undoable_action (buffer->priv->undo_manager);
}

/* Add a marker to a line.
 * If the list doesnt already exist, it will call set_marker. If the list does
 * exist, the new marker will be appended. If the marker already exists, it will
 * be removed from its current order and then prepended.
 */
void
gtk_source_buffer_line_add_marker (GtkSourceBuffer *buffer,
				   gint             line, 
				   const gchar     *marker)
{
	GList *list = NULL;
	GList *iter;
	gint line_count;

	g_return_if_fail (GTK_IS_SOURCE_BUFFER (buffer));
	g_return_if_fail (marker != NULL);
	
	line_count =
	    gtk_text_buffer_get_line_count (GTK_TEXT_BUFFER (buffer));
	
	line = CLAMP (line, 0, line_count - 1);

	list = (GList *) g_hash_table_lookup (buffer->priv->line_markers,
					      GINT_TO_POINTER (line));
	if (list) {
		iter = list;
		
		while (iter != NULL) {
			if ((iter->data != NULL) && 
			    (strcmp (marker, (gchar *) iter->data) == 0)) {
				list = g_list_remove (list, iter->data);
				g_free (iter->data);
				break;
			}

			iter = g_list_next (iter);
		}

		list = g_list_append (list, g_strdup (marker));
	} else 
		gtk_source_buffer_line_set_marker (buffer, line, marker);
}

void
gtk_source_buffer_line_set_marker (GtkSourceBuffer * buffer,
				   gint line, const gchar * marker)
{
	GList *new_list = NULL;
	gint line_count;

	g_return_if_fail (buffer != NULL);
	g_return_if_fail (GTK_IS_SOURCE_BUFFER (buffer));

	line_count =
	    gtk_text_buffer_get_line_count (GTK_TEXT_BUFFER (buffer));
	g_return_if_fail (line_count > line);

	gtk_source_buffer_line_remove_markers (buffer, line);
	if (marker) {
		new_list = g_list_append (new_list, g_strdup (marker));
		g_hash_table_insert (buffer->priv->line_markers,
				     GINT_TO_POINTER (line), new_list);
	}
}

gboolean
gtk_source_buffer_line_remove_marker (GtkSourceBuffer * buffer,
				      gint line, const gchar * marker)
{
	gboolean removed = FALSE;
	GList *list;
	GList *iter;
	gint line_count;

	g_return_val_if_fail (buffer != NULL, FALSE);
	g_return_val_if_fail (GTK_IS_SOURCE_BUFFER (buffer), FALSE);

	line_count =
	    gtk_text_buffer_get_line_count (GTK_TEXT_BUFFER (buffer));
	if (line > line_count)
		return FALSE;

	list = (GList *) g_hash_table_lookup (buffer->priv->line_markers,
					      GINT_TO_POINTER (line));
	for (iter = list; iter; iter = iter->next) {
		if (iter->data && !strcmp (marker, (gchar *) iter->data)) {
			list = g_list_remove (list, iter->data);

			g_hash_table_insert (buffer->priv->line_markers,
					     GINT_TO_POINTER (line), list);
			removed = TRUE;
			break;
		}
	}

	return removed;
}

const GList *
gtk_source_buffer_line_get_markers (const GtkSourceBuffer *buffer, 
				    gint                   line)
{
	g_return_val_if_fail (buffer != NULL, NULL);
	g_return_val_if_fail (GTK_IS_SOURCE_BUFFER (buffer), NULL);

	return (const GList *) g_hash_table_lookup (buffer->priv->
						    line_markers,
						    GINT_TO_POINTER
						    (line));
}

gint
gtk_source_buffer_line_has_markers (const GtkSourceBuffer *buffer, 
				    gint                   line)
{
	gpointer data;
	gint count = 0;

	g_return_val_if_fail (buffer != NULL, 0);
	g_return_val_if_fail (GTK_IS_SOURCE_BUFFER (buffer), 0);

	data = g_hash_table_lookup (buffer->priv->line_markers,
				    GINT_TO_POINTER (line));

	if (data)
		count = g_list_length ((GList *) data);

	return count;
}

gint
gtk_source_buffer_line_remove_markers (GtkSourceBuffer * buffer, gint line)
{
	GList *list = NULL;
	GList *iter = NULL;
	gint remove_count = 0;
	gint line_count = 0;

	g_return_val_if_fail (buffer != NULL, 0);
	g_return_val_if_fail (GTK_IS_SOURCE_BUFFER (buffer), 0);

	line_count =
	    gtk_text_buffer_get_line_count (GTK_TEXT_BUFFER (buffer));
	if (line > line_count)
		return 0;

	list = (GList *) g_hash_table_lookup (buffer->priv->line_markers,
					      GINT_TO_POINTER (line));
	if (list) {
		for (iter = list; iter; iter = iter->next) {
			if (iter->data)
				g_free (iter->data);
			remove_count++;
		}
		g_hash_table_remove (buffer->priv->line_markers,
				     GINT_TO_POINTER (line));
		g_list_free (list);
	}

	return remove_count;
}

GList *
gtk_source_buffer_get_all_markers (const GtkSourceBuffer *buffer)
{
	GList *list = NULL;

	g_hash_table_foreach (buffer->priv->line_markers, add_markers,
			      &list);

	return list;
}

gint
gtk_source_buffer_remove_all_markers (GtkSourceBuffer *buffer,
				      gint line_start, 
				      gint line_end)
{
	gint remove_count = 0;
	gint line_count;
	gint counter;

	g_return_val_if_fail (buffer != NULL, 0);
	g_return_val_if_fail (GTK_IS_SOURCE_BUFFER (buffer), 0);

	line_count =
	    gtk_text_buffer_get_line_count (GTK_TEXT_BUFFER (buffer));
	line_start = line_start < 0 ? 0 : line_start;
	line_end = line_end > line_count ? line_count : line_end;

	for (counter = line_start; counter <= line_end; counter++)
		remove_count +=
		    gtk_source_buffer_line_remove_markers (buffer,
							   counter);

	return remove_count;
}

gboolean
gtk_source_buffer_get_check_brackets (GtkSourceBuffer *buffer)
{
	g_return_val_if_fail (GTK_IS_SOURCE_BUFFER (buffer), FALSE);

	return buffer->priv->check_brackets;
}

void
gtk_source_buffer_set_check_brackets (GtkSourceBuffer *buffer,
				      gboolean         check_brackets)
{
	g_return_if_fail (GTK_IS_SOURCE_BUFFER (buffer));

	buffer->priv->check_brackets = check_brackets;
}

gboolean
gtk_source_buffer_get_highlight (const GtkSourceBuffer *buffer)
{
	g_return_val_if_fail (GTK_IS_SOURCE_BUFFER (buffer), FALSE);

	return buffer->priv->highlight;
}

void
gtk_source_buffer_set_highlight (GtkSourceBuffer *buffer,
				 gboolean         highlight)
{
	GtkTextIter iter1;
	GtkTextIter iter2;

	g_return_if_fail (GTK_IS_SOURCE_BUFFER (buffer));

	if (buffer->priv->highlight == highlight)
		return;

	buffer->priv->highlight = highlight;

	if (highlight) {
		invalidate_syntax_regions (buffer, NULL, 0);

	} else {
		if (buffer->priv->worker_handler) {
			g_source_remove (buffer->priv->worker_handler);
			buffer->priv->worker_handler = 0;
		}
		gtk_text_buffer_get_bounds (GTK_TEXT_BUFFER (buffer),
					    &iter1, 
					    &iter2);
		gtk_source_buffer_remove_all_source_tags (buffer,
							  &iter1,
							  &iter2);
	}
}

/* Idle worker code ------------ */

static gboolean
idle_worker (GtkSourceBuffer *source_buffer)
{
	GtkTextIter start_iter, end_iter, last_end_iter;
	gint i;
	
	if (source_buffer->priv->worker_last_offset >= 0) {
		/* the syntax regions table is incomplete */
		build_syntax_regions_table (source_buffer, NULL);
	}

	/* Now we highlight subregions requested by our views */
	gtk_text_buffer_get_iter_at_offset (GTK_TEXT_BUFFER (source_buffer), &last_end_iter, 0);
	for (i = 0; i < gtk_text_region_subregions (
		     source_buffer->priv->highlight_requests); i++) {
		gtk_text_region_nth_subregion (source_buffer->priv->highlight_requests,
					       i, &start_iter, &end_iter);

		if (source_buffer->priv->worker_last_offset < 0 ||
		    source_buffer->priv->worker_last_offset >=
		    gtk_text_iter_get_offset (&end_iter)) {
			ensure_highlighted (source_buffer, 
					    &start_iter, 
					    &end_iter);
			last_end_iter = end_iter;
		} else {
			/* since the subregions are ordered, we are
			 * guaranteed here that all subsequent
			 * subregions will be beyond the already
			 * analyzed text */
			break;
		}
	}
	gtk_text_buffer_get_iter_at_offset (GTK_TEXT_BUFFER (source_buffer), &start_iter, 0);

	if (!gtk_text_iter_equal (&start_iter, &last_end_iter)) {
		/* remove already highlighted subregions from requests */
		gtk_text_region_substract (source_buffer->priv->highlight_requests,
					   &start_iter, &last_end_iter);
		gtk_text_region_clear_zero_length_subregions (
			source_buffer->priv->highlight_requests);
	}
	
	if (source_buffer->priv->worker_last_offset < 0) {
		/* idle handler will be removed */
		source_buffer->priv->worker_handler = 0;
		return FALSE;
	}
	
	return TRUE;
}

static void
install_idle_worker (GtkSourceBuffer *source_buffer)
{
	if (source_buffer->priv->worker_handler == 0) {
		/* use the text view validation priority to get
		 * highlighted text even before complete validation of
		 * the buffer */
		source_buffer->priv->worker_handler =
			g_idle_add_full (GTK_TEXT_VIEW_PRIORITY_VALIDATE,
					 (GSourceFunc) idle_worker,
					 source_buffer, 
					 NULL);
	}
}

/* Syntax analysis code -------------- */

static gboolean
is_escaped (const gchar *text, gint index)
{
	gchar *tmp = (gchar *) text + index;
	gboolean retval = FALSE;

	tmp = g_utf8_find_prev_char (text, tmp);
	while (tmp && *tmp == '\\') {
		retval = !retval;
		tmp = g_utf8_find_prev_char (text, tmp);
	}
	return retval;
}

static const GtkSyntaxTag * 
get_syntax_start (GtkSourceBuffer      *source_buffer,
		  const gchar          *text,
		  gint                  length,
		  GtkSourceBufferMatch *match)
{
	GList *list;
	GtkSyntaxTag *tag;
	gint pos;
	
	/*
	g_return_val_if_fail (text != NULL, NULL);
	g_return_val_if_fail (length >= 0, NULL);
	g_return_val_if_fail (match != NULL, NULL);
	*/

	if (length == 0)
		return NULL;
	
	list = gtk_source_buffer_get_syntax_entries (source_buffer);

	if (list == NULL)
		return NULL;

	pos = 0;
	do {
		/* check for any of the syntax highlights */
		pos = gtk_source_regex_search (
			&source_buffer->priv->reg_syntax_all,
			text,
			pos,
			length,
			match);
		if (pos < 0 || !is_escaped (text, match->startindex))
			break;
		pos = match->startpos + 1;
	} while (pos >= 0);

	if (pos < 0)
		return NULL;

	while (list != NULL) {
		gint l;
		
		tag = list->data;
		
		l = gtk_source_regex_match (&tag->reg_start, text,
					    length, pos);

		if (l >= 0)
			return tag;

		list = g_list_next (list);
	}

	return NULL;
}

static gboolean 
get_syntax_end (const gchar          *text,
		gint                  length,
		GtkSyntaxTag         *tag,
		GtkSourceBufferMatch *match)
{
	GtkSourceBufferMatch tmp;
	gint pos;

	g_return_val_if_fail (text != NULL, FALSE);
	g_return_val_if_fail (length >= 0, FALSE);
	g_return_val_if_fail (tag != NULL, FALSE);

	if (!match)
		match = &tmp;
	
	pos = 0;
	do {
		pos = gtk_source_regex_search (&tag->reg_end, text, pos,
					       length, match);
		if (pos < 0 || !is_escaped (text, match->startindex))
			break;
		pos = match->startpos + 1;
	} while (pos >= 0);

	return (pos >= 0);
}

/* Syntax regions code ------------- */

static gint
bsearch_offset (GArray *array, gint offset)
{
	gint i, j, k;
	gint off_tmp;
	
	if (!array || array->len == 0)
		return 0;
	
	i = 0;
	/* border conditions */
	if (g_array_index (array, SyntaxDelimiter, i).offset > offset)
		return 0;
	j = array->len - 1;
	if (g_array_index (array, SyntaxDelimiter, j).offset <= offset)
		return array->len;
	
	while (j - i > 1) {
		k = (i + j) / 2;
		off_tmp = g_array_index (array, SyntaxDelimiter, k).offset;
		if (off_tmp == offset)
			return k + 1;
		else if (off_tmp > offset)
			j = k;
		else
			i = k;
	}
	return j;
}

static void
adjust_table_offsets (GArray *table, gint start, gint delta)
{
	if (!table)
		return;

	while (start < table->len) {
		g_array_index (table, SyntaxDelimiter, start).offset += delta;
		start++;
	}
}
	
static void 
invalidate_syntax_regions (GtkSourceBuffer *source_buffer,
			   GtkTextIter     *from,
			   gint             delta)
{
	GArray *table, *old_table;
	gint region, saved_region;
	gint offset;
	SyntaxDelimiter *delim;
	
	g_return_if_fail (GTK_IS_SOURCE_BUFFER (source_buffer));
	
	table = source_buffer->priv->syntax_regions;
	g_assert (table != NULL);
	
	if (from) {
		offset = gtk_text_iter_get_offset (from);
	} else {
		offset = 0;
	}

	DEBUG (g_message ("invalidating from %d", offset));
	
	if (!gtk_source_buffer_get_syntax_entries (source_buffer))
	{
		/* Shortcut case: we don't have syntax entries, so we
		 * won't build the table.  OTOH, we do need to refresh
		 * the highilighting in case there are pattern
		 * entries. */
		GtkTextIter start, end;
		
		g_array_set_size (table, 0);
		source_buffer->priv->worker_last_offset = -1;

		gtk_text_buffer_get_bounds (GTK_TEXT_BUFFER (source_buffer), &start, &end);
		if (from)
			start = *from;
		refresh_range (source_buffer, &start, &end);

		return;
	}
	
	/* check if the offset has been analyzed already */
	if ((source_buffer->priv->worker_last_offset >= 0) &&
	    (offset > source_buffer->priv->worker_last_offset))
		/* not yet */
		return;

	region = bsearch_offset (table, offset);
	if (region > 0) {
		delim = &g_array_index (table,
					SyntaxDelimiter,
					region - 1);
		if (delim->tag &&
		    delim->offset == offset) {
			/* take previous region if we are just at the
			   start of a syntax region (i.e. we're
			   invalidating because somebody deleted a
			   opening syntax pattern) */
			region--;
		}
	}
	
	/* if delta is negative, some text was deleted and surely some
	 * syntax delimiters have gone, so we don't need those in the
	 * saved table */
	if (delta < 0) {
		saved_region = bsearch_offset (table, offset - delta);
	} else {
		saved_region = region;
	}

	/* free saved old table */
	if (source_buffer->priv->old_syntax_regions) {
		g_array_free (source_buffer->priv->old_syntax_regions, TRUE);
		source_buffer->priv->old_syntax_regions = NULL;
	}

	/* we don't want to save information if delta is zero,
	 * i.e. the invalidation is not because the user edited the
	 * buffer */
	if (table->len - saved_region > 0 && delta != 0) {
		gint old_table_size;

		DEBUG (g_message ("saving table information"));
				
		/* save table to try to recover still valid information */
		old_table_size = table->len - saved_region;
		old_table = g_array_new (FALSE, FALSE, sizeof (SyntaxDelimiter));
		g_array_set_size (old_table, old_table_size);
		source_buffer->priv->old_syntax_regions = old_table;

		/* now copy from r through the end of the table */
		memcpy (&g_array_index (old_table, SyntaxDelimiter, 0),
			&g_array_index (table, SyntaxDelimiter, saved_region),
			sizeof (SyntaxDelimiter) * old_table_size);

		/* adjust saved table offsets */
		adjust_table_offsets (old_table, 0, delta);
	}
	
	/* chop table */
	g_array_set_size (table, region);

	/* update worker_last_offset from the new conditions in the table */
	if (region > 0) {
		source_buffer->priv->worker_last_offset =
			g_array_index (table, SyntaxDelimiter, region - 1).offset;
	} else {
		source_buffer->priv->worker_last_offset = 0;
	}
	
	install_idle_worker (source_buffer);
}

static gboolean
delimiter_is_equal (SyntaxDelimiter *d1, SyntaxDelimiter *d2)
{
	return (d1->offset == d2->offset &&
		d1->depth == d2->depth &&
		d1->tag == d2->tag);
}

/**
 * next_syntax_region:
 * @source_buffer: the GtkSourceBuffer to work on
 * @state: the current SyntaxDelimiter
 * @head: text to analyze
 * @head_length: length in bytes of @head
 * @head_offset: offset in the buffer where @head starts
 * @match: GtkSourceBufferMatch object to get the results
 * 
 * This function can be seen as a single iteration in the analyzing
 * process.  It takes the current @state, searches for the next syntax
 * pattern in head (starting from byte index 0) and if found, updates
 * @state to reflect the new state.  @match is also filled with the
 * matching bounds.
 * 
 * Return value: TRUE if a syntax pattern was found in @head.
 **/
static gboolean 
next_syntax_region (GtkSourceBuffer      *source_buffer,
		    SyntaxDelimiter      *state,
		    const gchar          *head,
		    gint                  head_length,
		    gint                  head_offset,
		    GtkSourceBufferMatch *match)
{
	GtkSyntaxTag *tag;
	gboolean found;
	
	if (!state->tag) {
		/* we come from a non-syntax colored region, so seek
		 * for an opening pattern */
		tag = (GtkSyntaxTag *) get_syntax_start (
			source_buffer, head, head_length, match);

		if (!tag)
			return FALSE;
		
		state->tag = tag;
		state->offset = match->startpos + head_offset;
		state->depth = 1;

	} else {
		/* seek the closing pattern for the current syntax
		 * region */
		found = get_syntax_end (head, head_length,
					state->tag, match);
		
		if (!found)
			return FALSE;
		
		state->offset = match->endpos + head_offset;
		state->tag = NULL;
		state->depth = 0;
		
	}
	return TRUE;
}

static void 
build_syntax_regions_table (GtkSourceBuffer *source_buffer,
			    GtkTextIter     *needed_end)
{
	GArray *table;
	GtkTextIter start, end;
	GArray *old_table;
	gint old_region;
	gboolean use_old_data;
	gchar *slice, *head;
	gint offset, head_length;
	GtkSourceBufferMatch match;
	SyntaxDelimiter delim;
	GTimer *timer;

	g_return_if_fail (GTK_IS_SOURCE_BUFFER (source_buffer));
	
	/* we shouldn't have been called if the buffer has no syntax entries */
	g_assert (gtk_source_buffer_get_syntax_entries (source_buffer) != NULL);
	
	/* check if we still have text to analyze */
	if (source_buffer->priv->worker_last_offset < 0)
		return;
	
	/* compute starting iter of the batch */
	offset = source_buffer->priv->worker_last_offset;
	gtk_text_buffer_get_iter_at_offset (GTK_TEXT_BUFFER (source_buffer),
					    &start, offset);
	
	DEBUG (g_message ("restarting syntax regions from %d", offset));
	
	/* compute ending iter of the batch */
	gtk_text_buffer_get_iter_at_offset (GTK_TEXT_BUFFER (source_buffer),
					    &end, offset + source_buffer->priv->
					    worker_batch_size);
	
	/* extend the range to include needed_end if necessary */
	if (needed_end && gtk_text_iter_compare (&end, needed_end) < 0)
		end = *needed_end;
	
	/* always stop processing at end of lines: this minimizes the
	 * chance of not getting a syntax pattern because it was split
	 * in between batches */
	if (!gtk_text_iter_ends_line (&end))
		gtk_text_iter_forward_to_line_end (&end);

	table = source_buffer->priv->syntax_regions;
	g_assert (table != NULL);
	
	/* get old table information */
	use_old_data = FALSE;
	old_table = source_buffer->priv->old_syntax_regions;
	old_region = old_table ? bsearch_offset (old_table, offset) : 0;
	
	/* setup analyzer */
	if (table->len == 0) {
		delim.offset = offset;
		delim.tag = NULL;
		delim.depth = 0;

	} else {
		delim = g_array_index (table, SyntaxDelimiter, table->len - 1);
		g_assert (delim.offset <= offset);
	}

	/* get slice of text to work on */
	slice = gtk_text_iter_get_slice (&start, &end);
	head = slice;
	head_length = strlen (head);

	timer = g_timer_new ();

	/* MAIN LOOP: build the table */
	while (head_length > 0) {
		if (!next_syntax_region (source_buffer,
					 &delim,
					 head,
					 head_length,
					 offset,
					 &match)) {
			/* no further data */
			break;
		}

		/* check if we can use the saved table */
		if (old_table && old_region < old_table->len) {
			/* don't fall behind the current match */
			while (old_region < old_table->len &&
			       g_array_index (old_table,
					      SyntaxDelimiter,
					      old_region).offset < delim.offset) {
				old_region++;
			}
			if (old_region < old_table->len &&
			    delimiter_is_equal (&delim,
						&g_array_index (old_table,
								SyntaxDelimiter,
								old_region))) {
				/* we have an exact match; we can use
				 * the saved data */
				use_old_data = TRUE;
				break;
			}
		}

		/* add the delimiter to the table */
		g_array_append_val (table, delim);
			
		/* move pointers */
		head += match.endindex;
		head_length -= match.endindex;
		offset += match.endpos;
	}
    
	g_free (slice);
	g_timer_stop (timer);

	if (use_old_data) {
		/* now we copy the saved information from old_table to
		 * the end of table */
		gint region = table->len;
		gint count = old_table->len - old_region;
		
		DEBUG (g_message ("copying %d delimiters from saved table information", count));

		g_array_set_size (table, table->len + count);
		memcpy (&g_array_index (table, SyntaxDelimiter, region),
			&g_array_index (old_table, SyntaxDelimiter, old_region),
			sizeof (SyntaxDelimiter) * count);
		
		/* set worker_last_offset from the last copied
		 * element, so we can continue to analyze the text in
		 * case the saved table was incomplete */
		region = table->len;
		offset = g_array_index (table, SyntaxDelimiter, region - 1).offset;
		source_buffer->priv->worker_last_offset = offset;
		gtk_text_iter_set_offset (&end, offset);
		
	} else {
		/* update worker information */
		source_buffer->priv->worker_last_offset =
			gtk_text_iter_is_end (&end) ? -1 : gtk_text_iter_get_offset (&end);
		
		head_length = gtk_text_iter_get_offset (&end) -	gtk_text_iter_get_offset (&start);
		
		if (head_length > 0) {
			/* update profile information only if we didn't use the saved data */
			source_buffer->priv->worker_batch_size =
				MAX (head_length * WORKER_TIME_SLICE
				     / (g_timer_elapsed (timer, NULL) * 1000),
				     MINIMUM_WORKER_BATCH);
		}
	}
		
	/* make sure the analyzed region gets highlighted */
	refresh_range (source_buffer, &start, &end);
	
	/* forget saved table if we have already "consumed" at least
	 * two of its delimiters, since that probably means it
	 * contains invalid, useless data */
	if (old_table && (use_old_data ||
			  source_buffer->priv->worker_last_offset < 0 ||
			  old_region > 1)) {
		g_array_free (old_table, TRUE);
		source_buffer->priv->old_syntax_regions = NULL;
	}
	
	PROFILE (g_message ("ended worker batch, %g ms elapsed",
			    g_timer_elapsed (timer, NULL) * 1000));
	DEBUG (g_message ("table has %u entries", table->len));

	g_timer_destroy (timer);
}

static void 
update_syntax_regions (GtkSourceBuffer *source_buffer,
		       gint             start_offset,
		       gint             delta)
{
	GArray *table;
	gint region;
	gint table_index, expected_end_index;
	gchar *slice, *head;
	gint head_length, head_offset;
	GtkTextIter start_iter, end_iter;
	GtkSourceBufferMatch match;
	SyntaxDelimiter delim;
	gboolean mismatch, started_in_syntax;
	
	table = source_buffer->priv->syntax_regions;
	g_assert (table != NULL);

	if (!source_buffer->priv->highlight)
		return;
	
	if (!gtk_source_buffer_get_syntax_entries (source_buffer))
	{
		/* Shortcut case: we don't have syntax entries, so we
		 * just refresh_range() the edited area */
		gtk_text_buffer_get_iter_at_offset (GTK_TEXT_BUFFER (source_buffer),
						    &start_iter, start_offset);
		end_iter = start_iter;
		if (delta > 0)
			gtk_text_iter_forward_chars (&end_iter, delta);
	
		gtk_text_iter_set_line_offset (&start_iter, 0);
		gtk_text_iter_forward_to_line_end (&end_iter);

		refresh_range (source_buffer, &start_iter, &end_iter);

		return;
	}
	
	/* check if the offset is at an unanalyzed region */
	if (source_buffer->priv->worker_last_offset >= 0 &&
	    start_offset >= source_buffer->priv->worker_last_offset) {
		/* update saved table offsets which potentially
		 * contain the offset */
		adjust_table_offsets (source_buffer->priv->old_syntax_regions, 0, delta);
		return;
	}
	
	/* search the edited region */
	region = bsearch_offset (table, start_offset);
	
	/* initialize analyzing context */
	started_in_syntax = FALSE;
	delim.tag = NULL;
	delim.offset = 0;
	delim.depth = 0;
	/* first expected match */
	table_index = region;
	/* last expected match (i.e. how far table_index is supossed to get) */
	expected_end_index = region;
	
	/* get the syntax region to analyze for changes */
	if (region > 0) {
		head_offset = g_array_index (table, SyntaxDelimiter, region - 1).offset;
		gtk_text_buffer_get_iter_at_offset (GTK_TEXT_BUFFER (source_buffer),
						    &start_iter,
						    head_offset);
		if (g_array_index (table, SyntaxDelimiter, region - 1).tag) {
			/* we are inside a syntax colored region, so
			 * we expect to see the opening delimiter
			 * first, and up to the closing delimited */
			started_in_syntax = TRUE;
			table_index = region - 1;
			expected_end_index = region + 1;
		}

		if (table_index > 0) {
			/* set the initial analyzing context to the
			 * delimiter right before the next expected
			 * delimiter */
			delim = g_array_index (table, SyntaxDelimiter, table_index - 1);
		}
		
	} else {
		/* no previous delimiter, so start analyzing at the
		 * start of the buffer */
		head_offset = 0;
		gtk_text_buffer_get_start_iter (GTK_TEXT_BUFFER (source_buffer),
						&start_iter);
	}
	/* we can't get past the end of the table */
	expected_end_index = MIN (expected_end_index, table->len);

	if (region < table->len) {
		gint end_offset;
		
		/* *corrected* end_offset */
		end_offset = g_array_index (table, SyntaxDelimiter, region).offset + delta;

		/* FIRST INVALIDATION CASE:
		 * the ending delimiter was deleted */
		if (end_offset < start_offset) {
			/* ending delimiter was deleted, so invalidate
			   from the starting delimiter onwards */
			DEBUG (g_message ("deleted ending delimiter"));
			invalidate_syntax_regions (source_buffer, &start_iter, delta);
			
			return;
		}

		/* set ending iter */
		gtk_text_buffer_get_iter_at_offset (GTK_TEXT_BUFFER (source_buffer),
						    &end_iter,
						    end_offset);
	
	} else {
		/* set the ending iter to the end of the buffer */
		gtk_text_buffer_get_end_iter (GTK_TEXT_BUFFER (source_buffer),
					      &end_iter);
	}

	/* get us the chunk of text to analyze */
	head = slice = gtk_text_iter_get_slice (&start_iter, &end_iter);
	head_length = strlen (head);

	/* We will start analyzing the slice of text and see if it
	 * matches the information from the table.  When we hit a
	 * mismatch, it means we need to invalidate. */
	mismatch = FALSE;
	while (next_syntax_region (source_buffer,
				   &delim,
				   head,
				   head_length,
				   head_offset,
				   &match)) {
		/* correct offset, since the table has the old offsets */
		if (delim.offset > start_offset + delta)
			delim.offset -= delta;

		if (table_index + 1 > table->len ||
		    !delimiter_is_equal (&delim,
					 &g_array_index (table,
							 SyntaxDelimiter,
							 table_index))) {
			/* SECOND INVALIDATION CASE: a mismatch
			   against the saved information or a new
			   delimiter not present in the table */
			mismatch = TRUE;
			break;
		}
		
		/* move pointers */
		head += match.endindex;
		head_length -= match.endindex;
		head_offset += match.endpos;
		table_index++;
	}

	g_free (slice);

	if (mismatch || table_index < expected_end_index) {
		/* we invalidate if there was a mismatch or we didn't
		 * advance the table_index enough (which means some
		 * delimiter was deleted) */
		DEBUG (g_message ("changed delimiter at %d", delim.offset));
		
		invalidate_syntax_regions (source_buffer, &start_iter, delta);
		
		return;
	}

	/* update trailing offsets with delta ... */
	adjust_table_offsets (table, region, delta);

	/* ... worker data ... */
	if (source_buffer->priv->worker_last_offset >= start_offset + delta)
		source_buffer->priv->worker_last_offset += delta;
	
	/* ... and saved table offsets */
	adjust_table_offsets (source_buffer->priv->old_syntax_regions, 0, delta);

	/* the syntax regions have not changed, so set the refreshing bounds */
	gtk_text_buffer_get_iter_at_offset (GTK_TEXT_BUFFER (source_buffer),
					    &start_iter, start_offset);
	end_iter = start_iter;
	if (delta > 0)
		gtk_text_iter_forward_chars (&end_iter, delta);
	
	if (!started_in_syntax) {
		/* we modified a non-syntax colored region, so we
		   adjust bounds to line bounds to correctly highlight
		   non-syntax patterns */
		gtk_text_iter_set_line_offset (&start_iter, 0);
		gtk_text_iter_forward_to_line_end (&end_iter);
	}
	
	refresh_range (source_buffer, &start_iter, &end_iter);
}

/* Beginning of highlighting code ------------ */

/**
 * search_patterns:
 * @matches: the starting list of matches to work from (can be NULL)
 * @text: the text which will be searched for
 * @length: the length (in bytes) of @text
 * @offset: the offset the beginning of @text is at
 * @index: an index to add the match indexes (usually: @text - base_text)
 * @patterns: additional patterns (can be NULL)
 * 
 * This function will fill and return a list of PatternMatch
 * structures ordered by match position in @text.  The initial list to
 * work on is @matches and it will be modified in-place.  Additional
 * new pattern tags might be specified in @patterns.
 *
 * From the patterns already in @matches only those whose starting
 * position is before @offset will be processed, and will be removed
 * if they don't match again.  New patterns will only be added if they
 * match.  The returned list is ordered.
 * 
 * Return value: the new list of matches
 **/
static GList * 
search_patterns (GList       *matches,
		 const gchar *text,
		 gint         length,
		 gint         offset,
		 gint         index,
		 GList       *patterns)
{
	GtkSourceBufferMatch match;
	PatternMatch *pmatch;
	GList *new_pattern;
	
	new_pattern = patterns;
	while (new_pattern || matches) {
		GtkPatternTag *tag;
		gint i;
		
		if (new_pattern) {
			/* process new patterns first */
			tag = new_pattern->data;
			new_pattern = new_pattern->next;
			pmatch = NULL;
		} else {
			/* process patterns already in @matches */
			pmatch = matches->data;
			tag = pmatch->tag;
			if (pmatch->match.startpos >= offset) {
				/* pattern is ahead of offset, so our
				 * work is done */
				break;
			}
			/* temporarily remove the PatternMatch from
			 * the list */
			matches = g_list_delete_link (matches, matches);
		}
		
		/* do the regex search on @text */
		i = gtk_source_regex_search (&tag->reg_pattern,
					     text,
					     0,
					     length,
					     &match);
		
		if (i >= 0 && match.endpos != i) {
			GList *p;
			
			/* create the match structure */
			if (!pmatch) {
				pmatch = g_new0 (PatternMatch, 1);
				pmatch->tag = tag;
			}
			/* adjust offsets (indexes remain relative to
			 * the current pointer in the text) */
			pmatch->match.startpos = match.startpos + offset;
			pmatch->match.endpos = match.endpos + offset;
			pmatch->match.startindex = match.startindex + index;
			pmatch->match.endindex = match.endindex + index;
			
			/* insert the match in order (prioritize longest match) */
			for (p = matches; p; p = p->next) {
				PatternMatch *tmp = p->data;
				if (tmp->match.startpos > pmatch->match.startpos ||
				    (tmp->match.startpos == pmatch->match.startpos &&
				     tmp->match.endpos < pmatch->match.endpos)) {
					break;
				}
			}
			matches = g_list_insert_before (matches, p, pmatch);

		} else if (pmatch) {
			/* either no match was found or the match has
			 * zero length (which probably indicates a
			 * buggy syntax pattern), so free the
			 * PatternMatch structure if we were analyzing
			 * a pattern from @matches */
			if (i >= 0 && i == match.endpos) {
				gchar *name;
				g_object_get (G_OBJECT (tag), "name", &name, NULL);
				g_warning ("The regex for pattern tag `%s' matched "
					   "a zero length string.  That's probably "
					   "due to a buggy regular expression.", name);
				g_free (name);
			}
			g_free (pmatch);
		}
	}

	return matches;
}

static void 
check_pattern (GtkSourceBuffer *source_buffer,
	       GtkTextIter     *start,
	       const gchar     *text,
	       gint             length)
{
	GList *matches;
	gint offset, index;
	GtkTextIter start_iter, end_iter;
	const gchar *ptr;

#ifdef ENABLE_PROFILE
	static GTimer *timer = NULL;
	static gdouble seconds = 0.0;
	static gint acc_length = 0;
#endif
	
	if (length == 0 || !gtk_source_buffer_get_pattern_entries (source_buffer))
		return;
	
	PROFILE ({
		if (timer == NULL)
			timer = g_timer_new ();
		acc_length += length;
		g_timer_start (timer);
	});
	
	/* setup environment */
	index = 0;
	offset = gtk_text_iter_get_offset (start);
	start_iter = end_iter = *start;
	ptr = text;
	
	/* get the initial list of matches */
	matches = search_patterns (NULL,
				   ptr, length,
				   offset, index,
				   gtk_source_buffer_get_pattern_entries (source_buffer));
	
	while (matches && length > 0) {
		/* pick the first (nearest) match... */
		PatternMatch *pmatch = matches->data;
		
		gtk_text_iter_set_offset (&start_iter,
					  pmatch->match.startpos);
		gtk_text_iter_set_offset (&end_iter,
					  pmatch->match.endpos);

		/* ... and apply it */
		gtk_text_buffer_apply_tag (GTK_TEXT_BUFFER (source_buffer),
					   GTK_TEXT_TAG (pmatch->tag),
					   &start_iter,
					   &end_iter);

		/* now skip it completely */
		offset = pmatch->match.endpos;
		index = pmatch->match.endindex;
		length -= (text + index) - ptr;
		ptr = text + index;

		/* and update matches from the new position */
		matches = search_patterns (matches,
					   ptr, length,
					   offset, index,
					   NULL);
	}

	if (matches) {
		/* matches should have been consumed completely */
		g_assert_not_reached ();
	}

	PROFILE ({
		g_timer_stop (timer);
		seconds += g_timer_elapsed (timer, NULL);
		g_message ("%g bytes/sec", acc_length / seconds);
	});
}

static void 
highlight_region (GtkSourceBuffer *source_buffer,
		  GtkTextIter     *start,
		  GtkTextIter     *end)
{
	GtkTextIter b_iter, e_iter;
	gint b_off, e_off, end_offset;
	GtkSyntaxTag *current_tag;
	SyntaxDelimiter *delim;
	GArray *table;
	gint region;
	gchar *slice, *slice_ptr;
	GTimer *timer;

	timer = NULL;
	PROFILE ({
 		timer = g_timer_new ();
 		g_message ("highlighting from %d to %d",
 			   gtk_text_iter_get_offset (start),
 			   gtk_text_iter_get_offset (end));
 	});

	
	table = source_buffer->priv->syntax_regions;
	g_return_if_fail (table != NULL);
	
	/* remove_all_tags is not efficient: for different positions
	   in the buffer it takes different times to complete, taking
	   longer if the slice is at the beginning */
	gtk_source_buffer_remove_all_source_tags (source_buffer, start, end);

	slice_ptr = slice = gtk_text_iter_get_slice (start, end);
	end_offset = gtk_text_iter_get_offset (end);
	
	/* get starting syntax region */
	b_off = gtk_text_iter_get_offset (start);
	region = bsearch_offset (table, b_off);
	delim = region > 0 && region <= table->len ?
		&g_array_index (table, SyntaxDelimiter, region - 1) :
		NULL;

	e_iter = *start;
	e_off = b_off;

	do {
		/* select region to work on */
		b_iter = e_iter;
		b_off = e_off;
		current_tag = delim ? delim->tag : NULL;
		region++;
		delim = region <= table->len ?
			&g_array_index (table, SyntaxDelimiter, region - 1) :
			NULL;

		if (delim)
			e_off = MIN (delim->offset, end_offset);
		else
			e_off = end_offset;
		gtk_text_iter_forward_chars (&e_iter, (e_off - b_off));

		/* do the highlighting for the selected region */
		if (current_tag) {
			/* apply syntax tag from b_iter to e_iter */
			gtk_text_buffer_apply_tag (GTK_TEXT_BUFFER (source_buffer),
						   GTK_TEXT_TAG (current_tag),
						   &b_iter,
						   &e_iter);

			slice_ptr = g_utf8_offset_to_pointer (slice_ptr,
							      e_off - b_off);
		
		} else {
			gchar *tmp;
			
			/* highlight from b_iter through e_iter using
			   non-syntax patterns */
			tmp = g_utf8_offset_to_pointer (slice_ptr,
							e_off - b_off);
			check_pattern (source_buffer, &b_iter,
				       slice_ptr, tmp - slice_ptr);
			slice_ptr = tmp;
		}

	} while (gtk_text_iter_compare (&b_iter, end) < 0);

	g_free (slice);

 	PROFILE ({
		g_message ("highlighting took %g ms",
			   g_timer_elapsed (timer, NULL) * 1000);
		g_timer_destroy (timer);
	});
}

static void
refresh_range (GtkSourceBuffer *buffer,
	       GtkTextIter     *start, 
	       GtkTextIter     *end)
{
	g_return_if_fail (GTK_IS_SOURCE_BUFFER (buffer));

	/* Add the region to the refresh region */
	gtk_text_region_add (buffer->priv->refresh_region, start, end);

	/* Notify views of the updated highlight region */
	g_signal_emit (buffer, buffer_signals [HIGHLIGHT_UPDATED], 0, start, end);
}

static void 
ensure_highlighted (GtkSourceBuffer *source_buffer,
		    GtkTextIter     *start,
		    GtkTextIter     *end)
{
	GtkTextRegion *region;
	
#if 0
	DEBUG (g_message ("ensure_highlighted %d to %d",
			  gtk_text_iter_get_offset (start),
			  gtk_text_iter_get_offset (end)));
#endif
	
	/* get the subregions not yet highlighted */
	region = gtk_text_region_intersect (
		source_buffer->priv->refresh_region, start, end);
	if (region) {
		GtkTextIter iter1, iter2;
		gint i;
		
		/* highlight all subregions from the intersection.
                   hopefully this will only be one subregion */
		for (i = 0; i < gtk_text_region_subregions (region); i++) {
			gtk_text_region_nth_subregion (region, i,
						       &iter1, &iter2);
			highlight_region (source_buffer, &iter1, &iter2);
		}
		gtk_text_region_destroy (region, TRUE);
		/* remove the just highlighted region */
		gtk_text_region_substract (source_buffer->priv->refresh_region,
					   start, 
					   end);
		gtk_text_region_clear_zero_length_subregions (
			source_buffer->priv->refresh_region);
	}
}

static void 
highlight_queue (GtkSourceBuffer *source_buffer,
		 GtkTextIter     *start,
		 GtkTextIter     *end)
{
	gtk_text_region_add (source_buffer->priv->highlight_requests,
			     start,
			     end);

	DEBUG (g_message ("queueing highlight [%d, %d]",
			  gtk_text_iter_get_offset (start),
			  gtk_text_iter_get_offset (end)));
}

void
_gtk_source_buffer_highlight_region (GtkSourceBuffer *source_buffer,
				    GtkTextIter     *start,
				    GtkTextIter     *end)
{
	g_return_if_fail (source_buffer != NULL);
	g_return_if_fail (start != NULL);
       	g_return_if_fail (end != NULL);

	if (!source_buffer->priv->highlight)
		return;

#ifndef LAZIEST_MODE
	if (source_buffer->priv->worker_last_offset < 0 ||
	    source_buffer->priv->worker_last_offset >= gtk_text_iter_get_offset (end)) {
		ensure_highlighted (source_buffer, start, end);
	} else
#endif
	{
		highlight_queue (source_buffer, start, end);
		install_idle_worker (source_buffer);
	}
}

/* This is a modified version of the gtk_text_buffer_remove_all_tags
 * function from gtk/gtktextbuffer.c
 *
 * Copyright (C) 2000 Red Hat, Inc.
 */

static gint
pointer_cmp (gconstpointer a, gconstpointer b)
{
	if (a < b)
		return -1;
	else if (a > b)
		return 1;
	else
		return 0;
}

/**
 * gtk_source_buffer_remove_all_source_tags:
 * @buffer: a #GtkSourceBuffer
 * @start: one bound of range to be untagged
 * @end: other bound of range to be untagged
 * 
 * Removes all tags in the range between @start and @end.  Be careful
 * with this function; it could remove tags added in code unrelated to
 * the code you're currently writing. That is, using this function is
 * probably a bad idea if you have two or more unrelated code sections
 * that add tags.
 **/
static void
gtk_source_buffer_remove_all_source_tags (GtkSourceBuffer   *buffer,
					  const GtkTextIter *start,
					  const GtkTextIter *end)
{
	GtkTextIter first, second, tmp;
	GSList *tags;
	GSList *tmp_list;
	GSList *prev;
	GtkTextTag *tag;
  
	/*
	g_return_if_fail (GTK_IS_SOURCE_BUFFER (buffer));
	g_return_if_fail (start != NULL);
	g_return_if_fail (end != NULL);
	g_return_if_fail (gtk_text_iter_get_buffer (start) == GTK_TEXT_BUFFER (buffer));
	g_return_if_fail (gtk_text_iter_get_buffer (end) == GTK_TEXT_BUFFER (buffer));
	*/
	
	first = *start;
	second = *end;

	gtk_text_iter_order (&first, &second);

	/* Get all tags turned on at the start */
	tags = gtk_text_iter_get_tags (&first);
  
	/* Find any that are toggled on within the range */
	tmp = first;
	while (gtk_text_iter_forward_to_tag_toggle (&tmp, NULL))
	{
		GSList *toggled;
		GSList *tmp_list2;

		if (gtk_text_iter_compare (&tmp, &second) >= 0)
			break; /* past the end of the range */
      
		toggled = gtk_text_iter_get_toggled_tags (&tmp, TRUE);

		/* We could end up with a really big-ass list here.
		 * Fix it someday.
		 */
		tmp_list2 = toggled;
		while (tmp_list2 != NULL)
		{
			if (GTK_IS_SOURCE_TAG (tmp_list2->data))
			{
				tags = g_slist_prepend (tags, tmp_list2->data);
			}

			tmp_list2 = g_slist_next (tmp_list2);
		}

		g_slist_free (toggled);
	}
  
	/* Sort the list */
	tags = g_slist_sort (tags, pointer_cmp);

	/* Strip duplicates */
	tag = NULL;
	prev = NULL;
	tmp_list = tags;

	while (tmp_list != NULL)
	{
		if (tag == tmp_list->data)
		{
			/* duplicate */
			if (prev)
				prev->next = tmp_list->next;

			tmp_list->next = NULL;

			g_slist_free (tmp_list);

			tmp_list = prev->next;
			/* prev is unchanged */
		}
		else
		{
			/* not a duplicate */
			tag = GTK_TEXT_TAG (tmp_list->data);
			prev = tmp_list;
			tmp_list = tmp_list->next;
		}
	}

	g_slist_foreach (tags, (GFunc) g_object_ref, NULL);
  
	tmp_list = tags;
	while (tmp_list != NULL)
	{
		tag = GTK_TEXT_TAG (tmp_list->data);

		gtk_text_buffer_remove_tag (GTK_TEXT_BUFFER (buffer), 
					    tag,
					    &first,
					    &second);
      
		tmp_list = tmp_list->next;
	}

	g_slist_foreach (tags, (GFunc) g_object_unref, NULL);
  
	g_slist_free (tags);
}
