/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8; coding: utf-8 -*-
 *
 * gtksourceprintcompositor.c
 * This file is part of GtkSourceView 
 *
 * Copyright (C) 2000, 2001 Chema Celorio  
 * Copyright (C) 2003  Gustavo Giráldez
 * Copyright (C) 2004  Red Hat, Inc.
 * Copyright (C) 2001-2007  Paolo Maggi
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
 
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <time.h>

#include "gtksourceview-i18n.h" 
#include "gtksourceprintcompositor.h"


#define ENABLE_DEBUG
#define ENABLE_PROFILE

/*
#undef ENABLE_DEBUG
#undef ENABLE_PROFILE
*/
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

#define DEFAULT_TAB_WIDTH 		8
#define MAX_TAB_WIDTH			32

#define DEFAULT_FONT_NAME   "Monospace 10"

/* 5 mm */
#define NUMBERS_TEXT_SEPARATION convert_from_mm (5, GTK_UNIT_POINTS)

#define HEADER_FOOTER_SIZE_FACTOR 2.5

typedef enum 
{
	/* Initial state: properties can be changed only when the paginator
	   is in the INIT state */
	INIT,
	
	/* Paginating state: paginator goes in this state when the paginate
	   function is called for the first time */
	PAGINATING,
	
	/* Done state: paginator goes in this state when the entire document
	   has been paginated */
	DONE
} PaginatorState;

struct _GtkSourcePrintCompositorPrivate
{
	GtkSourceBuffer         *buffer;

	/* Properties */
	guint			 tab_width;
	GtkWrapMode		 wrap_mode;
	gboolean                 highlight_syntax;
	guint                    print_line_numbers;
	
	PangoFontDescription    *body_font;
	PangoFontDescription    *line_numbers_font;
	PangoFontDescription    *header_font;
	PangoFontDescription    *footer_font;

	/* Page size, stored in pixels */
	gdouble                  page_width;
	gdouble                  page_height;

	/* These are stored in mm */
	gdouble                  margin_top;
	gdouble                  margin_bottom;
	gdouble                  margin_left;
	gdouble                  margin_right;

	gboolean                 print_header;
	gboolean                 print_footer;

	gchar                   *header_format_left;
	gchar                   *header_format_center;
	gchar                   *header_format_right;
	gboolean                 header_separator;
	gchar                   *footer_format_left;
	gchar                   *footer_format_center;
	gchar                   *footer_format_right;
	gboolean                 footer_separator;	

	/* State */
	PaginatorState           state;
	
	GArray                  *pages; /* pages[i] contains the begin offset
	                                   of i-th  */

	guint                    paginated_lines;
	gint                     n_pages;

	gdouble                  header_height;
	gdouble                  footer_height;
	gdouble                  line_numbers_width;

	/* printable (for the document itself) size */
	gdouble                  text_width;
	gdouble                  text_height;
	
	gdouble                  real_margin_top;
	gdouble                  real_margin_bottom;
	gdouble                  real_margin_left;
	gdouble                  real_margin_right;
	
	PangoLanguage           *language;	
};

enum
{
	PROP_0,
	PROP_BUFFER,
	PROP_TAB_WIDTH,
	PROP_WRAP_MODE,
	PROP_HIGHLIGHT_SYNTAX,
	PROP_PRINT_LINE_NUMBERS
};

G_DEFINE_TYPE (GtkSourcePrintCompositor, gtk_source_print_compositor, G_TYPE_OBJECT)

#define MM_PER_INCH 25.4
#define POINTS_PER_INCH 72

static gdouble
convert_to_mm (gdouble len, GtkUnit unit)
{
	switch (unit)
	{
		case GTK_UNIT_MM:
			return len;

		case GTK_UNIT_INCH:
			return len * MM_PER_INCH;
		
		default:
		case GTK_UNIT_PIXEL:
			g_warning ("Unsupported unit");
			/* Fall through */

		case GTK_UNIT_POINTS:
			return len * (MM_PER_INCH / POINTS_PER_INCH);
    	}
}

static gdouble
convert_from_mm (gdouble len, GtkUnit unit)
{
	switch (unit)
	{
		case GTK_UNIT_MM:
			return len;
		
		case GTK_UNIT_INCH:
			return len / MM_PER_INCH;
			
		default:
		case GTK_UNIT_PIXEL:
			g_warning ("Unsupported unit");
			/* Fall through */

		case GTK_UNIT_POINTS:
			return len / (MM_PER_INCH / POINTS_PER_INCH);
	}
}

