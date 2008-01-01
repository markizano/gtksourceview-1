/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8; coding: utf-8 -*-
 *
 * gtksourceprintcompositor.h
 * This file is part of GtkSourceView 
 *
 * Copyright (C) 2003  Gustavo Giráldez 
 * Copyright (C) 2007  Paolo Maggi
 *
 * GtkSourceView is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * GtkSourceView is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
 
#ifndef __GTK_SOURCE_PRINT_COMPOSITOR_H__
#define __GTK_SOURCE_PRINT_COMPOSITOR_H__

#include <gtk/gtk.h>
#include <gtksourceview/gtksourceview.h>

#define GTK_TYPE_SOURCE_PRINT_COMPOSITOR            (gtk_source_print_compositor_get_type ())
#define GTK_SOURCE_PRINT_COMPOSITOR(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GTK_TYPE_SOURCE_PRINT_COMPOSITOR, GtkSourcePrintCompositor))
#define GTK_SOURCE_PRINT_COMPOSITOR_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), GTK_TYPE_SOURCE_PRINT_COMPOSITOR, GtkSourcePrintCompositorClass))
#define GTK_IS_SOURCE_PRINT_COMPOSITOR(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GTK_TYPE_SOURCE_PRINT_COMPOSITOR))
#define GTK_IS_SOURCE_PRINT_COMPOSITOR_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GTK_TYPE_SOURCE_PRINT_COMPOSITOR))
#define GTK_SOURCE_PRINT_COMPOSITOR_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), GTK_TYPE_SOURCE_PRINT_COMPOSITOR, GtkSourcePrintCompositorClass))

typedef struct _GtkSourcePrintCompositor         GtkSourcePrintCompositor;
typedef struct _GtkSourcePrintCompositorClass    GtkSourcePrintCompositorClass;
typedef struct _GtkSourcePrintCompositorPrivate  GtkSourcePrintCompositorPrivate;

struct _GtkSourcePrintCompositor
{
	GObject parent_instance;

	GtkSourcePrintCompositorPrivate *priv;
};

struct _GtkSourcePrintCompositorClass
{
	GObjectClass parent_class;
};


GType			  gtk_source_print_compositor_get_type		(void) G_GNUC_CONST;

/* Constructor */
GtkSourcePrintCompositor *gtk_source_print_compositor_new		(GtkSourceBuffer         *buffer);

GtkSourceBuffer   	 *gtk_source_print_compositor_get_buffer	(GtkSourcePrintCompositor *compositor);

void			  gtk_source_print_compositor_setup_from_view	(GtkSourcePrintCompositor *compositor,
									 GtkSourceView            *view);

/* Properties */
void			  gtk_source_print_compositor_set_tab_width	(GtkSourcePrintCompositor *compositor,
									 guint                     width);
guint			  gtk_source_print_compositor_get_tab_width	(GtkSourcePrintCompositor *compositor);

void			  gtk_source_print_compositor_set_wrap_mode	(GtkSourcePrintCompositor *compositor,
									 GtkWrapMode               wrap_mode);
GtkWrapMode		  gtk_source_print_compositor_get_wrap_mode	(GtkSourcePrintCompositor *compositor);

void			  gtk_source_print_compositor_set_highlight_syntax 
									(GtkSourcePrintCompositor *compositor,
									 gboolean                  highlight);
gboolean		  gtk_source_print_compositor_get_highlight_syntax
									(GtkSourcePrintCompositor *compositor);

void			  gtk_source_print_compositor_set_print_line_numbers
									(GtkSourcePrintCompositor *compositor,
									 guint                     interval);
guint			  gtk_source_print_compositor_get_print_line_numbers
									(GtkSourcePrintCompositor *compositor);
#if 0
void			  gtk_source_print_compositor_set_body_font_name
									(GtkSourcePrintCompositor *compositor,
									 const gchar              *font_name);
const gchar		 *gtk_source_print_compositor_get_body_font_name
									(GtkSourcePrintCompositor *compositor);

void			  gtk_source_print_compositor_set_line_numbers_font_name
									(GtkSourcePrintCompositor *compositor,
									 const gchar              *font_name);
