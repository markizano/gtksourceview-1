/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- 
 *  gtksourceview.c
 *
 *  Copyright (C) 2001 - Mikael Hermansson <tyan@linux.se> and
 *  Chris Phelps <chicane@reninet.com>
 *
 *  Copyright (C) 2003 - Gustavo Giráldez and Paolo Maggi 
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Library General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU Library General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <pango/pango-tabs.h>

#include "gtksourceview-i18n.h"
#include "gtksourceview-marshal.h"
#include "gtksourceview.h"


#define GUTTER_PIXMAP 			16
#define DEFAULT_TAB_WIDTH 		8
#define MIN_NUMBER_WINDOW_WIDTH		20

enum {
	UNDO,
	REDO,
	LAST_SIGNAL
};

struct _GtkSourceViewPrivate
{
	guint		 tabs_width;
	gboolean 	 show_line_numbers;
	gboolean	 show_line_pixmaps;
	
	GHashTable 	*pixmap_cache;

	GtkSourceBuffer *source_buffer;
	gint		 old_lines;
};


static guint signals[LAST_SIGNAL] = { 0 };

static GObjectClass *parent_class = NULL;

/* Prototypes. */
static void	gtk_source_view_class_init 		(GtkSourceViewClass *klass);
static void	gtk_source_view_init 			(GtkSourceView      *view);
static void 	gtk_source_view_finalize 		(GObject            *object);

static void 	gtk_source_view_pixbuf_foreach_unref 	(gpointer            key,
						  	 gpointer            value,
						  	 gpointer            user_data);

static void	gtk_source_view_undo 			(GtkSourceView      *view);
static void	gtk_source_view_redo 			(GtkSourceView      *view);

static void 	set_source_buffer 			(GtkSourceView      *view,
			       				 GtkTextBuffer      *buffer);

static void	gtk_source_view_populate_popup 		(GtkTextView        *view,
					    		 GtkMenu            *menu);
static void 	menu_item_activate_cb 			(GtkWidget          *menu_item,
				  			 GtkTextView        *text_view);

static void 	gtk_source_view_draw_line_markers 	(GtkSourceView      *view,
					       		 gint                line,
					       		 gint                x,
					       		 gint                y);

#if 0
static GdkPixbuf *gtk_source_view_get_line_marker (GtkSourceView *view,
						   GList *list);
#endif

static void 	gtk_source_view_get_lines 		(GtkTextView       *text_view,
				       			 gint               first_y,
				       			 gint               last_y,
				       			 GArray            *buffer_coords,
				       			 GArray            *numbers,
				       			 gint              *countp);
static gint     gtk_source_view_expose 			(GtkWidget         *widget,
							 GdkEventExpose    *event);


/* Private functions. */
static void
gtk_source_view_class_init (GtkSourceViewClass *klass)
{
	GObjectClass	 *object_class;
	GtkTextViewClass *textview_class;
	GtkBindingSet    *binding_set;
	GtkWidgetClass   *widget_class;
	
	object_class 	= G_OBJECT_CLASS (klass);
	textview_class 	= GTK_TEXT_VIEW_CLASS (klass);
	parent_class 	= g_type_class_peek_parent (klass);
	widget_class 	= GTK_WIDGET_CLASS (klass);
	
	object_class->finalize = gtk_source_view_finalize;
	widget_class->expose_event = gtk_source_view_expose;
	
	textview_class->populate_popup = gtk_source_view_populate_popup;
	
	klass->undo = gtk_source_view_undo;
	klass->redo = gtk_source_view_redo;

	signals [UNDO] =
		g_signal_new ("undo",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
			      G_STRUCT_OFFSET (GtkSourceViewClass, undo),
			      NULL,
			      NULL,
			      gtksourceview_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);
	signals [REDO] =
		g_signal_new ("redo",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
			      G_STRUCT_OFFSET (GtkSourceViewClass, redo),
			      NULL,
			      NULL,
			      gtksourceview_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);

	binding_set = gtk_binding_set_by_class (klass);

	gtk_binding_entry_add_signal (binding_set,
				      GDK_z,
				      GDK_CONTROL_MASK,
				      "undo", 0);
	gtk_binding_entry_add_signal (binding_set,
				      GDK_z,
				      GDK_CONTROL_MASK | GDK_SHIFT_MASK,
				      "redo", 0);
}

