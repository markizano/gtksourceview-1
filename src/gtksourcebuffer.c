/*  gtksourcebuffer.c
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

/* how many lines will the refresh idle handler process at a time */
#define DEFAULT_IDLE_REFRESH_LINES_PER_RUN  100
#define MINIMUM_IDLE_REFRESH_LINES_PER_RUN  50
/* in milliseconds */
#define IDLE_REFRESH_TIME_SLICE             40

#define LINES 25

typedef struct _MarkerSubList        MarkerSubList;
typedef struct _PreviousLineState    PreviousLineState;

enum {
	NO_OPEN_TAG,
	OPEN_SYNTAX_TAG
};


/* Signals */
enum {
	CAN_UNDO = 0,
	CAN_REDO,
	LAST_SIGNAL
};

struct _MarkerSubList 
{
	gint   line;
	GList *marker_list;
};

struct _PreviousLineState 
{

	gint                state;
	const GtkTextTag   *tag;
	GtkTextIter         start_of_tag;
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
	GtkSourceRegex     reg_syntax_all;

	/* Region covering the unhighlighted text */
	GtkTextRegion      *refresh_region;

	guint               refresh_idle_handler;
	guint               refresh_lines;

	GtkUndoManager     *undo_manager;
};


static GtkTextBufferClass *parent_class = NULL;
static guint 	buffer_signals[LAST_SIGNAL] = { 0 };

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

static void	 check_pattern 				(GtkSourceBuffer         *source_buffer,
							 GtkTextIter             *start, 
							 const gchar             *text, 
							 gint                     length);

static PreviousLineState *check_syntax 			(GtkSourceBuffer         *source_buffer,
							 GtkTextIter             *start,
							 const gchar             *text,
							 gint                     length,
							 const PreviousLineState *pl_state);

static GList 	*gtk_source_buffer_get_syntax_entries 	(const GtkSourceBuffer   *buffer);
static GList 	*gtk_source_buffer_get_pattern_entries 	(const GtkSourceBuffer   *buffer);

static void	 sync_syntax_regex 			(GtkSourceBuffer         *buffer);

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
	
	priv->refresh_idle_handler = 0;
	priv->refresh_region =  gtk_text_region_new (GTK_TEXT_BUFFER (buffer));
	priv->refresh_lines = DEFAULT_IDLE_REFRESH_LINES_PER_RUN;

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

	gtk_text_region_destroy (buffer->priv->refresh_region);
	g_object_unref (buffer->priv->undo_manager);

	/* TODO: free syntax_items, patterns, etc. - Paolo */
	
	G_OBJECT_CLASS (parent_class)->finalize (object);
}