const gchar		 *gtk_source_print_compositor_get_line_numbers_font_name
									(GtkSourcePrintCompositor *compositor);

void			  gtk_source_print_compositor_set_header_font_name
									(GtkSourcePrintCompositor *compositor,
									 const gchar              *font_name);
const gchar		 *gtk_source_print_compositor_get_header_font_name
									(GtkSourcePrintCompositor *compositor);

void			  gtk_source_print_compositor_set_footer_font_name
									(GtkSourcePrintCompositor *compositor,
									 const gchar              *font_name);
const gchar		 *gtk_source_print_compositor_get_footer_font_name
									(GtkSourcePrintCompositor *compositor);

gdouble			  gtk_source_print_compositor_get_top_margin	(GtkSourcePrintCompositor *compositor,
									 GtkUnit                   unit);
void			  gtk_source_print_compositor_set_top_margin	(GtkSourcePrintCompositor *compositor,
									 gdouble                   margin,
									 GtkUnit                   unit);

gdouble			  gtk_source_print_compositor_get_bottom_margin	(GtkSourcePrintCompositor *compositor,
									 GtkUnit                   unit);
void			  gtk_source_print_compositor_set_bottom_margin	(GtkSourcePrintCompositor *compositor,
									 gdouble                   margin,
									 GtkUnit                   unit);

gdouble			  gtk_source_print_compositor_get_left_margin	(GtkSourcePrintCompositor *compositor,
									 GtkUnit                   unit);
void			  gtk_source_print_compositor_set_left_margin	(GtkSourcePrintCompositor *compositor,
									 gdouble                   margin,
									 GtkUnit                   unit);

gdouble			  gtk_source_print_compositor_get_right_margin	(GtkSourcePrintCompositor *compositor,
									 GtkUnit                   unit);
void			  gtk_source_print_compositor_set_right_margin	(GtkSourcePrintCompositor *compositor,
									 gdouble                   margin,
									 GtkUnit                   unit);

void			  gtk_source_print_compositor_set_print_header	(GtkSourcePrintCompositor *compositor,
									 gboolean                  print);
gboolean		  gtk_source_print_compositor_get_print_header	(GtkSourcePrintCompositor *compositor);

void			  gtk_source_print_compositor_set_print_footer	(GtkSourcePrintCompositor *compositor,
									 gboolean                  print);
gboolean		  gtk_source_print_compositor_get_print_footer	(GtkSourcePrintCompositor *compositor);

/* format strings are strftime like */
void			  gtk_source_print_compositor_set_header_format	(GtkSourcePrintCompositor *compositor,
									 gboolean                  separator
									 const gchar              *left,
									 const gchar              *center,
									 const gchar              *right);
/* TODO: add get_header_format */

void			  gtk_source_print_compositor_set_footer_format	(GtkSourcePrintCompositor *compositor,
									 gboolean                  separator
									 const gchar              *left,
									 const gchar              *center,
									 const gchar              *right);
/* TODO: add get_footer_format */
									  
/* TODO: set/get style scheme ??? */
/* TODO: print right margin hint??? */

#endif

/* Returns the number of pages. Returns -1 until the document has been completely paginated */
gint			  gtk_source_print_compositor_get_n_pages	(GtkSourcePrintCompositor *compositor);

/* Returns TRUE if the document has been completely paginated. otherwise FALSE. 
   It paginates the document in small chunks and so it must be called multiple times to paginate the entire
   document. It has been designed to be called in the ::paginate handler. If you don't need to do pagination in 
   chunks, you can simply do it all in the ::begin-print handler */
gboolean		  gtk_source_print_compositor_paginate		(GtkSourcePrintCompositor *compositor,
									 GtkPrintContext          *context);
/* Returns the number of already paginated lines */
gint			  gtk_source_print_compositor_get_pagination_progress
									(GtkSourcePrintCompositor *compositor);

void			  gtk_source_print_compositor_draw_page		(GtkSourcePrintCompositor *compositor,
									 GtkPrintContext          *context,
									 gint                      page_nr);

/* TODO: do we need to add other functions to support print previewing? */
G_END_DECLS

#endif /* __GTK_SOURCE_PRINT_COMPOSITOR_H__ */

 
 
