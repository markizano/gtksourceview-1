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
#include <libgnome/gnome-i18n.h>

#include "gtksourcebuffer.h"

#include "gtkundomanager.h"
#include "gtksourceview-marshal.h"
#include "gtktextregion.h"

/*
#define ENABLE_DEBUG
*/
#undef ENABLE_DEBUG

#ifdef ENABLE_DEBUG
#define DEBUG(x) (x)
#else
#define DEBUG(x)
#endif

/* define this to always highlight in an idle handler, and not
 * possibly in the expose method of the view */
#undef LAZIEST_MODE

/* in milliseconds */
#define WORKER_TIME_SLICE                   30
#define INITIAL_WORKER_BATCH                40960

typedef struct _MarkerSubList        MarkerSubList;
typedef struct _SyntaxDelimiter      SyntaxDelimiter;

/* Signals */
enum {
	CAN_UNDO = 0,
	CAN_REDO,
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

struct _GtkSourceBufferPrivate 
{
	gint                highlight:1;
	gint                check_brackets:1;

	GtkTextTag         *bracket_match_tag;
	GtkTextMark        *bracket_mark;

	GHashTable         *line_markers;

	GList              *syntax_items;
	GList              *pattern_items;
	GtkSourceRegex      reg_syntax_all;

	/* Region covering the unhighlighted text */
	GtkTextRegion      *refresh_region;

	/* Syntax regions data */
	GArray             *syntax_regions;
	gint                worker_last_offset;
	gint                worker_batch_size;
	guint               worker_handler;

	/* view visible region */
	gint                visible_start;
	gint                visible_end;
	
	GtkUndoManager     *undo_manager;
};


static GtkTextBufferClass *parent_class = NULL;
static guint 	 buffer_signals[LAST_SIGNAL] = { 0 };

static void 	 gtk_source_buffer_class_init		(GtkSourceBufferClass    *klass);
static void 	 gtk_source_buffer_init			(GtkSourceBuffer         *klass);
static void 	 gtk_source_buffer_finalize		(GObject                 *object);

static void 	 gtk_source_buffer_can_undo_handler 	(GtkUndoManager          *um,
							 gboolean                 can_undo,
							 GtkSourceBuffer         *buffer);
static void 	 gtk_source_buffer_can_redo_handler	(GtkUndoManager          *um,
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
							 GtkTextIter             *from);
static void      refresh_range                          (GtkSourceBuffer         *buffer,
							 GtkTextIter             *start,
							 GtkTextIter             *end);
static void      ensure_highlighted                     (GtkSourceBuffer         *source_buffer,
							 GtkTextIter             *start,
							 GtkTextIter             *end);



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
		
	object_class->finalize	= gtk_source_buffer_finalize;

	klass->can_undo 	= NULL;
	klass->can_redo 	= NULL;

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
}