static void 
gtk_source_print_compositor_get_property (GObject    *object,
					  guint       prop_id,
					  GValue     *value,
					  GParamSpec *pspec)
{
	GtkSourcePrintCompositor *compositor = GTK_SOURCE_PRINT_COMPOSITOR (object);

	switch (prop_id)
	{			
		case PROP_BUFFER:
			g_value_set_object (value, compositor->priv->buffer);
			break;
		case PROP_TAB_WIDTH:
			g_value_set_uint (value,
					  gtk_source_print_compositor_get_tab_width (compositor));
			break;
		case PROP_WRAP_MODE:
			g_value_set_enum (value,
					  gtk_source_print_compositor_get_wrap_mode (compositor));
			break;
		case PROP_HIGHLIGHT_SYNTAX:
			g_value_set_boolean (value,
					     gtk_source_print_compositor_get_highlight_syntax (compositor));
			break;
		case PROP_PRINT_LINE_NUMBERS:
			g_value_set_uint (value,
					  gtk_source_print_compositor_get_print_line_numbers (compositor));
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}

static void 
gtk_source_print_compositor_set_property (GObject      *object,
					  guint         prop_id,
					  const GValue *value,
					  GParamSpec   *pspec)
{
	GtkSourcePrintCompositor *compositor = GTK_SOURCE_PRINT_COMPOSITOR (object);

	g_debug ("gtk_source_print_compositor_set_property");
	
	switch (prop_id)
	{
		case PROP_BUFFER:
			compositor->priv->buffer = GTK_SOURCE_BUFFER (g_value_get_object (value));
			break;
		case PROP_TAB_WIDTH:
			gtk_source_print_compositor_set_tab_width (compositor,
								   g_value_get_uint (value));
			break;
		case PROP_WRAP_MODE:
			gtk_source_print_compositor_set_wrap_mode (compositor,
								   g_value_get_enum (value));
			break;
		case PROP_HIGHLIGHT_SYNTAX:
			gtk_source_print_compositor_set_highlight_syntax (compositor,
									  g_value_get_boolean (value));
			break;
		case PROP_PRINT_LINE_NUMBERS:
			gtk_source_print_compositor_set_print_line_numbers (compositor,
									    g_value_get_uint (value));
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}

static void
gtk_source_print_compositor_finalize (GObject *object)
{
	GtkSourcePrintCompositor *compositor;

	compositor = GTK_SOURCE_PRINT_COMPOSITOR (object);

	G_OBJECT_CLASS (gtk_source_print_compositor_parent_class)->finalize (object);
}

static void						 
gtk_source_print_compositor_class_init (GtkSourcePrintCompositorClass *klass)
{
	GObjectClass *object_class;
	
	object_class = G_OBJECT_CLASS (klass);

	object_class->get_property = gtk_source_print_compositor_get_property;
	object_class->set_property = gtk_source_print_compositor_set_property;
	object_class->finalize = gtk_source_print_compositor_finalize;

	/**
	 * GtkSourcePrintCompositor:tab-width:
	 *
	 * The GtkSourceBuffer object to print.
	 */
	g_object_class_install_property (object_class,
					 PROP_BUFFER,
					 g_param_spec_object ("buffer",
							      _("Source Buffer"),
							      _("The GtkSourceBuffer object to print"),
							      GTK_TYPE_SOURCE_BUFFER,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (object_class,
					 PROP_TAB_WIDTH,
					 g_param_spec_uint ("tab-width",
							    _("Tab Width"),
							    _("Width of a tab character expressed in spaces"),
							    1,
							    MAX_TAB_WIDTH,
							    DEFAULT_TAB_WIDTH,
							    G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_WRAP_MODE,
					 g_param_spec_enum ("wrap-mode",
							    _("Wrap Mode"),
							    _("Word wrapping mode"),
							    GTK_TYPE_WRAP_MODE,
							    GTK_WRAP_NONE,
							    G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_HIGHLIGHT_SYNTAX,
					 g_param_spec_boolean ("highlight-syntax",
							       _("Highlight Syntax"),
							       _("Whether to print the "
								 "document with highlighted "
								 "syntax"),
							       TRUE,
							       G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_PRINT_LINE_NUMBERS,
					 g_param_spec_uint ("print-line-numbers",
							    _("Print Line Numbers"),
							    _("Interval of printed line numbers "
							      "(0 means no numbers)"),
							    0, 100, 1,
							    G_PARAM_READWRITE));

	g_type_class_add_private (object_class, sizeof(GtkSourcePrintCompositorPrivate));	
}

static void
gtk_source_print_compositor_init (GtkSourcePrintCompositor *compositor)
{
	GtkSourcePrintCompositorPrivate *priv;

	g_debug ("gtk_source_print_compositor_init");
	
	priv = G_TYPE_INSTANCE_GET_PRIVATE (compositor, 
					    GTK_TYPE_SOURCE_PRINT_COMPOSITOR,
					    GtkSourcePrintCompositorPrivate);

	compositor->priv = priv;
	
	priv->buffer = NULL;
	
	priv->tab_width = DEFAULT_TAB_WIDTH;
	priv->wrap_mode = GTK_WRAP_NONE;
	priv->highlight_syntax = TRUE;
	priv->print_line_numbers = 0;

	priv->body_font = pango_font_description_from_string (DEFAULT_FONT_NAME);
	priv->line_numbers_font = NULL;
	priv->header_font = NULL;
	priv->footer_font = NULL;

	priv->page_width = 0.0;
	priv->page_height = 0.0;

	priv->margin_top = 0.0;
	priv->margin_bottom = 0.0;
	priv->margin_left = 0.0;
	priv->margin_right = 0.0;

	priv->print_header = FALSE;
	priv->print_footer = FALSE;

	priv->header_format_left = NULL;
	priv->header_format_center = NULL;
	priv->header_format_right = NULL;
	priv->header_separator = FALSE;
	
	priv->footer_format_left = NULL;
	priv->footer_format_center = NULL;
	priv->footer_format_right = NULL;
	priv->footer_separator = FALSE;
	
	priv->state = INIT;
	
	priv->pages = NULL;
	
	priv->paginated_lines = 0;
	priv->n_pages = -1;
	
	priv->language = gtk_get_default_language ();

	/* Negative values mean uninitialized */
	priv->header_height = -1.0; 
	priv->footer_height = -1.0;
	priv->line_numbers_width = -1.0;
	priv->text_width = -1.0;
	priv->text_height = -1.0;
}

/**
 * gtk_source_print_compositor_new:
 * @buffer: the #GtkSourceBuffer to print
 * 
 * Creates a new print compositor that can be used to print @buffer.
 * 
 * Return value: a new print compositor object.
 **/
GtkSourcePrintCompositor *
gtk_source_print_compositor_new (GtkSourceBuffer *buffer)
{
	g_return_val_if_fail (GTK_IS_SOURCE_BUFFER (buffer), NULL);
	
	return g_object_new (GTK_TYPE_SOURCE_PRINT_COMPOSITOR,
			     "buffer", buffer,
			     NULL);
}

/**
 * gtk_source_print_compositor_get_buffer:
 * @compositor: a #GtkSourcePrintCompositor.
 * 
 * Gets the #GtkSourceBuffer associated with the compositor. The returned
 * object reference is owned by the compositor object and
 * should not be unreferenced.
 * 
 * Return value: the #GtkSourceBuffer associated with the compositor.
 **/
GtkSourceBuffer *
gtk_source_print_compositor_get_buffer (GtkSourcePrintCompositor *compositor)
{
	g_return_val_if_fail (GTK_IS_SOURCE_PRINT_COMPOSITOR (compositor), NULL);
	
	return compositor->priv->buffer;
}

/**
 * gtk_source_print_compositor_setup_from_view:
 * @compositor: a #GtkSourcePrintCompositor.
 * @view: a #GtkSourceView to get configuration from.
 * 
 * Convenience function to set several configuration options at once,
 * so that the printed output matches @view.  The options set are
 * tab-width, highligh-syntax, wrap-mode and font-name.
 **/
void
gtk_source_print_compositor_setup_from_view (GtkSourcePrintCompositor *compositor,
					     GtkSourceView            *view)
{
	g_return_if_fail (GTK_IS_SOURCE_PRINT_COMPOSITOR (compositor));
	g_return_if_fail (GTK_IS_SOURCE_VIEW (view));
	g_return_if_fail (compositor->priv->state == INIT);	
	/* TODO */
}

/**
 * gtk_source_print_compositor_set_tab_width:
 * @compositor: a #GtkSourcePrintCompositor.
 * @width: width of tab in characters.
 *
 * Sets the width of tabulation in characters for printed text. 
 *
 * This function cannot be called anymore after the first call to the 
 * "paginate" function.
 */
void
gtk_source_print_compositor_set_tab_width (GtkSourcePrintCompositor *compositor,
					   guint                     width)
{
	g_return_if_fail (GTK_IS_SOURCE_PRINT_COMPOSITOR (compositor));
	g_return_if_fail (width > 0 && width <= MAX_TAB_WIDTH);
	g_return_if_fail (compositor->priv->state == INIT);
	
	if (width == compositor->priv->tab_width)
		return;
	
	compositor->priv->tab_width = width;

	g_object_notify (G_OBJECT (compositor), "tab-width");
}

/**
 * gtk_source_print_compositor_get_tab_width:
 * @compositor: a #GtkSourcePrintCompositor.
 *
 * Returns the width of tabulation in characters for printed text.
 *
 * Return value: width of tab.
 */
guint
gtk_source_print_compositor_get_tab_width (GtkSourcePrintCompositor *compositor)
{
	g_return_val_if_fail (GTK_IS_SOURCE_PRINT_COMPOSITOR (compositor), DEFAULT_TAB_WIDTH);
	
	return compositor->priv->tab_width;
}

/**
 * gtk_source_print_compositor_set_wrap_mode:
 * @compositor: a #GtkSourcePrintCompositor.
 * @wrap_mode: a #GtkWrapMode.
 *
 * Sets the line wrapping mode for the printed text.
 *
 * This function cannot be called anymore after the first call to the 
 * "paginate" function.  
 */
void
gtk_source_print_compositor_set_wrap_mode (GtkSourcePrintCompositor *compositor,
					   GtkWrapMode               wrap_mode)
{
	g_return_if_fail (GTK_IS_SOURCE_PRINT_COMPOSITOR (compositor));
	g_return_if_fail (compositor->priv->state == INIT);

	if (wrap_mode == compositor->priv->wrap_mode)
		return;
	
	compositor->priv->wrap_mode = wrap_mode;

	g_object_notify (G_OBJECT (compositor), "wrap-mode");
}

/**
 * gtk_source_print_compositor_get_wrap_mode:
 * @compositor: a #GtkSourcePrintCompositor.
 *
 * Gets the line wrapping mode for the printed text.
 *
 * Return value: the line wrap mode.
 */
GtkWrapMode
gtk_source_print_compositor_get_wrap_mode (GtkSourcePrintCompositor *compositor)
{
	g_return_val_if_fail (GTK_IS_SOURCE_PRINT_COMPOSITOR (compositor), GTK_WRAP_NONE);
	
	return compositor->priv->wrap_mode;
}

/**
 * gtk_source_print_compositor_set_highlight_syntax:
 * @compositor: a #GtkSourcePrintCompositor.
 * @highlight: whether syntax should be highlighted.
 * 
 * Sets whether the printed text will be highlighted according to the
 * buffer rules.  Both color and font style are applied.
 **/
void
gtk_source_print_compositor_set_highlight_syntax (GtkSourcePrintCompositor *compositor,
						  gboolean                  highlight)
{
	g_return_if_fail (GTK_IS_SOURCE_PRINT_COMPOSITOR (compositor));
	g_return_if_fail (compositor->priv->state == INIT);

	if (highlight == compositor->priv->highlight_syntax)
		return;

	compositor->priv->highlight_syntax = highlight;

	g_object_notify (G_OBJECT (compositor), "highlight-syntax");
}

/**
 * gtk_source_print_compositor_get_highlight_syntax:
 * @compositor: a #GtkSourcePrintCompositor.
 *
 * Determines whether the printed text will be highlighted according to the
 * buffer rules.  Note that highlighting will happen
 * only if the buffer to print has highlighting activated.
 * 
 * Return value: %TRUE if the printed output will be highlighted.
 **/
gboolean
gtk_source_print_compositor_get_highlight_syntax (GtkSourcePrintCompositor *compositor)
{
	g_return_val_if_fail (GTK_IS_SOURCE_PRINT_COMPOSITOR (compositor), 0);

	return compositor->priv->highlight_syntax;
}

/**
 * gtk_source_print_compositor_set_print_line_numbers:
 * @compositor: a #GtkSourcePrintCompositor.
 * @interval: interval for printed line numbers.
 * 
 * Sets the interval for printed line numbers.  If @interval is 0 no
 * numbers will be printed.  If greater than 0, a number will be
 * printed every @interval lines (i.e. 1 will print all line numbers).
 **/
void
gtk_source_print_compositor_set_print_line_numbers (GtkSourcePrintCompositor *compositor,
						    guint                     interval)
{
	g_return_if_fail (GTK_IS_SOURCE_PRINT_COMPOSITOR (compositor));
	g_return_if_fail (compositor->priv->state == INIT);
	
	if (interval == compositor->priv->print_line_numbers)
		return;
	
	compositor->priv->print_line_numbers = interval;

	g_object_notify (G_OBJECT (compositor), "print-line-numbers");
}

/**
 * gtk_source_print_compositor_get_print_line_numbers:
 * @compositor: a #GtkSourcePrintCompositor.
 *
 * Returns the interval used for line number printing.  If the
 * value is 0, no line numbers will be printed.  The default value is
 * 1 (i.e. numbers printed in all lines).
 * 
 * Return value: the interval of printed line numbers.
 **/
guint
gtk_source_print_compositor_get_print_line_numbers (GtkSourcePrintCompositor *compositor)
{
	g_return_val_if_fail (GTK_IS_SOURCE_PRINT_COMPOSITOR (compositor), 0);
	
	return compositor->priv->print_line_numbers;
}

/**
 * gtk_source_print_compositor_get_n_pages:
 * @compositor: a #GtkSourcePrintCompositor.
 * 
 * Returns the number of pages in the document or <code>-1</code> if the 
 * document has not been completely paginated.
 *
 * Return value: the number of pages in the document or <code>-1</code> if the 
 * document has not been completely paginated.
 */
gint
gtk_source_print_compositor_get_n_pages	(GtkSourcePrintCompositor *compositor)
{
	g_return_val_if_fail (GTK_IS_SOURCE_PRINT_COMPOSITOR (compositor), -1);
	
	if (compositor->priv->state != DONE)
		return -1;
		
	return compositor->priv->n_pages;
}

static gsize
get_n_digits (guint n)
{
	gsize d = 1;

	while (n /= 10)
		d++;

	return d;
}

static void
calculate_line_numbers_width (GtkSourcePrintCompositor *compositor,
			      GtkPrintContext          *context)
{
	gint line_count;
	gdouble digit_width;
	PangoContext *pango_context;
	PangoFontMetrics* font_metrics;
	
	if (compositor->priv->print_line_numbers == 0)
	{
		compositor->priv->line_numbers_width = 0.0;	

		DEBUG ({
			g_debug ("line_numbers_width: %f mm", compositor->priv->line_numbers_width);
		});
		
		return;
	}

	if (compositor->priv->line_numbers_font == NULL)
		compositor->priv->line_numbers_font = pango_font_description_copy_static (compositor->priv->body_font);
	
	pango_context = gtk_print_context_create_pango_context (context);
	pango_context_set_font_description (pango_context, compositor->priv->line_numbers_font);
	
	font_metrics = pango_context_get_metrics (pango_context,
						  compositor->priv->line_numbers_font,
						  compositor->priv->language);

	digit_width = (gdouble) pango_font_metrics_get_approximate_digit_width (font_metrics) / PANGO_SCALE;
	
	pango_font_metrics_unref (font_metrics);
	g_object_unref (pango_context);
	
	line_count = gtk_text_buffer_get_line_count (GTK_TEXT_BUFFER (compositor->priv->buffer));

	compositor->priv->line_numbers_width = digit_width * get_n_digits (line_count) + NUMBERS_TEXT_SEPARATION;
	
	DEBUG ({
		g_debug ("line_numbers_width: %f mm", convert_to_mm (compositor->priv->line_numbers_width, GTK_UNIT_POINTS));
	});
}

static gdouble
calculate_header_footer_height (GtkSourcePrintCompositor *compositor,
		                GtkPrintContext          *context,
		                PangoFontDescription     *font)
{
	PangoContext *pango_context;
	PangoFontMetrics* font_metrics;
	gdouble ascent;
	gdouble descent;
	
	pango_context = gtk_print_context_create_pango_context (context);
	pango_context_set_font_description (pango_context, font);
	
	font_metrics = pango_context_get_metrics (pango_context,
						  font,
						  compositor->priv->language);

	ascent = (gdouble) pango_font_metrics_get_ascent (font_metrics) / PANGO_SCALE;
	descent = (gdouble) pango_font_metrics_get_descent (font_metrics) / PANGO_SCALE;

	pango_font_metrics_unref (font_metrics);
	g_object_unref (pango_context);
		
	return HEADER_FOOTER_SIZE_FACTOR * (ascent + descent);
	
}

static void
calculate_header_height (GtkSourcePrintCompositor *compositor,
		         GtkPrintContext          *context)
{
#if 0
	if (!compositor->priv->print_header ||
	    (compositor->priv->header_format_left == NULL &&
	     compositor->priv->header_format_center == NULL &&
	     compositor->priv->header_format_right == NULL))
	{
		compositor->priv->header_height = 0.0;
	
		DEBUG ({
			g_debug ("header_height: %f mm", compositor->priv->header_height);
		});
		return;
	}
#endif	
	if (compositor->priv->header_font == NULL)
		compositor->priv->header_font = pango_font_description_copy_static (compositor->priv->body_font);
	
	compositor->priv->header_height = calculate_header_footer_height (compositor,
									  context,
									  compositor->priv->header_font);

	DEBUG ({
		g_debug ("header_height: %f mm", convert_to_mm (compositor->priv->header_height, GTK_UNIT_POINTS));
	});									  
}
			         
static void
calculate_footer_height (GtkSourcePrintCompositor *compositor,
		         GtkPrintContext          *context)
{
#if 0
	if (!compositor->priv->print_footer ||
	    (compositor->priv->footer_format_left == NULL &&
	     compositor->priv->footer_format_center == NULL &&
	     compositor->priv->footer_format_right == NULL))
	{
		compositor->priv->footer_height = 0.0;

		DEBUG ({
			g_debug ("footer_height: %f mm", compositor->priv->footer_height);
		});
		
		return;
	}
#endif	
	if (compositor->priv->footer_font == NULL)
		compositor->priv->footer_font = pango_font_description_copy_static (compositor->priv->body_font);
	
	compositor->priv->footer_height = calculate_header_footer_height (compositor,
									  context,
									  compositor->priv->footer_font);
									  
	DEBUG ({
		g_debug ("footer_height: %f", convert_to_mm (compositor->priv->footer_height, GTK_UNIT_POINTS));
	});
}

static void
calculate_page_size_and_margins (GtkSourcePrintCompositor *compositor,
			         GtkPrintContext          *context)
{
	GtkPageSetup *page_setup;

	/* calculate_line_numbers_width and calculate_header_footer_height 
	   functions must be called before calculate_page_size_and_margins */	
	g_return_if_fail (compositor->priv->line_numbers_width >= 0.0);
	g_return_if_fail (compositor->priv->header_height >= 0.0);
	g_return_if_fail (compositor->priv->footer_height >= 0.0);			
	
	page_setup = gtk_print_context_get_page_setup (context);
	
	/* Calculate real margins: the margins specified in the GtkPageSetup object are the "print margins".
	   they are used to determine the minimal size for the layout margins. */
	compositor->priv->real_margin_top = MAX (gtk_page_setup_get_top_margin (page_setup, GTK_UNIT_MM),
						 compositor->priv->margin_top);
	compositor->priv->real_margin_bottom = MAX (gtk_page_setup_get_bottom_margin (page_setup, GTK_UNIT_MM),
						 compositor->priv->margin_bottom);
	compositor->priv->real_margin_left = MAX (gtk_page_setup_get_left_margin (page_setup, GTK_UNIT_MM),
						 compositor->priv->margin_left);
	compositor->priv->real_margin_right = MAX (gtk_page_setup_get_right_margin (page_setup, GTK_UNIT_MM),
						 compositor->priv->margin_right);

	DEBUG ({
		g_debug ("real_margin_top: %f mm", compositor->priv->real_margin_top);
		g_debug ("real_margin_bottom: %f  mm", compositor->priv->real_margin_bottom);
		g_debug ("real_margin_left: %f mm", compositor->priv->real_margin_left);
		g_debug ("real_margin_righ: %f mm", compositor->priv->real_margin_right);
	});

	compositor->priv->page_width = gtk_print_context_get_width (context);
	compositor->priv->page_height = gtk_print_context_get_height (context);

#if 0					 
	job->priv->text_width = (job->priv->page_width -
				 job->priv->doc_margin_left - job->priv->doc_margin_right -
				 job->priv->margin_left - job->priv->margin_right -
				 job->priv->numbers_width);
	
	job->priv->text_height = (job->priv->page_height -
				  job->priv->doc_margin_top - job->priv->doc_margin_bottom -
				  job->priv->margin_top - job->priv->margin_bottom -
				  job->priv->header_height - job->priv->footer_height);

	/* FIXME: put some saner values than 5cm - Gustavo */
	g_return_val_if_fail (job->priv->text_width > CM(5.0), FALSE);
	g_return_val_if_fail (job->priv->text_height > CM(5.0), FALSE);
#endif							 
}

static gboolean
ignore_tag (GtkSourcePrintCompositor *compositor,
            GtkTextTag               *tag)
{
	/* TODO: ignore bracket match tags etc */

	return FALSE;
}

static GSList *
get_iter_attrs (GtkSourcePrintCompositor *compositor,
		GtkTextIter              *iter,
		GtkTextIter              *limit)
{
	GSList *attrs = NULL;
	GSList *tags;
	PangoAttribute *bg = NULL, *fg = NULL, *style = NULL, *ul = NULL;
	PangoAttribute *weight = NULL, *st = NULL;

	tags = gtk_text_iter_get_tags (iter);
	gtk_text_iter_forward_to_tag_toggle (iter, NULL);

	if (gtk_text_iter_compare (iter, limit) > 0)
		*iter = *limit;

	while (tags)
	{
		GtkTextTag *tag;
		gboolean bg_set, fg_set, style_set, ul_set, weight_set, st_set;

		tag = tags->data;
		tags = g_slist_delete_link (tags, tags);

		if (ignore_tag (compositor, tag))
			continue;

		g_object_get (tag,
			     "background-set", &bg_set,
			     "foreground-set", &fg_set,
			     "style-set", &style_set,
			     "underline-set", &ul_set,
			     "weight-set", &weight_set,
			     "strikethrough-set", &st_set,
			     NULL);

		if (bg_set)
		{
			GdkColor *color = NULL;
			if (bg) pango_attribute_destroy (bg);
			g_object_get (tag, "background-gdk", &color, NULL);
			bg = pango_attr_background_new (color->red, color->green, color->blue);
			gdk_color_free (color);
		}

		if (fg_set)
		{
			GdkColor *color = NULL;
			if (fg) pango_attribute_destroy (fg);
			g_object_get (tag, "foreground-gdk", &color, NULL);
			fg = pango_attr_foreground_new (color->red, color->green, color->blue);
			gdk_color_free (color);
		}

		if (style_set)
		{
			PangoStyle style_value;
			if (style) pango_attribute_destroy (style);
			g_object_get (tag, "style", &style_value, NULL);
			style = pango_attr_style_new (style_value);
		}

		if (ul_set)
		{
			PangoUnderline underline;
			if (ul) pango_attribute_destroy (ul);
			g_object_get (tag, "underline", &underline, NULL);
			ul = pango_attr_underline_new (underline);
		}

		if (weight_set)
		{
			PangoWeight weight_value;
			if (weight) pango_attribute_destroy (weight);
			g_object_get (tag, "weight", &weight_value, NULL);
			weight = pango_attr_weight_new (weight_value);
		}

		if (st_set)
		{
			gboolean strikethrough;
			if (st) pango_attribute_destroy (st);
			g_object_get (tag, "strikethrough", &strikethrough, NULL);
			st = pango_attr_strikethrough_new (strikethrough);
		}
	}

	if (bg)
		attrs = g_slist_prepend (attrs, bg);
	if (fg)
		attrs = g_slist_prepend (attrs, fg);
	if (style)
		attrs = g_slist_prepend (attrs, style);
	if (ul)
		attrs = g_slist_prepend (attrs, ul);
	if (weight)
		attrs = g_slist_prepend (attrs, weight);
	if (st)
		attrs = g_slist_prepend (attrs, st);

	return attrs;
}

static void
layout_paragraph (GtkSourcePrintCompositor *compositor,
		  PangoLayout              *layout,
		  GtkTextIter              *start,
		  GtkTextIter              *end)
{
	gchar *text;

	text = gtk_text_iter_get_slice (start, end);
	pango_layout_set_text (layout, text, -1);
	g_free (text);

	if (compositor->priv->highlight_syntax)
	{
		PangoAttrList *attr_list = NULL;
		GtkTextIter segm_start, segm_end;
		int start_index;

		/* Make sure it is highlighted even if it was not shown yet */
		gtk_source_buffer_ensure_highlight (compositor->priv->buffer,
						    start,
						    end);

		segm_start = *start;
		start_index = gtk_text_iter_get_line_index (start);

		while (gtk_text_iter_compare (&segm_start, end) < 0)
		{
			GSList *attrs;
			int si, ei;

			segm_end = segm_start;
			attrs = get_iter_attrs (compositor, &segm_end, end);
			if (attrs)
			{
				si = gtk_text_iter_get_line_index (&segm_start) - start_index;
				ei = gtk_text_iter_get_line_index (&segm_end) - start_index;
			}

			while (attrs)
			{
				PangoAttribute *a = attrs->data;

				a->start_index = si;
				a->end_index = ei;

				if (!attr_list)
					attr_list = pango_attr_list_new ();

				pango_attr_list_insert (attr_list, a);

				attrs = g_slist_delete_link (attrs, attrs);
			}

			segm_start = segm_end;
		}

		pango_layout_set_attributes (layout, attr_list);

		if (attr_list)
			pango_attr_list_unref (attr_list);
	}
}

static void
layout_line_number (GtkSourcePrintCompositor *compositor,
		    PangoLayout              *layout,
		    gint                      line_number)
{
	gchar *str;

	str = g_strdup_printf ("%d", line_number + 1);
	pango_layout_set_text (layout, str, -1);
	g_free (str);

	// TODO: baseline etc
}

static void
get_layout_size (PangoLayout *layout,
                 double      *width,
                 double      *height)
{
	PangoRectangle rect;

	pango_layout_get_extents (layout, NULL, &rect);

	if (width)
		*width = (double) rect.width / (double) PANGO_SCALE;

	if (height)
		*height = (double) rect.height / (double) PANGO_SCALE;
}

/* Returns TRUE if the document has been completely paginated. otherwise FALSE. 
   It paginates the document in small chunks and so it must be called multiple times to paginate the entire
   document. It has been designed to be called in the ::paginate handler. If you don't need to do pagination in 
   chunks, you can simply do it all in the ::begin-print handler. If you want
   to use the ::paginate signal to perform pagination in async way, it is suggested to
   ensure the buffer is not modified until pagination terminates. */
gboolean
gtk_source_print_compositor_paginate (GtkSourcePrintCompositor *compositor,
				      GtkPrintContext          *context)
{
	GtkTextIter start, end;
	PangoLayout *layout;
	gint offset;
	double page_height;

	g_return_val_if_fail (GTK_IS_SOURCE_PRINT_COMPOSITOR (compositor), TRUE);
	g_return_val_if_fail (GTK_IS_PRINT_CONTEXT (context), TRUE);

	if (compositor->priv->state == DONE)
		return TRUE;

	if (compositor->priv->state == INIT)
	{
		g_return_val_if_fail (compositor->priv->pages == NULL, TRUE);
		
		compositor->priv->pages = g_array_new (FALSE, FALSE, sizeof (gint));

		calculate_line_numbers_width (compositor, context);
		calculate_footer_height (compositor, context);
		calculate_header_height (compositor, context);
		calculate_page_size_and_margins (compositor, context);

		compositor->priv->state = PAGINATING;
	}

	g_return_val_if_fail (compositor->priv->state == PAGINATING, FALSE);

	/* TODO: paginate just a chunk of text to
	   - allow async pagination
	   - allow to paginate/print just a part of the text

#define PAGINATION_CHUNK_LINES 1000

	gtk_text_buffer_get_iter_at_line (GTK_TEXT_BUFFER (compositor->priv->buffer),
					  &start,
					  compositor->priv->paginated_lines);

	end = start;
	gtk_text_iter_forward_lines (&end, PAGINATION_CHUNK_LINES);
	 */

	gtk_text_buffer_get_start_iter (GTK_TEXT_BUFFER (compositor->priv->buffer), &start);
	gtk_text_buffer_get_end_iter (GTK_TEXT_BUFFER (compositor->priv->buffer), &end);

	/* add the first page start */
	offset = gtk_text_iter_get_offset (&start);
	g_array_append_val (compositor->priv->pages, offset);

	/* TODO: put the layout in priv to create it just once when paginating incrementally */
	layout = gtk_print_context_create_pango_layout (context);

	page_height = 0;

	while (gtk_text_iter_compare (&start, &end) < 0)
	{
		GtkTextIter line_end;
		double line_height;

		line_end = start;
		if (!gtk_text_iter_ends_line (&line_end))
			gtk_text_iter_forward_to_line_end (&line_end);

		layout_paragraph (compositor, layout, &start, &line_end);

		get_layout_size (layout, NULL, &line_height);

//		if (line_number_displayed (op, line_no))
//			line_height = MAX (line_height, op->priv->ln_height);

#define EPS (.1)
		if (page_height > EPS &&
		    page_height + line_height > compositor->priv->page_height + EPS)
		{
			// TODO: wrap multilines

			offset = gtk_text_iter_get_offset (&start);
			g_array_append_val (compositor->priv->pages, offset);
			page_height = line_height;
		}
		else
		{
			page_height += line_height;
		}

		gtk_text_iter_forward_line (&start);
	}
#undef EPS

	g_object_unref (layout);

	{
		int i;
		g_print ("Pagination:\n");
		for (i = 0; i < compositor->priv->pages->len; i += 1)
		{
			gint offset;
			GtkTextIter iter;

			offset = g_array_index (compositor->priv->pages, int, i);
			gtk_text_buffer_get_iter_at_offset (GTK_TEXT_BUFFER (compositor->priv->buffer), &iter, offset);

			g_print ("  page %d starts at line %d (offset %d)\n", i, gtk_text_iter_get_line (&iter), offset); 
		}
	}

	compositor->priv->state = DONE;

	compositor->priv->n_pages = compositor->priv->pages->len;

	return FALSE;
}

gint
gtk_source_print_compositor_get_pagination_progress (GtkSourcePrintCompositor *compositor)
{
	g_return_val_if_fail (GTK_IS_SOURCE_PRINT_COMPOSITOR (compositor), -1);

	g_return_val_if_fail (compositor->priv->state == PAGINATING ||
			      compositor->priv->state == DONE, -1);

	return compositor->priv->n_pages;	
}

void
gtk_source_print_compositor_draw_page (GtkSourcePrintCompositor *compositor,
				       GtkPrintContext          *context,
				       gint                      page_nr)
{
	cairo_t *cr;
	PangoLayout *layout;
	PangoLayout *line_numbers_layout;
	GtkTextIter start, end;
	gint offset;
	double x, y;

	g_return_if_fail (GTK_IS_SOURCE_PRINT_COMPOSITOR (compositor));
	g_return_if_fail (GTK_IS_PRINT_CONTEXT (context));

	cr = gtk_print_context_get_cairo_context (context);
	cairo_set_source_rgb (cr, 0, 0, 0);

	x = compositor->priv->line_numbers_width;
	y = 0;

	/* TODO: put the layout in priv to create it just once */
	layout = gtk_print_context_create_pango_layout (context);
	pango_cairo_update_layout (cr, layout);

	line_numbers_layout = gtk_print_context_create_pango_layout (context);
	pango_cairo_update_layout (cr, line_numbers_layout);

	g_return_if_fail (compositor->priv->buffer != NULL);
	g_return_if_fail (compositor->priv->pages != NULL);
	g_return_if_fail (page_nr < compositor->priv->pages->len);

	offset = g_array_index (compositor->priv->pages, int, page_nr);
	gtk_text_buffer_get_iter_at_offset (GTK_TEXT_BUFFER (compositor->priv->buffer),
					    &start, offset);

	if (page_nr + 1 < compositor->priv->pages->len)
	{
		offset = g_array_index (compositor->priv->pages, int, page_nr + 1);
		gtk_text_buffer_get_iter_at_offset (GTK_TEXT_BUFFER (compositor->priv->buffer),
						    &end, offset);
	}
	else
	{
		gtk_text_buffer_get_end_iter (GTK_TEXT_BUFFER (compositor->priv->buffer),
					      &end);
	}

	while (gtk_text_iter_compare (&start, &end) < 0)
	{
		gint line_number;
		double line_number_height, line_height;

		line_number = gtk_text_iter_get_line (&start);

		/* print the line number if needed */
		if (compositor->priv->print_line_numbers > 0 &&
		    ((line_number % compositor->priv->print_line_numbers) == 0))
		{
			layout_line_number (compositor,
					    line_numbers_layout,
					    line_number);

			get_layout_size (layout, NULL, &line_number_height);

			cairo_move_to (cr, 0, y);
			pango_cairo_show_layout (cr, line_numbers_layout);
		}

		/* print the paragraph */
		if (gtk_text_iter_ends_line (&start))
		{
			pango_layout_set_text (layout, "", 0);
			pango_layout_set_attributes (layout, NULL);
		}
		else
		{
			GtkTextIter line_end;

			line_end = start;
			gtk_text_iter_forward_to_line_end (&line_end);

			if (gtk_text_iter_compare (&line_end, &end) > 0)
				line_end = end;

			layout_paragraph (compositor,
					  layout,
					  &start,
					  &line_end);
		}

		get_layout_size (layout, NULL, &line_height);
		line_height = MAX (line_height, line_number_height);

		cairo_move_to (cr, x, y);
		pango_cairo_show_layout (cr, layout);

		y += line_height;
		gtk_text_iter_forward_line (&start);
	}

	g_object_unref (layout);
	g_object_unref (line_numbers_layout);
}