GtkSourceBuffer *
gtk_source_buffer_new (GtkTextTagTable *table)
{
	GtkSourceBuffer *buffer;

	buffer = GTK_SOURCE_BUFFER (g_object_new (GTK_TYPE_SOURCE_BUFFER, NULL));

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

static gboolean
idle_refresh_handler (GtkSourceBuffer *source_buffer)
{
	gboolean retval;

	g_return_val_if_fail (GTK_IS_SOURCE_BUFFER (source_buffer), FALSE);

	if (!source_buffer->priv->highlight) {
		/* Nothing to do */
		/* Handler will be removed */
		source_buffer->priv->refresh_idle_handler = 0;

		return FALSE;
	}

	/* Make sure the region contains valid data */
	gtk_text_region_clear_zero_length_subregions (
			source_buffer->priv->refresh_region);

	if (gtk_text_region_subregions (source_buffer->priv->refresh_region) == 0) {
		/* Nothing to do */
		retval = FALSE;
	} else {
		GTimer *timer;
		gulong time_slice; /* microseconds */
		GtkTextIter start, end;

		/* Get us some work to do */
		gtk_text_region_nth_subregion (source_buffer->priv->refresh_region, 
					       0, 
					       &start,
					       &end);

		if ((gtk_text_iter_get_line (&end) - gtk_text_iter_get_line (&start)) >
		    source_buffer->priv->refresh_lines) {
			/* Region too big, reduce it */
			end = start;
			gtk_text_iter_forward_lines (&end,
						     source_buffer->priv->refresh_lines);
		}

		/*Profile syntax highlighting */
		timer = g_timer_new ();
		g_timer_start (timer);

		DEBUG (g_message
		       ("Req hi:  [%d, %d]",
			gtk_text_iter_get_offset (&start),
			gtk_text_iter_get_offset (&end)));

		highlight_region (source_buffer, &start, &end);

		DEBUG (g_message
		       ("Really hi:  [%d, %d]\n",
			gtk_text_iter_get_offset (&start),
			gtk_text_iter_get_offset (&end)));

		g_timer_stop (timer);
		g_timer_elapsed (timer, &time_slice);
		g_timer_destroy (timer);

		source_buffer->priv->refresh_lines =
			gtk_text_iter_get_line (&end) - gtk_text_iter_get_line (&start);

		/* Assume elapsed time is linear with number of lines
		   and make our best guess for next run */
		source_buffer->priv->refresh_lines =
			(IDLE_REFRESH_TIME_SLICE * 1000 * source_buffer->priv->refresh_lines) / 
				time_slice;

		source_buffer->priv->refresh_lines =
			MAX (source_buffer->priv->refresh_lines,
			     MINIMUM_IDLE_REFRESH_LINES_PER_RUN);

		/* Region done */
		gtk_text_region_substract (source_buffer->priv->refresh_region, 
					   &start, 
					   &end);

		if (gtk_text_region_subregions (source_buffer->priv->refresh_region) == 0)
			/* No more regions */
			retval = FALSE;
		else
			retval = TRUE;
	}

	if (!retval) {
		/* Nothing to do */
		/* Handler will be removed */
		source_buffer->priv->refresh_idle_handler = 0;
	}

	return retval;
}

static void
refresh_range (GtkSourceBuffer *buffer,
	       GtkTextIter     *start, 
	       GtkTextIter     *end)
{
	g_return_if_fail (GTK_IS_SOURCE_BUFFER (buffer));

	/* Add the region to the refresh region */
	gtk_text_region_add (buffer->priv->refresh_region, start, end);

	if (buffer->priv->highlight && (buffer->priv->refresh_idle_handler == 0)) {
		/* now add the idle handler if one was not running */
		buffer->priv->refresh_idle_handler =
		    g_idle_add_full (G_PRIORITY_DEFAULT_IDLE,
				     (GSourceFunc) idle_refresh_handler,
				     buffer, 
				     NULL);

		idle_refresh_handler (buffer);
	}


}

static void
gtk_source_buffer_real_insert_text (GtkTextBuffer *buffer,
				    GtkTextIter   *iter,
				    const gchar   *text, 
				    gint           len)
{
	const GtkSyntaxTag *tag;
	GtkTextIter start_iter, end_iter;
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

	gtk_text_buffer_get_iter_at_offset (buffer, 
					    &start_iter,
					    start_offset);
	end_iter = *iter;

	tag = NULL;

	if (GTK_SOURCE_BUFFER (buffer)->priv->syntax_items != NULL) {
		tag = iter_has_syntax_tag (&start_iter);

		if (tag != NULL) {
			iter_backward_to_tag_start (&start_iter,
						    GTK_TEXT_TAG (tag));
			iter_forward_to_tag_end (&end_iter,
						 GTK_TEXT_TAG (tag));
		}
	}

	if (tag == NULL) {
		/* No syntax tag found or no syntax item */

		/* Refresh from the start of the first inserted line to the 
		 * end of last inserted line */
		gtk_text_iter_set_line_offset (&start_iter, 0);
		tag = iter_has_syntax_tag (&start_iter);
		
		if ((tag != NULL) && 
		    !gtk_text_iter_begins_tag (&start_iter, GTK_TEXT_TAG (tag)))
			iter_forward_to_tag_end (&start_iter,
						 GTK_TEXT_TAG (tag));

		if (!gtk_text_iter_ends_line (&end_iter))
			gtk_text_iter_forward_to_line_end (&end_iter);

		tag = iter_has_syntax_tag (&end_iter);
		if (tag != NULL)
			iter_forward_to_tag_end (&end_iter,
						 GTK_TEXT_TAG (tag));
	}

	DEBUG (g_message ("INS Range : [%d - %d]\n",
			  gtk_text_iter_get_offset (&start_iter),
			  gtk_text_iter_get_offset (&end_iter)));

	refresh_range (GTK_SOURCE_BUFFER (buffer), &start_iter, &end_iter);
}

static void
gtk_source_buffer_real_delete_range (GtkTextBuffer *buffer,
				     GtkTextIter   *start,
				     GtkTextIter   *end)
{

	const GtkSyntaxTag *tag_start = NULL;
	const GtkSyntaxTag *tag_end = NULL;
	GtkTextIter *refresh_start = NULL;
	GtkTextIter *refresh_end = NULL;
	gint refresh_start_offset;
	gint refresh_end_offset;
	gint range_length;

	g_return_if_fail (GTK_IS_SOURCE_BUFFER (buffer));
	g_return_if_fail (start != NULL);
	g_return_if_fail (end != NULL);
	g_return_if_fail (gtk_text_iter_get_buffer (start) == buffer);
	g_return_if_fail (gtk_text_iter_get_buffer (end) == buffer);

	if (!GTK_SOURCE_BUFFER (buffer)->priv->highlight) {
		parent_class->delete_range (buffer, start, end);
		return;
	}

	/* First check if start and/or end hold a tag */
	/* If start holds a tag iterate backward to tag beginning */
	/* If end hold a tag iterate forward to tag end */

	gtk_text_iter_order (start, end);

	if (GTK_SOURCE_BUFFER (buffer)->priv->syntax_items) {
		tag_start = iter_has_syntax_tag (start);
		tag_end = iter_has_syntax_tag (end);

		if (tag_start != NULL) {
			refresh_start = gtk_text_iter_copy (start);
			iter_backward_to_tag_start (refresh_start,
						    GTK_TEXT_TAG (tag_start));
		}

		if (tag_end != NULL) {
			refresh_end = gtk_text_iter_copy (end);
			iter_forward_to_tag_end (refresh_end,
						 GTK_TEXT_TAG (tag_end));
		}
	}

	if (refresh_start == NULL) {
		refresh_start = gtk_text_iter_copy (start);
		gtk_text_iter_set_line_offset (refresh_start, 0);

		tag_start = iter_has_syntax_tag (refresh_start);
		if ((tag_start != NULL) && 
		    !gtk_text_iter_begins_tag (refresh_start,
					       GTK_TEXT_TAG (tag_start)))
			iter_forward_to_tag_end (refresh_start,
						 GTK_TEXT_TAG (tag_start));
	}

	if (refresh_end == NULL) {
		refresh_end = gtk_text_iter_copy (end);

		if (!gtk_text_iter_ends_line (refresh_end))
			gtk_text_iter_forward_to_line_end (refresh_end);

		tag_end = iter_has_syntax_tag (refresh_end);
		if (tag_end != NULL)
			iter_forward_to_tag_end (refresh_end,
						 GTK_TEXT_TAG (tag_end));
	}

	refresh_start_offset = gtk_text_iter_get_offset (refresh_start);
	refresh_end_offset = gtk_text_iter_get_offset (refresh_end);

	range_length =
	    gtk_text_iter_get_offset (end) - gtk_text_iter_get_offset (start);

	refresh_end_offset -= range_length;

	parent_class->delete_range (buffer, start, end);

	if ((refresh_end_offset - refresh_start_offset) == 0) {
		gtk_text_iter_free (refresh_start);
		gtk_text_iter_free (refresh_end);

		return;
	}

	DEBUG (g_message ("DEL Range : [%d - %d]\n",
			  refresh_start_offset, refresh_end_offset));

	/* Re-validate refresh_start and refresh_end */
	gtk_text_buffer_get_iter_at_offset (buffer, 
					    refresh_start,
					    refresh_start_offset);
	
	gtk_text_buffer_get_iter_at_offset (buffer, 
					    refresh_end,
					    refresh_end_offset);

	refresh_range (GTK_SOURCE_BUFFER (buffer), 
		       refresh_start,
		       refresh_end);
	
	gtk_text_iter_free (refresh_start);
	gtk_text_iter_free (refresh_end);
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

	buffer->priv->highlight = highlight;

	gtk_text_buffer_get_bounds (GTK_TEXT_BUFFER (buffer), &iter1,
				    &iter2);

	if (highlight)
		refresh_range (buffer, &iter1, &iter2);
	else {
		if (buffer->priv->refresh_idle_handler) {
			g_source_remove (buffer->priv->
					 refresh_idle_handler);
			buffer->priv->refresh_idle_handler = 0;
		}
		gtk_text_buffer_remove_all_tags (GTK_TEXT_BUFFER (buffer),
						 &iter1, &iter2);
	}
}

/*
 * Search for the beginning of a syntax tag.
 * Returns: found syntax tag or NULL
 */
static const GtkSyntaxTag *
get_syntax_start (GtkSourceBuffer * source_buffer,
		  const gchar * text,
		  gint length, GtkSourceBufferMatch * match)
{
	GList *list;
	GtkSyntaxTag *tag;
	gint pos;

	g_return_val_if_fail (text != NULL, NULL);
	g_return_val_if_fail (length > 0, NULL);
	g_return_val_if_fail (match != NULL, NULL);

	list = gtk_source_buffer_get_syntax_entries (source_buffer);

	if (list == NULL)
		return NULL;

	/* check for any of the syntax highlights */
	pos = gtk_source_regex_search (&source_buffer->priv->reg_syntax_all,
				       text,
				       0,
				       match);

	if (pos < 0)
		return NULL;

	while (list != NULL) {
		gint l;

		tag = GTK_SYNTAX_TAG (list->data);

		l = gtk_source_regex_match (&tag->reg_start, text, length, pos);

		if (l >= 0)
			return tag;

		list = g_list_next (list);
	}

	return NULL;
}

static gboolean
get_syntax_end (const gchar * text,
		gint length,
		GtkSyntaxTag * tag, GtkSourceBufferMatch * match)
{
	gint pos;

	g_return_val_if_fail (text != NULL, FALSE);
	g_return_val_if_fail (length > 0, FALSE);
	g_return_val_if_fail (tag != NULL, FALSE);

	pos = gtk_source_regex_search (&tag->reg_end, text, 0, match);

	return (pos >= 0);
}

static PreviousLineState *
check_syntax (GtkSourceBuffer * source_buffer,
	      GtkTextIter * start,
	      const gchar * text,
	      gint length, 
	      const PreviousLineState * pl_state)
{
	GList *list;

	g_return_val_if_fail (GTK_SOURCE_BUFFER (source_buffer), NULL);
	g_return_val_if_fail (start != NULL, NULL);
	g_return_val_if_fail (text != NULL, NULL);
	g_return_val_if_fail (length > 0, NULL);

	DEBUG (g_message ("check_syntax (start: %d, len: %d)",
			  gtk_text_iter_get_offset (start), length));

	list = gtk_source_buffer_get_syntax_entries (source_buffer);
	if (!list) {
		/* Check patterns */
		check_pattern (source_buffer, start, text, length);

		return NULL;
	}

	if ((pl_state == NULL) || (pl_state->state == NO_OPEN_TAG)) {
		/* Search for the beginning of a syntax tag */
		GtkSourceBufferMatch start_match;
		const GtkSyntaxTag *tag;

		DEBUG (g_message
		       ("check_syntax: Search for the beginning of a syntax tag"));

		tag =
		    get_syntax_start (source_buffer, text, length,
				      &start_match);

		if ((tag != NULL) && (start_match.startpos > 0)) {
			gchar *pos =
			    g_utf8_offset_to_pointer (text,
						      start_match.
						      startpos);

			DEBUG (g_message
			       ("check_syntax: found at %d",
				start_match.startpos));

			/* Check patterns */
			check_pattern (source_buffer, start, text,
				       pos - text);
		}

		if (tag != NULL) {
			gchar *pos;
			GtkTextIter start_iter;
			PreviousLineState *res;
			PreviousLineState current;

			DEBUG (g_message
			       ("check_syntax: found at %d",
				start_match.startpos));

			pos =
			    g_utf8_offset_to_pointer (text,
						      start_match.endpos);

			g_return_val_if_fail (pos < (text + length), NULL);

			current.state = OPEN_SYNTAX_TAG;
			current.tag = GTK_TEXT_TAG (tag);
			current.start_of_tag = *start;

			gtk_text_iter_forward_chars (&current.start_of_tag,
						     start_match.startpos);

			start_iter = *start;
			gtk_text_iter_forward_chars (&start_iter,
						     start_match.endpos);

			res = check_syntax (source_buffer,
					    &start_iter,
					    pos,
					    text + length - pos, &current);

			return res;
		} else {
			DEBUG (g_message ("check_syntax: not found"));

			/* Check patterns */
			check_pattern (source_buffer, start, text, length);

			return NULL;
		}
	}

	if ((pl_state->state == OPEN_SYNTAX_TAG)) {
		GtkSourceBufferMatch end_match;
		gboolean found;

		/* Check for the end of a syntax tag */
		found =
		    get_syntax_end (text, length,
				    GTK_SYNTAX_TAG (pl_state->tag),
				    &end_match);

		if (found) {
			GtkTextIter end_iter;
			gchar *pos;

			end_iter = *start;
			gtk_text_iter_forward_chars (&end_iter,
						     end_match.endpos);

			gtk_text_buffer_remove_all_tags (GTK_TEXT_BUFFER
							 (source_buffer),
							 &pl_state->
							 start_of_tag,
							 &end_iter);

			gtk_text_buffer_apply_tag (GTK_TEXT_BUFFER
						   (source_buffer),
						   GTK_TEXT_TAG (pl_state->
								 tag),
						   &pl_state->start_of_tag,
						   &end_iter);

			pos =
			    g_utf8_offset_to_pointer (text,
						      end_match.endpos);

			if (pos < (text + length)) {
				return check_syntax (source_buffer,
						     &end_iter,
						     pos,
						     text + length - pos,
						     NULL);
			} else {
				g_return_val_if_fail (pos ==
						      (text + length),
						      NULL);

				return NULL;
			}
		} else {
			PreviousLineState *res;

			res = g_new0 (PreviousLineState, 1);
			res->state = OPEN_SYNTAX_TAG;
			res->tag = pl_state->tag;
			res->start_of_tag = pl_state->start_of_tag;

			return res;
		}
	}

	g_return_val_if_fail (FALSE, NULL);
}

static void
check_pattern (GtkSourceBuffer * source_buffer,
	       GtkTextIter * start, const gchar * text, gint length)
{
	GList *patterns = NULL;
	gint utf8_len;
	GtkTextIter end_iter;
	gint offset;

	DEBUG (g_message ("check_pattern (start: %d, len: %d)",
			  gtk_text_iter_get_offset (start), length));

	patterns = gtk_source_buffer_get_pattern_entries (source_buffer);

	if (!patterns)
		return;

	/* The cast should be safe in this case - Paolo */
	utf8_len = (gint) g_utf8_strlen (text, length);

	offset = gtk_text_iter_get_offset (start);

	end_iter = *start;

	gtk_text_iter_set_offset (&end_iter, offset + utf8_len);

	gtk_text_buffer_remove_all_tags (GTK_TEXT_BUFFER (source_buffer),
					 start, &end_iter);
	while (patterns) {
		GtkTextIter start_iter;

		GtkPatternTag *tag;
		GtkSourceBufferMatch m;

		gint i;

		tag = GTK_PATTERN_TAG (patterns->data);

		start_iter = *start;

		i = 0;
		while (i < utf8_len && i >= 0) {
			i = gtk_source_regex_search (&tag->reg_pattern, text, i, &m);

			if (i >= 0) {
				if (m.endpos == i) {
					g_warning
					    ("Zero length regex match. "
					     "Probably a buggy syntax specification.");
					i++;
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
				i = m.endpos;
			}
		}

		patterns = g_list_next (patterns);
	}
}

static void
highlight_region (GtkSourceBuffer * source_buffer,
		  GtkTextIter * start, GtkTextIter * end)
{
	GtkTextIter b_iter;
	GtkTextIter e_iter;
	PreviousLineState *end_state_of_last_line;
	GtkTextIter end_iter;

	g_return_if_fail (GTK_SOURCE_BUFFER (source_buffer));
	g_return_if_fail (start != NULL);
	g_return_if_fail (end != NULL);

	DEBUG (g_message ("highlight_region : [%d - %d]\n",
			  gtk_text_iter_get_offset (start),
			  gtk_text_iter_get_offset (end))
	    );

	b_iter = *start;
	end_state_of_last_line = NULL;

	e_iter = b_iter;
	gtk_text_iter_forward_lines (&e_iter, LINES);

	gtk_text_buffer_get_end_iter (GTK_TEXT_BUFFER (source_buffer),
				      &end_iter);

	while ((gtk_text_iter_compare (&b_iter, end) < 0)
	       || (end_state_of_last_line != NULL)) {
		PreviousLineState *state;
		gchar *text;
		gint len;

		text = gtk_text_iter_get_slice (&b_iter, &e_iter);

		len = strlen (text);

		state = check_syntax (source_buffer,
				      &b_iter,
				      text, len, end_state_of_last_line);

		g_free (end_state_of_last_line);

		end_state_of_last_line = state;

		g_free (text);

		gtk_text_iter_forward_lines (&b_iter, LINES);

		*end = e_iter;
		gtk_text_iter_forward_lines (&e_iter, LINES);

		if ((end_state_of_last_line != NULL) &&
		    (gtk_text_iter_compare (&e_iter, &end_iter) == 0)) {
			gtk_text_buffer_remove_all_tags (GTK_TEXT_BUFFER
							 (source_buffer),
							 &end_state_of_last_line->
							 start_of_tag,
							 &end_iter);

			gtk_text_buffer_apply_tag (GTK_TEXT_BUFFER
						   (source_buffer),
						   GTK_TEXT_TAG
						   (end_state_of_last_line->
						    tag),
						   &end_state_of_last_line->
						   start_of_tag,
						   &end_iter);

			g_free (end_state_of_last_line);
			end_state_of_last_line = NULL;
		}
	}
}