static void
view_realize_cb (GtkWidget *widget, GtkSourceView *view)
{
	g_return_if_fail (GTK_IS_SOURCE_VIEW (view));
			
	/* Set tab size: this function must be called after the widget is
	 * realized */
	gtk_source_view_set_tabs_width (view, 
					view->priv->tabs_width);
}

static void
gtk_source_view_init (GtkSourceView *view)
{
	view->priv = g_new0 (GtkSourceViewPrivate, 1);

	view->priv->tabs_width = DEFAULT_TAB_WIDTH;

	view->priv->pixmap_cache = g_hash_table_new (g_str_hash, g_str_equal);

	/* FIXME: remove when we will use properties - Paolo */
	gtk_source_view_set_show_line_numbers (view, FALSE);
	gtk_source_view_set_show_line_pixmaps (view, FALSE);

	gtk_text_view_set_left_margin (GTK_TEXT_VIEW (view), 2);
	gtk_text_view_set_right_margin (GTK_TEXT_VIEW (view), 2);

	g_signal_connect (G_OBJECT (view),
			  "realize",
			  G_CALLBACK (view_realize_cb),
			  view);
}

static void
gtk_source_view_finalize (GObject *object)
{
	GtkSourceView *view;

	g_return_if_fail (object != NULL);
	g_return_if_fail (GTK_IS_SOURCE_VIEW (object));

	view = GTK_SOURCE_VIEW (object);

	if (view->priv->pixmap_cache) 
	{
		g_hash_table_foreach_remove (view->priv->pixmap_cache,
					     (GHRFunc) gtk_source_view_pixbuf_foreach_unref,
					     NULL);
		g_hash_table_destroy (view->priv->pixmap_cache);
	}
	
	set_source_buffer (view, NULL);

	g_free (view->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void 
highlight_updated_cb (GtkSourceBuffer *buffer,
		      GtkTextIter     *start,
		      GtkTextIter     *end,
		      GtkTextView     *text_view)
{
	GdkRectangle visible_rect;
	GdkRectangle updated_rect;	
	GdkRectangle redraw_rect;
	gint y;
	gint height;
	
	/* get visible area */
	gtk_text_view_get_visible_rect (text_view, &visible_rect);
	
	/* get updated rectangle */
	gtk_text_view_get_line_yrange (text_view, start, &y, &height);
	updated_rect.y = y;
	gtk_text_view_get_line_yrange (text_view, end, &y, &height);
	updated_rect.height = y + height - updated_rect.y;
	updated_rect.x = visible_rect.x;
	updated_rect.width = visible_rect.width;

	/* intersect both rectangles to see whether we need to queue a redraw */
	if (gdk_rectangle_intersect (&updated_rect, &visible_rect, &redraw_rect)) 
	{
		GdkRectangle widget_rect;
		
		gtk_text_view_buffer_to_window_coords (text_view,
						       GTK_TEXT_WINDOW_WIDGET,
						       redraw_rect.x,
						       redraw_rect.y,
						       &widget_rect.x,
						       &widget_rect.y);
		
		widget_rect.width = redraw_rect.width;
		widget_rect.height = redraw_rect.height;
		
		gtk_widget_queue_draw_area (GTK_WIDGET (text_view),
					    widget_rect.x,
					    widget_rect.y,
					    widget_rect.width,
					    widget_rect.height);
	}
}

static void
set_source_buffer (GtkSourceView *view, GtkTextBuffer *buffer)
{
	/* keep our pointer to the source buffer in sync with
	 * textview's, though it would be a lot nicer if GtkTextView
	 * had a "set_buffer" signal */
	/* FIXME: in gtk 2.3 we have a buffer property so we can
	 * connect to the notify signal.  Unfortunately we can't
	 * depend on gtk 2.3 yet (see bug #108353) */
	if (view->priv->source_buffer) 
	{
		g_signal_handlers_disconnect_by_func (view->priv->source_buffer,
						      highlight_updated_cb,
						      view);
		g_object_remove_weak_pointer (G_OBJECT (view->priv->source_buffer),
					      (gpointer *) &view->priv->source_buffer);
	}
	if (buffer && GTK_IS_SOURCE_BUFFER (buffer)) 
	{
		view->priv->source_buffer = GTK_SOURCE_BUFFER (buffer);
		g_object_add_weak_pointer (G_OBJECT (buffer),
					   (gpointer *) &view->priv->source_buffer);
		g_signal_connect (buffer,
				  "highlight_updated",
				  G_CALLBACK (highlight_updated_cb),
				  view);
	}
	else 
	{
		view->priv->source_buffer = NULL;
	}
}

static void
gtk_source_view_pixbuf_foreach_unref (gpointer key,
				      gpointer value,
				      gpointer user_data)
{
	g_object_unref (G_OBJECT (value));
}

static void
gtk_source_view_undo (GtkSourceView *view)
{
	GtkSourceBuffer *buffer;
	
	g_return_if_fail (view != NULL);
	g_return_if_fail (GTK_IS_SOURCE_VIEW (view));

	buffer = GTK_SOURCE_BUFFER (
			gtk_text_view_get_buffer (GTK_TEXT_VIEW (view)));
		
	if (gtk_source_buffer_can_undo (buffer))
		gtk_source_buffer_undo (buffer);
}

static void
gtk_source_view_redo (GtkSourceView *view)
{
	GtkSourceBuffer *buffer;

	g_return_if_fail (view != NULL);
	g_return_if_fail (GTK_IS_SOURCE_VIEW (view));

	buffer = GTK_SOURCE_BUFFER (
			gtk_text_view_get_buffer (GTK_TEXT_VIEW (view)));
		
	if (gtk_source_buffer_can_redo (buffer))
		gtk_source_buffer_redo (buffer);
}

static void
gtk_source_view_populate_popup (GtkTextView *text_view,
				GtkMenu     *menu)
{
	GtkTextBuffer *buffer;
	GtkWidget *menu_item;

	buffer = gtk_text_view_get_buffer (text_view);
	if (!buffer && !GTK_IS_SOURCE_BUFFER (buffer))
		return;

	/* separator */
	menu_item = gtk_menu_item_new ();
	gtk_menu_shell_prepend (GTK_MENU_SHELL (menu), menu_item);
	gtk_widget_show (menu_item);

	/* create undo menu_item. */
	menu_item = gtk_image_menu_item_new_from_stock ("gtk-undo", NULL);
	g_object_set_data (G_OBJECT (menu_item), "gtk-signal", "undo");
	g_signal_connect (G_OBJECT (menu_item), "activate",
			  G_CALLBACK (menu_item_activate_cb), text_view);
	gtk_menu_shell_prepend (GTK_MENU_SHELL (menu), menu_item);
	gtk_widget_set_sensitive (menu_item, 
				  gtk_source_buffer_can_undo (GTK_SOURCE_BUFFER (buffer)));
	gtk_widget_show (menu_item);

	/* create redo menu_item. */
	menu_item = gtk_image_menu_item_new_from_stock ("gtk-redo", NULL);
	g_object_set_data (G_OBJECT (menu_item), "gtk-signal", "redo");
	g_signal_connect (G_OBJECT (menu_item), "activate",
			  G_CALLBACK (menu_item_activate_cb), text_view);
	gtk_menu_shell_prepend (GTK_MENU_SHELL (menu), menu_item);
	gtk_widget_set_sensitive (menu_item,
				  gtk_source_buffer_can_redo (GTK_SOURCE_BUFFER (buffer)));
	gtk_widget_show (menu_item);
}

static void
menu_item_activate_cb (GtkWidget   *menu_item,
		       GtkTextView *text_view)
{
	const gchar *signal;

	signal = g_object_get_data (G_OBJECT (menu_item), "gtk-signal");
	g_signal_emit_by_name (G_OBJECT (text_view), signal);
}

#if 0
static GdkPixbuf *
gtk_source_view_get_line_marker (GtkSourceView *view,
				 GList         *list)
{
	GdkPixbuf *pixbuf;
	GdkPixbuf *composite;
	GList *iter;

	pixbuf = gtk_source_view_get_pixbuf (view, (const gchar *) list->data);
	if (!pixbuf) {
		g_warning ("Unknown marker '%s' used.", (char*)list->data);
		return NULL;
	}

	if (!list->next)
		g_object_ref (pixbuf);
	else {
		pixbuf = gdk_pixbuf_copy (pixbuf);
		for (iter = list->next; iter; iter = iter->next) {
			composite = gtk_source_view_get_pixbuf (view,
								(const gchar *) iter->data);
			if (composite) {
				gint width;
				gint height;
				gint comp_width;
				gint comp_height;

				width = gdk_pixbuf_get_width (pixbuf);
				height = gdk_pixbuf_get_height (pixbuf);
				comp_width = gdk_pixbuf_get_width (composite);
				comp_height = gdk_pixbuf_get_height (composite);
				gdk_pixbuf_composite ((const GdkPixbuf *) composite,
						      pixbuf,
						      0, 0,
						      width, height,
						      0, 0,
						      width / comp_width,
						      height / comp_height,
						      GDK_INTERP_BILINEAR,
						      225);
			} else
				g_warning ("Unknown marker '%s' used", (char*)iter->data);
		}
	}

	return pixbuf;
}
#endif
static void
gtk_source_view_draw_line_markers (GtkSourceView *view,
				   gint           line,
				   gint           x,
				   gint           y)
{
#if 0
	GList *list;
	GdkPixbuf *pixbuf;
	GdkWindow *win = gtk_text_view_get_window (GTK_TEXT_VIEW (view),
						   GTK_TEXT_WINDOW_LEFT);

	list = (GList *)
		gtk_source_buffer_line_get_markers (GTK_SOURCE_BUFFER
						    (GTK_TEXT_VIEW (view)->buffer), line);
	if (list) {
		if ((pixbuf = gtk_source_view_get_line_marker (view, list))) {
			gdk_pixbuf_render_to_drawable_alpha (pixbuf, GDK_DRAWABLE (win), 0, 0,
							     x, y,
							     gdk_pixbuf_get_width (pixbuf),
							     gdk_pixbuf_get_height (pixbuf),
							     GDK_PIXBUF_ALPHA_BILEVEL,
							     127, GDK_RGB_DITHER_NORMAL, 0, 0);
			g_object_unref (pixbuf);
		}
	}
#endif 
}

/* This function is taken from gtk+/tests/testtext.c */
static void
gtk_source_view_get_lines (GtkTextView  *text_view,
			   gint          first_y,
			   gint          last_y,
			   GArray       *buffer_coords,
			   GArray       *numbers,
			   gint         *countp)
{
	GtkTextIter iter;
	gint count;
	gint size;
      	gint last_line_num;	

	g_array_set_size (buffer_coords, 0);
	g_array_set_size (numbers, 0);
  
	/* Get iter at first y */
	gtk_text_view_get_line_at_y (text_view, &iter, first_y, NULL);

	/* For each iter, get its location and add it to the arrays.
	 * Stop when we pass last_y
	*/
	count = 0;
  	size = 0;

  	while (!gtk_text_iter_is_end (&iter))
    	{
		gint y, height;
      
		gtk_text_view_get_line_yrange (text_view, &iter, &y, &height);

		g_array_append_val (buffer_coords, y);
		last_line_num = gtk_text_iter_get_line (&iter);
		g_array_append_val (numbers, last_line_num);
      	
		++count;

		if ((y + height) >= last_y)
			break;
      
		gtk_text_iter_forward_line (&iter);
	}

	if (gtk_text_iter_is_end (&iter))
    	{
		gint y, height;
		gint line_num;
      
		gtk_text_view_get_line_yrange (text_view, &iter, &y, &height);

		line_num = gtk_text_iter_get_line (&iter);

		if (line_num != last_line_num)
		{
			g_array_append_val (buffer_coords, y);
			g_array_append_val (numbers, line_num);
			++count;
		}
	}

	*countp = count;
}

static void
gtk_source_view_paint_margin (GtkSourceView *view,
			      GdkEventExpose *event)
{
	GtkTextView *text_view;
	GdkWindow *win;
	PangoLayout *layout;
	GArray *numbers;
	GArray *pixels;
	gchar *str;
	gint y1;
	gint y2;
	gint count;
	gint margin_width;
	gint text_width;
	gint i;

	text_view = GTK_TEXT_VIEW (view);

	if (!view->priv->show_line_numbers && !view->priv->show_line_pixmaps)
	{
		gtk_text_view_set_border_window_size (GTK_TEXT_VIEW (text_view),
						      GTK_TEXT_WINDOW_LEFT,
						      0);

		return;
	}

	win = gtk_text_view_get_window (text_view,
					GTK_TEXT_WINDOW_LEFT);

	y1 = event->area.y;
	y2 = y1 + event->area.height;

	/* get the extents of the line printing */
	gtk_text_view_window_to_buffer_coords (text_view,
					       GTK_TEXT_WINDOW_LEFT,
					       0,
					       y1,
					       NULL,
					       &y1);

	gtk_text_view_window_to_buffer_coords (text_view,
					       GTK_TEXT_WINDOW_LEFT,
					       0,
					       y2,
					       NULL,
					       &y2);

	numbers = g_array_new (FALSE, FALSE, sizeof (gint));
	pixels = g_array_new (FALSE, FALSE, sizeof (gint));

	/* get the line numbers and y coordinates. */
	gtk_source_view_get_lines (text_view,
				   y1,
				   y2,
				   pixels,
				   numbers,
				   &count);

	/* A zero-lined document should display a "1"; we don't need to worry about
	scrolling effects of the text widget in this special case */
	
	if (count == 0)
	{
		gint y = 0;
		gint n = 0;
		count = 1;
		g_array_append_val (pixels, y);
		g_array_append_val (numbers, n);
	}

	/* set size. */
	str = g_strdup_printf ("%d", MAX (99,
					  gtk_text_buffer_get_line_count (text_view->buffer)));
	layout = gtk_widget_create_pango_layout (GTK_WIDGET (view), str);
	g_free (str);

	pango_layout_get_pixel_size (layout, &text_width, NULL);
	
	pango_layout_set_width (layout, text_width);
	pango_layout_set_alignment (layout, PANGO_ALIGN_RIGHT);

	/* determine the width of the left margin. */
	if (view->priv->show_line_numbers && view->priv->show_line_pixmaps)
		margin_width = text_width + 4 + GUTTER_PIXMAP;
	else if (view->priv->show_line_numbers)
		margin_width = text_width + 4;
	else if (view->priv->show_line_pixmaps)
		margin_width = GUTTER_PIXMAP;
	else
		margin_width = 0;

	g_return_if_fail (margin_width != 0);
	
	gtk_text_view_set_border_window_size (GTK_TEXT_VIEW (text_view),
					      GTK_TEXT_WINDOW_LEFT,
					      margin_width);
	
	i = 0;
	while (i < count) 
	{
		gint pos;

		gtk_text_view_buffer_to_window_coords (text_view,
						       GTK_TEXT_WINDOW_LEFT,
						       0,
						       g_array_index (pixels, gint, i),
						       NULL,
						       &pos);

		if (view->priv->show_line_numbers ) 
		{
			str = g_strdup_printf ("%d", g_array_index (numbers, gint, i) + 1);

			pango_layout_set_text (layout, str, -1);

			gtk_paint_layout (GTK_WIDGET (view)->style,
					  win,
					  GTK_WIDGET_STATE (view),
					  FALSE,
					  NULL,
					  GTK_WIDGET (view),
					  NULL,
					  text_width + 2, 
					  pos,
					  layout);

			g_free (str);
		}

		if (view->priv->show_line_pixmaps) 
		{
			gint x;

			if (view->priv->show_line_numbers)
				x = text_width + 4;
			else
				x = 0;
			
			gtk_source_view_draw_line_markers (view,
							   g_array_index (numbers, gint, i) + 1,
							   x,
							   pos);
		}

		++i;
	}

	g_array_free (pixels, TRUE);
	g_array_free (numbers, TRUE);

	g_object_unref (G_OBJECT (layout));
}

static gint
gtk_source_view_expose (GtkWidget      *widget,
			GdkEventExpose *event)
{
	GtkSourceView *view;
	GtkTextView *text_view;
	gboolean event_handled;
	
	view = GTK_SOURCE_VIEW (widget);
	text_view = GTK_TEXT_VIEW (widget);

	event_handled = FALSE;
	
	/* maintain the our source_buffer pointer synchronized */
	if (text_view->buffer != GTK_TEXT_BUFFER (view->priv->source_buffer) &&
	    GTK_IS_SOURCE_BUFFER (text_view->buffer)) 
	{
		set_source_buffer (view, text_view->buffer);
	}
	
	/* check if the expose event is for the text window first, and
	 * make sure the visible region is highlighted */
	if (event->window == gtk_text_view_get_window (text_view, GTK_TEXT_WINDOW_TEXT) &&
	    view->priv->source_buffer != NULL) 
	{
		GdkRectangle visible_rect;
		GtkTextIter iter1, iter2;
		
		gtk_text_view_get_visible_rect (text_view, &visible_rect);
		gtk_text_view_get_line_at_y (text_view, &iter1,
					     visible_rect.y, NULL);
		gtk_text_iter_backward_line (&iter1);
		gtk_text_view_get_line_at_y (text_view, &iter2,
					     visible_rect.y
					     + visible_rect.height, NULL);
		gtk_text_iter_forward_line (&iter2);

		_gtk_source_buffer_highlight_region (view->priv->source_buffer,
						     &iter1, &iter2);
	}

	/* now check for the left window, which contains the margin */
	if (event->window == gtk_text_view_get_window (text_view,
						       GTK_TEXT_WINDOW_LEFT)) 
	{
		gtk_source_view_paint_margin (view, event);
		event_handled = TRUE;
	} 
	else 
	{
		gint lines;

		/* FIXME: could it be a performances problem? - Paolo */
		lines = gtk_text_buffer_get_line_count (text_view->buffer);

		if (view->priv->old_lines != lines)
		{
			GdkWindow *w;
			view->priv->old_lines = lines;

			w = gtk_text_view_get_window (text_view, GTK_TEXT_WINDOW_LEFT);

			if (w != NULL)
				gdk_window_invalidate_rect (w, NULL, FALSE);
		}

		if (GTK_WIDGET_CLASS (parent_class)->expose_event)
			event_handled = 
				(* GTK_WIDGET_CLASS (parent_class)->expose_event)
				(widget, event);
	}
	
	return event_handled;	
}


/*
 *This is a pretty important function...we call it when the tab_stop is changed,
 *And when the font is changed.
 *NOTE: You must change this with the default font for now...
 *It may be a good idea to set the tab_width for each GtkTextTag as well
 *based on the font that we set at creation time
 *something like style_cache_set_tabs_from_font or the like.
 *Now, this *may* not be necessary because most tabs wont be inside of another highlight,
 *except for things like multi-line comments (which will usually have an italic font which
 *may or may not be a different size than the standard one), or if some random language
 *definition decides that it would be spiffy to have a bg color for "start of line" whitespace
 *"^\(\t\| \)+" would probably do the trick for that.
 */
static gint
calculate_real_tab_width (GtkSourceView *view, guint tab_size)
{
	PangoLayout *layout;
	gchar *tab_string;
	gint counter = 0;
	gint tab_width = 0;

	if (tab_size == 0)
		return -1;

	tab_string = g_malloc (tab_size + 1);

	while (counter < tab_size) {
		tab_string [counter] = ' ';
		counter++;
	}

	tab_string [tab_size] = 0;

	layout = gtk_widget_create_pango_layout (
			GTK_WIDGET (view), 
			tab_string);
	g_free (tab_string);

	if (layout != NULL) {
		pango_layout_get_pixel_size (layout, &tab_width, NULL);
		g_object_unref (G_OBJECT (layout));
	} else
		tab_width = -1;

	return tab_width;
}


/* ----------------------------------------------------------------------
 * Public interface 
 * ---------------------------------------------------------------------- */

GtkWidget *
gtk_source_view_new ()
{
	GtkWidget *widget;
	GtkSourceBuffer *buffer;

	buffer = gtk_source_buffer_new (NULL);
	widget = gtk_source_view_new_with_buffer (buffer);
	g_object_unref (buffer);
	return widget;
}

GtkWidget *
gtk_source_view_new_with_buffer (GtkSourceBuffer *buffer)
{
	GtkWidget *view;

	view = g_object_new (GTK_TYPE_SOURCE_VIEW, NULL);
	gtk_text_view_set_buffer (GTK_TEXT_VIEW (view), GTK_TEXT_BUFFER (buffer));

	return view;
}

GType
gtk_source_view_get_type (void)
{
	static GType our_type = 0;

	if (our_type == 0) {
		static const GTypeInfo our_info = {
			sizeof (GtkSourceViewClass),
			(GBaseInitFunc) NULL,
			(GBaseFinalizeFunc) NULL,
			(GClassInitFunc) gtk_source_view_class_init,
			NULL,	/* class_finalize */
			NULL,	/* class_data */
			sizeof (GtkSourceView),
			0,	/* n_preallocs */
			(GInstanceInitFunc) gtk_source_view_init
		};

		our_type = g_type_register_static (GTK_TYPE_TEXT_VIEW,
						   "GtkSourceView",
						   &our_info, 0);
	}

	return our_type;
}

gboolean
gtk_source_view_get_show_line_numbers (const GtkSourceView *view)
{
	g_return_val_if_fail (view != NULL, FALSE);
	g_return_val_if_fail (GTK_IS_SOURCE_VIEW (view), FALSE);

	return view->priv->show_line_numbers;
}

void
gtk_source_view_set_show_line_numbers (GtkSourceView *view,
				       gboolean       visible)
{
	g_return_if_fail (view != NULL);
	g_return_if_fail (GTK_IS_SOURCE_VIEW (view));

	if (visible) 
	{
		if (!view->priv->show_line_numbers) 
		{
			/* Set left margin to minimum width if no margin is 
			   visible yet. Otherwise, just queue a redraw, so the
			   expose handler will automatically adjust the margin. */
			if (!view->priv->show_line_pixmaps)
				gtk_text_view_set_border_window_size (GTK_TEXT_VIEW (view),
								      GTK_TEXT_WINDOW_LEFT,
								      MIN_NUMBER_WINDOW_WIDTH);
			else
				gtk_widget_queue_draw (GTK_WIDGET (view));

			view->priv->show_line_numbers = visible;
		}
	} 
	else 
	{
		if (view->priv->show_line_numbers) 
		{
			view->priv->show_line_numbers = visible;

			/* force expose event, which will adjust margin. */
			gtk_widget_queue_draw (GTK_WIDGET (view));
		}
	}
}

gboolean
gtk_source_view_get_show_line_pixmaps (const GtkSourceView *view)
{
	g_return_val_if_fail (view != NULL, FALSE);
	g_return_val_if_fail (GTK_IS_SOURCE_VIEW (view), FALSE);

	return view->priv->show_line_pixmaps;
}

void
gtk_source_view_set_show_line_pixmaps (GtkSourceView *view,
				       gboolean       visible)
{
	g_return_if_fail (view != NULL);
	g_return_if_fail (GTK_IS_SOURCE_VIEW (view));

	if (visible) 
	{
		if (!view->priv->show_line_pixmaps) 
		{
			/* Set left margin to minimum width if no margin is 
			   visible yet. Otherwise, just queue a redraw, so the
			   expose handler will automatically adjust the margin. */
			if (!view->priv->show_line_numbers)
				gtk_text_view_set_border_window_size (GTK_TEXT_VIEW (view),
								      GTK_TEXT_WINDOW_LEFT,
								      MIN_NUMBER_WINDOW_WIDTH);
			else
				gtk_widget_queue_draw (GTK_WIDGET (view));

			view->priv->show_line_pixmaps = visible;
		}
	} 
	else 
	{
		if (view->priv->show_line_pixmaps) 
		{
			view->priv->show_line_pixmaps = visible;

			/* force expose event, which will adjust margin. */
			gtk_widget_queue_draw (GTK_WIDGET (view));
		}
	}
}

guint
gtk_source_view_get_tabs_width (const GtkSourceView *view)
{
	g_return_val_if_fail (view != NULL, FALSE);
	g_return_val_if_fail (GTK_IS_SOURCE_VIEW (view), FALSE);

	return view->priv->tabs_width;
}


/* This function must be called after the widget is
 * realized 
 */
void
gtk_source_view_set_tabs_width (GtkSourceView *view,
				guint          width)
{
	PangoTabArray *tab_array;
	gint real_tab_width;

	g_return_if_fail (GTK_SOURCE_VIEW (view));
	g_return_if_fail (width <= 32);

	if (view->priv->tabs_width == width)
		return;

	real_tab_width = calculate_real_tab_width (
					GTK_SOURCE_VIEW (view),
					width);

	if (real_tab_width < 0)
	{
		g_warning ("Impossible to set tabs width.");
		return;
	}
	
	tab_array = pango_tab_array_new (1, TRUE);
	pango_tab_array_set_tab (tab_array, 0, PANGO_TAB_LEFT, real_tab_width);

	gtk_text_view_set_tabs (GTK_TEXT_VIEW (view), 
				tab_array);

	pango_tab_array_free (tab_array);

	view->priv->tabs_width = width;
}

/*
gboolean
gtk_source_view_add_pixbuf (GtkSourceView *view,
			    const gchar   *key,
			    GdkPixbuf     *pixbuf,
			    gboolean       overwrite)
{
	gpointer data = NULL;
	gboolean replaced = FALSE;

	g_return_val_if_fail (view != NULL, FALSE);
	g_return_val_if_fail (GTK_IS_SOURCE_VIEW (view), FALSE);

	data = g_hash_table_lookup (view->pixmap_cache, key);
	if (data && !overwrite)
		return (FALSE);

	if (data) {
		g_hash_table_remove (view->pixmap_cache, key);
		g_object_unref (G_OBJECT (data));
		replaced = TRUE;
	}
	if (pixbuf && GDK_IS_PIXBUF (pixbuf)) {
		gint width;
		gint height;

		width = gdk_pixbuf_get_width (pixbuf);
		height = gdk_pixbuf_get_height (pixbuf);
		if (width > GUTTER_PIXMAP || height > GUTTER_PIXMAP) {
			if (width > GUTTER_PIXMAP)
				width = GUTTER_PIXMAP;
			if (height > GUTTER_PIXMAP)
				height = GUTTER_PIXMAP;
			pixbuf = gdk_pixbuf_scale_simple (pixbuf, width, height,
							  GDK_INTERP_BILINEAR);
		}
		g_object_ref (G_OBJECT (pixbuf));
		g_hash_table_insert (view->pixmap_cache,
				     (gchar *) key,
				     (gpointer) pixbuf);
	}

	return replaced;
}

GdkPixbuf *
gtk_source_view_get_pixbuf (GtkSourceView *view,
			    const gchar   *key)
{
	return g_hash_table_lookup (view->pixmap_cache, key);
}
*/