static void
gtk_source_buffer_init (GtkSourceBuffer *buffer)
{
	GtkSourceBufferPrivate *priv;

	priv = g_new0 (GtkSourceBufferPrivate, 1);

	buffer->priv = priv;

	priv->undo_manager = gtk_undo_manager_new (buffer);

	priv->check_brackets = TRUE;
	priv->bracket_mark = NULL;
	
	priv->line_markers = g_hash_table_new (NULL, NULL);

	/* highlight data */
	priv->highlight = TRUE;
	priv->refresh_region =  gtk_text_region_new (GTK_TEXT_BUFFER (buffer));
	priv->syntax_regions =  g_array_new (FALSE, FALSE,
					     sizeof (SyntaxDelimiter));
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

	gtk_text_region_destroy (buffer->priv->refresh_region);
	g_object_unref (buffer->priv->undo_manager);

	g_array_free (buffer->priv->syntax_regions, TRUE);
	
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
gtk_source_buffer_new (GtkTextTagTable *table)
{
	GtkSourceBuffer *buffer;

	if (table != NULL) 
		buffer = GTK_SOURCE_BUFFER (g_object_new (GTK_TYPE_SOURCE_BUFFER, 
							  "tag_table", table, 
							  NULL));
	else
		buffer = GTK_SOURCE_BUFFER (g_object_new (GTK_TYPE_SOURCE_BUFFER, 
							  NULL));
	
	
	buffer->priv->bracket_match_tag = 
		gtk_text_buffer_create_tag (GTK_TEXT_BUFFER (buffer),
					    "gsb-bracket-match",
					    "foreground", "white",
					    "background", "red",
					    "weight", PANGO_WEIGHT_BOLD,
					    NULL);
	return buffer;
}


static void
gtk_source_buffer_can_undo_handler (GtkUndoManager  *um,
				    gboolean         can_undo,
				    GtkSourceBuffer *buffer)
{
	g_return_if_fail (GTK_IS_SOURCE_BUFFER (buffer));

	g_signal_emit (G_OBJECT (buffer),
		       buffer_signals[CAN_UNDO], 
		       0, 
		       can_undo);
}

static void
gtk_source_buffer_can_redo_handler (GtkUndoManager  *um,
				    gboolean         can_redo,
				    GtkSourceBuffer *buffer)
{
	g_return_if_fail (GTK_IS_SOURCE_BUFFER (buffer));

	g_signal_emit (G_OBJECT (buffer),
		       buffer_signals[CAN_REDO], 0, can_redo);
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

	GList *list = *(GList **) data;

	if (GTK_IS_SOURCE_TAG (tag))
		list = g_list_append (list, tag);
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

	if (gtk_source_buffer_find_bracket_match (iter)) {
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

GList *
gtk_source_buffer_get_regex_tags (const GtkSourceBuffer *buffer)
{
	GList *list = NULL;
	GtkTextTagTable *table;

	g_return_val_if_fail (GTK_IS_SOURCE_BUFFER (buffer), NULL);

	table = gtk_text_buffer_get_tag_table (GTK_TEXT_BUFFER (buffer));
	gtk_text_tag_table_foreach (table, get_tags_func, &list);
	list = g_list_first (list);

	return list;
}

void
gtk_source_buffer_purge_regex_tags (GtkSourceBuffer * buffer)
{
	GtkTextTagTable *table;
	GList *list;
	GList *cur;
	GtkTextIter start_iter;
	GtkTextIter end_iter;

	g_return_if_fail (GTK_IS_SOURCE_BUFFER (buffer));

	gtk_text_buffer_get_bounds (GTK_TEXT_BUFFER (buffer),
				    &start_iter, 
				    &end_iter);
	
	gtk_text_buffer_remove_all_tags (GTK_TEXT_BUFFER (buffer),
					 &start_iter, 
					 &end_iter);

	table = gtk_text_buffer_get_tag_table (GTK_TEXT_BUFFER (buffer));
	list = gtk_source_buffer_get_regex_tags (buffer);

	cur = list;
	while (cur != NULL) {
		gtk_text_tag_table_remove (table,
					   GTK_TEXT_TAG (cur->data));
		g_object_unref (G_OBJECT (cur->data));
		cur = g_list_next (cur);
	}

	g_list_free (list);

	if (buffer->priv->syntax_items) {
		g_list_free (buffer->priv->syntax_items);
		buffer->priv->syntax_items = NULL;
	}

	if (buffer->priv->pattern_items) {
		g_list_free (buffer->priv->pattern_items);
		buffer->priv->pattern_items = NULL;
	}
}

void
gtk_source_buffer_install_regex_tags (GtkSourceBuffer *buffer,
				      GList           *entries)
{
	GtkTextTagTable *tag_table;

	g_return_if_fail (GTK_IS_SOURCE_BUFFER (buffer));
	
	tag_table = gtk_text_buffer_get_tag_table (GTK_TEXT_BUFFER (buffer));
	g_return_if_fail (tag_table != NULL);
	
	while (entries != NULL) {
		
		gchar *name;

		g_object_get (G_OBJECT (entries->data), "name", &name, NULL);

		if (name != NULL) {
			GtkTextTag *tag;

			tag = gtk_text_tag_table_lookup (tag_table, name);
		
			/* FIXME: I'm not sure this is the right behavior - Paolo */	
			if (tag)
				gtk_text_tag_table_remove (tag_table, tag);
		}

		if (GTK_IS_SYNTAX_TAG (entries->data)) {
			buffer->priv->syntax_items =
			    g_list_append (buffer->priv->syntax_items, entries->data);
			
			gtk_text_tag_table_add (tag_table,
						GTK_TEXT_TAG (entries->data));
		} else if (GTK_IS_PATTERN_TAG (entries->data)) {
			buffer->priv->pattern_items =
			    g_list_append (buffer->priv->pattern_items, entries->data);
			
			gtk_text_tag_table_add (tag_table,
						GTK_TEXT_TAG (entries->data));

			/* lower priority for pattern tags */
			gtk_text_tag_set_priority (GTK_TEXT_TAG
						   (entries->data), 0);
		}

		if (name)
			g_free (name);

		entries = g_list_next (entries);
	}

	if (buffer->priv->syntax_items != NULL)
		sync_syntax_regex (buffer);

	if (buffer->priv->highlight)
		invalidate_syntax_regions (buffer, NULL);
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

gboolean
gtk_source_buffer_find_bracket_match (GtkTextIter *orig)
{
	GtkTextIter iter;
	
	gunichar base_char;
	gunichar search_char;
	gunichar cur_char;
	gint addition;

	gint counter;
	
	gboolean found;

	g_return_val_if_fail (orig != NULL, FALSE);

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

	do {
		gtk_text_iter_forward_chars (&iter, addition);
		cur_char = gtk_text_iter_get_char (&iter);
		
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
	while (!gtk_text_iter_is_end (&iter) && !gtk_text_iter_is_start (&iter));

	if (found)
		*orig = iter;

	return found;
}

gboolean
gtk_source_buffer_can_undo (const GtkSourceBuffer *buffer)
{
	g_return_val_if_fail (GTK_IS_SOURCE_BUFFER (buffer), FALSE);

	return gtk_undo_manager_can_undo (buffer->priv->undo_manager);
}

gboolean
gtk_source_buffer_can_redo (const GtkSourceBuffer *buffer)
{
	g_return_val_if_fail (GTK_IS_SOURCE_BUFFER (buffer), FALSE);

	return gtk_undo_manager_can_redo (buffer->priv->undo_manager);
}

void
gtk_source_buffer_undo (GtkSourceBuffer *buffer)
{
	g_return_if_fail (GTK_IS_SOURCE_BUFFER (buffer));
	g_return_if_fail (gtk_undo_manager_can_undo (buffer->priv->undo_manager));

	gtk_undo_manager_undo (buffer->priv->undo_manager);
}

void
gtk_source_buffer_redo (GtkSourceBuffer *buffer)
{
	g_return_if_fail (GTK_IS_SOURCE_BUFFER (buffer));
	g_return_if_fail (gtk_undo_manager_can_redo (buffer->priv->undo_manager));

	gtk_undo_manager_redo (buffer->priv->undo_manager);
}

gint
gtk_source_buffer_get_undo_levels (const GtkSourceBuffer *buffer)
{
	g_return_val_if_fail (GTK_IS_SOURCE_BUFFER (buffer), 0);

	return gtk_undo_manager_get_undo_levels (buffer->priv->undo_manager);
}

void
gtk_source_buffer_set_undo_levels (GtkSourceBuffer *buffer,
				   gint             undo_levels)
{
	g_return_if_fail (GTK_IS_SOURCE_BUFFER (buffer));

	gtk_undo_manager_set_undo_levels (buffer->priv->undo_manager,
					  undo_levels);
}

void
gtk_source_buffer_begin_not_undoable_action (GtkSourceBuffer *buffer)
{
	g_return_if_fail (GTK_IS_SOURCE_BUFFER (buffer));

	gtk_undo_manager_begin_not_undoable_action (buffer->priv->undo_manager);
}

void
gtk_source_buffer_end_not_undoable_action (GtkSourceBuffer *buffer)
{
	g_return_if_fail (GTK_IS_SOURCE_BUFFER (buffer));

	gtk_undo_manager_end_not_undoable_action (buffer->priv->undo_manager);
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
		invalidate_syntax_regions (buffer, NULL);

	} else {
		if (buffer->priv->worker_handler) {
			g_source_remove (buffer->priv->worker_handler);
			buffer->priv->worker_handler = 0;
		}
		gtk_text_buffer_get_bounds (GTK_TEXT_BUFFER (buffer),
					    &iter1, 
					    &iter2);
		gtk_text_buffer_remove_all_tags (GTK_TEXT_BUFFER (buffer),
						 &iter1, 
						 &iter2);
	}
}

/* Idle worker code ------------ */

static gboolean
idle_worker (GtkSourceBuffer *source_buffer)
{
	if (source_buffer->priv->worker_last_offset >= 0) {
		/* the syntax regions table is incomplete */
		build_syntax_regions_table (source_buffer, NULL);
	}
	
	if (source_buffer->priv->worker_last_offset < 0 ||
	    source_buffer->priv->worker_last_offset >=  source_buffer->priv->visible_end) 
	{
		GtkTextIter start_iter, end_iter;
		gtk_text_buffer_get_iter_at_offset (GTK_TEXT_BUFFER (source_buffer),
						    &start_iter, 
						    source_buffer->priv->visible_start);
		gtk_text_buffer_get_iter_at_offset (GTK_TEXT_BUFFER (source_buffer),
						    &end_iter, 
						    source_buffer->priv->visible_end);
		ensure_highlighted (source_buffer, 
				    &start_iter, 
				    &end_iter);
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
		
		l = re_match (&tag->reg_start.buf, text,
			      match->endindex, match->startindex,
			      &tag->reg_start.reg);
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
invalidate_syntax_regions (GtkSourceBuffer *source_buffer, GtkTextIter *from)
{
	GArray *table;
	gint region;
	gint offset;
	SyntaxDelimiter *delim;
	
	g_return_if_fail (GTK_IS_SOURCE_BUFFER (source_buffer));
	
	table = source_buffer->priv->syntax_regions;
	if (!table)
		return;
	
	if (from)
		offset = gtk_text_iter_get_offset (from);
	else
		offset = 0;

	DEBUG (g_message ("invalidating from %d", offset));
	
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
			/* take previous region if we are
			   just at the start of a syntax
			   region */
			region--;
		}
	}
	
	/* chop table */
	g_array_set_size (table, region);

	if (region > 0)
		source_buffer->priv->worker_last_offset = g_array_index (
			table, SyntaxDelimiter, region - 1).offset;
	else
		source_buffer->priv->worker_last_offset = 0;

	install_idle_worker (source_buffer);
}

static void 
build_syntax_regions_table (GtkSourceBuffer *source_buffer,
			    GtkTextIter     *needed_end)
{
	GArray *table;
	GtkTextIter start, end;
	gchar *slice, *head;
	gint offset, head_length;
	GtkSourceBufferMatch match;
	SyntaxDelimiter delim;
	GTimer *timer;

	g_return_if_fail (GTK_IS_SOURCE_BUFFER (source_buffer));
	
	timer = g_timer_new ();

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

	if (needed_end && gtk_text_iter_compare (&end, needed_end) < 0)
		end = *needed_end;
	
	if (!gtk_text_iter_ends_line (&end))
		gtk_text_iter_forward_to_line_end (&end);

	/* some sanity checks */
	g_assert (offset == 0 || source_buffer->priv->syntax_regions);
	
	if (!source_buffer->priv->syntax_regions) {
		/* Create the syntax regions table */
		GtkTextIter sb, eb;
		gint buffer_size;

		gtk_text_buffer_get_bounds (GTK_TEXT_BUFFER (source_buffer),
					    &sb, &eb);
		buffer_size = gtk_text_iter_get_offset (&eb)
			- gtk_text_iter_get_offset (&sb);
		/* estimate a syntax pattern every 200 characters... */
		table = g_array_sized_new (FALSE, FALSE,
					   sizeof (SyntaxDelimiter),
					   buffer_size / 200);
		source_buffer->priv->syntax_regions = table;

	} else {
		table = source_buffer->priv->syntax_regions;
	}

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

	DEBUG (g_message ("starting batch of %d bytes, %g ms",
			  head_length,
			  g_timer_elapsed (timer, NULL) * 1000));
    
	/* build the table */
	while (head_length > 0) {
		if (!delim.tag) {
			delim.tag = (GtkSyntaxTag *) get_syntax_start (
				source_buffer, head, head_length, &match);

			if (!delim.tag)
				break;
		
			delim.offset = match.startpos + offset;
			delim.depth = 1;

		} else {
			gboolean found;

			found = get_syntax_end (head, head_length,
						delim.tag, &match);

			if (!found)
				break;

			delim.offset = match.endpos + offset;
			delim.tag = NULL;
			delim.depth = 0;

		}
		g_array_append_val (table, delim);
			
		/* move pointers */
		head += match.endindex;
		head_length -= match.endindex;
		offset += match.endpos;
	}
    
	g_free (slice);
	g_timer_stop (timer);

	/* update worker information */
	source_buffer->priv->worker_last_offset =
		gtk_text_iter_is_end (&end) ? -1 :
		gtk_text_iter_get_offset (&end);
	head_length = gtk_text_iter_get_offset (&end) -
		gtk_text_iter_get_offset (&start);
	if (head_length > 0) {
		source_buffer->priv->worker_batch_size = head_length * WORKER_TIME_SLICE
			/ (g_timer_elapsed (timer, NULL) * 1000);
		/* make sure the analyzed region gets highlighted */
		refresh_range (source_buffer, &start, &end);
	}
	
	DEBUG ({
		g_message ("ended worker batch, %g ms elapsed",
			   g_timer_elapsed (timer, NULL) * 1000);
		g_message ("table has %u entries", table->len);
	});

	g_timer_destroy (timer);
}

static void 
update_syntax_regions (GtkSourceBuffer *source_buffer,
		       gint             start_offset,
		       gint             delta)
{
	GArray *table;
	gint region;
	SyntaxDelimiter begin_delim, end_delim;
	gchar *slice, *head;
	gint head_length, head_offset;
	GtkTextIter start_iter, end_iter;
	GtkSourceBufferMatch match;
	const GtkSyntaxTag *tag;
	
	table = source_buffer->priv->syntax_regions;
	g_assert (table != NULL);

	/* check if the offset is at an unanalyzed region */
	if ((source_buffer->priv->worker_last_offset >= 0) &&
	    (start_offset >= source_buffer->priv->worker_last_offset))
		return;
	
	region = bsearch_offset (table, start_offset);

	/* get lower delimiter */
	if (region > 0) {
		begin_delim = g_array_index (table,
					     SyntaxDelimiter,
					     region - 1);
	} else {
		/* there's no beginning delimiter, which means it's
		 * the start of the buffer */
		begin_delim.tag = NULL;
		begin_delim.offset = 0;
		begin_delim.depth = 0;
	}

	/* initially set starting iter to the lower delimiter */
	gtk_text_buffer_get_iter_at_offset (GTK_TEXT_BUFFER (source_buffer),
					    &start_iter,
					    begin_delim.offset);

	/* initially set the ending iter to the end of the buffer */
	gtk_text_buffer_get_end_iter (GTK_TEXT_BUFFER (source_buffer),
				      &end_iter);
	
	/* get upper delimiter */
	if (region < table->len) {
		end_delim = g_array_index (table, SyntaxDelimiter, region);
		end_delim.offset += delta;
		if (end_delim.offset < start_offset) {
			/* deleted text makes ending delimiter to disappear
			   completely, so set bounding iters and rebuild
			   syntax regions table from starting delimiter */
			invalidate_syntax_regions (source_buffer, &start_iter);
			
			DEBUG (g_message ("deleted ending delimiter"));
			
			return;
		}

		/* fix ending iter */
		gtk_text_buffer_get_iter_at_offset (
			GTK_TEXT_BUFFER (source_buffer),
			&end_iter, end_delim.offset);
	
	} else {
		/* there's no ending delimiter, which means it's just
		 * the end of the buffer */
		end_delim.tag = begin_delim.tag;
		end_delim.offset = gtk_text_iter_get_offset (&end_iter);
		end_delim.depth = begin_delim.depth;
	}

	/* get us the chunk of text to analyze */
	head = slice = gtk_text_iter_get_slice (&start_iter, &end_iter);
	head_length = strlen (head);
	head_offset = begin_delim.offset;
	
	/* if we're inside a syntax region */
	if (begin_delim.tag != NULL) {
		gboolean found;

		/* verify that the beginning syntax pattern has not changed */
		tag = get_syntax_start (source_buffer, head,
					head_length, &match);
		if (tag != begin_delim.tag || match.startpos != 0) {
			/* opening pattern has changed... invalidate */
			gtk_text_buffer_get_end_iter (
				GTK_TEXT_BUFFER (source_buffer), &end_iter);
			gtk_text_iter_set_line_offset (&start_iter, 0);
			invalidate_syntax_regions (source_buffer, &start_iter);
			g_free (slice);

			DEBUG (g_message ("changed starting delimiter"));
			
			return;
		}
		/* eat up the syntax delimiter */
		head += match.endindex;
		head_length -= match.endindex;
		head_offset += match.endpos;

		/* FIXME: this is not quite right for end of buffer */
		/* now search for ending tag (if we support nested
		 * syntax regions, we need to search for the syntax
		 * pattern of the ending delimiter) */
		found = get_syntax_end (head, head_length,
					begin_delim.tag, &match);
		if (!found || match.endpos + head_offset != end_delim.offset) {
			/* not found or changed position */
			gtk_text_buffer_get_end_iter (
				GTK_TEXT_BUFFER (source_buffer), &end_iter);
			invalidate_syntax_regions (source_buffer, &start_iter);
			g_free (slice);
		
			DEBUG (g_message ("changed ending delimiter, %d != %d",
					  match.endpos + head_offset,
					  end_delim.offset));
			
			return;
		}
		/* eat up the ending syntax delimiter */
		head_length -= (match.endindex - match.startindex);
	}

	/* search for other starting syntax tags if the region being
	 * analyzed is not a syntax region (the condition needs to be
	 * fixed if we are to support nested regions)... if there are
	 * any we need to invalidate */
	if (!begin_delim.tag) {
		tag = get_syntax_start (source_buffer, head,
					head_length, &match);
		if (tag) {
			gtk_text_buffer_get_end_iter (
				GTK_TEXT_BUFFER (source_buffer), &end_iter);
			invalidate_syntax_regions (source_buffer, &start_iter);
			g_free (slice);
		
			DEBUG (g_message ("found new starting delimiter at %d",
					  match.startpos));
			
			return;
		}
	}

	g_free (slice);

	/* the syntax regions have not changed, so set the refreshing bounds */
	gtk_text_buffer_get_iter_at_offset (GTK_TEXT_BUFFER (source_buffer),
					    &start_iter, start_offset);
	end_iter = start_iter;
	if (delta > 0)
		gtk_text_iter_forward_chars (&end_iter, delta);
	
	if (!begin_delim.tag) {
		/* we modified a non-syntax region, so we adjust bounds to
		   line bounds to correctly highlight non-syntax patterns */
		gtk_text_iter_set_line_offset (&start_iter, 0);
		gtk_text_iter_forward_to_line_end (&end_iter);
	}
	
	/* update trailing offsets with delta */
	while (region < table->len) {
		g_array_index (table, SyntaxDelimiter, region).offset += delta;
		region++;
	}

	/* update worker data too */
	if (source_buffer->priv->worker_last_offset >= start_offset + delta)
		source_buffer->priv->worker_last_offset += delta;
	
	refresh_range (source_buffer, &start_iter, &end_iter);
}

/* Beginning of highlighting code ------------ */

static void 
check_pattern (GtkSourceBuffer *source_buffer,
	       GtkTextIter     *start,
	       const gchar     *text,
	       gint             length)
{
	GList *patterns = NULL;
	gint offset;
	gint utf8_len, len;
	
	patterns = gtk_source_buffer_get_pattern_entries (source_buffer);

	if (!patterns)
		return;

	/* The cast should be safe in this case - Paolo */
	utf8_len = (gint) g_utf8_strlen (text, length);

	while (patterns) {
		GtkTextIter start_iter, end_iter;
		GtkPatternTag *tag;
		GtkSourceBufferMatch m;
		gchar *tmp;
		gint i;

		offset = gtk_text_iter_get_offset (start);
		tag = GTK_PATTERN_TAG (patterns->data);
		start_iter = end_iter = *start;
		i = 0;
		len = length;
		tmp = (gchar *) text;
		
		while (i < utf8_len && i >= 0) {
			i = gtk_source_regex_search (&tag->reg_pattern,
						     tmp,
						     0,
						     len,
						     &m);

			if (i >= 0) {
				if (m.endpos == i) {
					g_warning
					    ("Zero length regex match. "
					     "Probably a buggy syntax "
					     "specification.");
					offset += i + 1;
					len -= g_utf8_next_char (tmp) - tmp;
					tmp = g_utf8_next_char (tmp);
					continue;
				}

				gtk_text_iter_set_offset (&start_iter,
							  offset + i);
				gtk_text_iter_set_offset (&end_iter,
							  offset +
							  m.endpos);

				gtk_text_buffer_apply_tag (GTK_TEXT_BUFFER
							   (source_buffer),
							   GTK_TEXT_TAG
							   (tag),
							   &start_iter,
							   &end_iter);
				offset += m.endpos;
				len -= m.endindex;
				tmp += m.endindex;
			}
		}

		patterns = g_list_next (patterns);
	}
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
	DEBUG (timer = g_timer_new ());
	DEBUG (g_message ("highlighting from %d to %d",
			  gtk_text_iter_get_offset (start),
			  gtk_text_iter_get_offset (end)));
	
	table = source_buffer->priv->syntax_regions;
	g_return_if_fail (table != NULL);
	
	/* remove_all_tags is not efficient: for different positions
	   in the buffer it takes different times to complete, taking
	   longer if the slice is at the beginning */
	gtk_text_buffer_remove_all_tags (GTK_TEXT_BUFFER (source_buffer),
					 start, end);

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
			gtk_text_buffer_apply_tag (GTK_TEXT_BUFFER
						   (source_buffer),
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

	DEBUG ({
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

	/* and possibly install the idle worker */
	if (buffer->priv->highlight) {
		install_idle_worker (buffer);
	}
}

static void 
ensure_highlighted (GtkSourceBuffer *source_buffer,
		    GtkTextIter     *start,
		    GtkTextIter     *end)
{
	GtkTextRegion *region;
	
	DEBUG (g_message ("ensure_highlighted %d to %d",
			  gtk_text_iter_get_offset (start),
			  gtk_text_iter_get_offset (end)));

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
		gtk_text_region_destroy (region);
		/* remove the just highlighted region */
		gtk_text_region_substract (source_buffer->priv->refresh_region,
					   start, 
					   end);
		gtk_text_region_clear_zero_length_subregions (
				source_buffer->priv->refresh_region);
	}
}

void
gtk_source_buffer_highlight_region (GtkSourceBuffer *source_buffer,
				    GtkTextIter     *start,
				    GtkTextIter     *end)
{
	g_return_if_fail (source_buffer != NULL);
	g_return_if_fail (start != NULL && end != NULL);

	if (!source_buffer->priv->highlight)
		return;

	/* FIXME: support multiple views */
	source_buffer->priv->visible_start = gtk_text_iter_get_offset (start);
	source_buffer->priv->visible_end = gtk_text_iter_get_offset (end);

#ifndef LAZIEST_MODE
	if (source_buffer->priv->worker_last_offset < 0 ||
	    source_buffer->priv->worker_last_offset >= source_buffer->priv->visible_end) {
		ensure_highlighted (source_buffer, start, end);
	} else
#endif
	{
		install_idle_worker (source_buffer);
	}
}
