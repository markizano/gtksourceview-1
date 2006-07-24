/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8; coding: utf-8 -*-
 *  gtksourcestylescheme.c
 *
 *  Copyright (C) 2003 - Paolo Maggi <paolo.maggi@polito.it>
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

#include "gtksourceview-i18n.h"
#include "gtksourcestylescheme.h"
#include "gtksourceview.h"
#include "gtksourcelanguage-private.h"
#include <string.h>

#define STYLE_HAS_FOREGROUND(s) ((s) && ((s)->mask & GTK_SOURCE_STYLE_USE_FOREGROUND))
#define STYLE_HAS_BACKGROUND(s) ((s) && ((s)->mask & GTK_SOURCE_STYLE_USE_BACKGROUND))

#define STYLE_TEXT		"text"
#define STYLE_SELECTED		"text-selected"
#define STYLE_BRACKETS		"brackets"
#define STYLE_CURSOR		"cursor"
#define STYLE_CURRENT_LINE	"current-line"

enum {
	PROP_0,
	PROP_ID
};

struct _GtkSourceStyleSchemePrivate
{
	char *id;
	char *name;
	GtkSourceStyleScheme *parent;
	GHashTable *styles;
};

static GtkSourceStyleScheme *default_scheme;

G_DEFINE_TYPE (GtkSourceStyleScheme, gtk_source_style_scheme, G_TYPE_OBJECT)

static void
gtk_source_style_scheme_finalize (GObject *object)
{
	GtkSourceStyleScheme *scheme = GTK_SOURCE_STYLE_SCHEME (object);

	g_hash_table_destroy (scheme->priv->styles);
	g_free (scheme->priv->id);
	g_free (scheme->priv->name);

	if (scheme->priv->parent)
		g_object_unref (scheme->priv->parent);

	G_OBJECT_CLASS (gtk_source_style_scheme_parent_class)->finalize (object);
}

static void
gtk_source_style_scheme_set_property (GObject 	   *object,
				      guint 	    prop_id,
				      const GValue *value,
				      GParamSpec   *pspec)
{
	char *tmp;
	GtkSourceStyleScheme *scheme = GTK_SOURCE_STYLE_SCHEME (object);

	switch (prop_id)
	{
	    case PROP_ID:
		tmp = scheme->priv->id;
		scheme->priv->id = g_strdup (g_value_get_string (value));
		g_free (tmp);
		break;

	    default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gtk_source_style_scheme_get_property (GObject 	 *object,
				      guint 	  prop_id,
				      GValue 	 *value,
				      GParamSpec *pspec)
{
	GtkSourceStyleScheme *scheme = GTK_SOURCE_STYLE_SCHEME (object);

	switch (prop_id)
	{
	    case PROP_ID:
		    g_value_set_string (value, scheme->priv->id);
		    break;

	    default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gtk_source_style_scheme_class_init (GtkSourceStyleSchemeClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = gtk_source_style_scheme_finalize;
	object_class->set_property = gtk_source_style_scheme_set_property;
	object_class->get_property = gtk_source_style_scheme_get_property;

	g_object_class_install_property (object_class,
					 PROP_ID,
					 g_param_spec_string ("id",
						 	      _("Style scheme id"),
							      _("Style scheme id"),
							      NULL,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_type_class_add_private (object_class, sizeof (GtkSourceStyleSchemePrivate));
}

static void
gtk_source_style_scheme_init (GtkSourceStyleScheme *scheme)
{
	scheme->priv = G_TYPE_INSTANCE_GET_PRIVATE (scheme, GTK_TYPE_SOURCE_STYLE_SCHEME,
						    GtkSourceStyleSchemePrivate);
	scheme->priv->styles = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
						      (GDestroyNotify) gtk_source_style_free);
}

const gchar *
gtk_source_style_scheme_get_id (GtkSourceStyleScheme *scheme)
{
	g_return_val_if_fail (GTK_IS_SOURCE_STYLE_SCHEME (scheme), NULL);
	g_return_val_if_fail (scheme->priv->id != NULL, "");
	return scheme->priv->id;
}

const gchar *
gtk_source_style_scheme_get_name (GtkSourceStyleScheme *scheme)
{
	g_return_val_if_fail (GTK_IS_SOURCE_STYLE_SCHEME (scheme), NULL);
	g_return_val_if_fail (scheme->priv->name != NULL, "");
	return _(scheme->priv->name);
}

GtkSourceStyleScheme *
gtk_source_style_scheme_new (const gchar *id)
{
	GtkSourceStyleScheme *scheme;

	g_return_val_if_fail (id != NULL, NULL);

	scheme = g_object_new (GTK_TYPE_SOURCE_STYLE_SCHEME, "id", id, NULL);

	return scheme;
}

GtkSourceStyle *
gtk_source_style_scheme_get_style (GtkSourceStyleScheme *scheme,
				   const gchar          *style_name)
{
	GtkSourceStyle *style = NULL;

	g_return_val_if_fail (GTK_IS_SOURCE_STYLE_SCHEME (scheme), NULL);
	g_return_val_if_fail (style_name != NULL, NULL);

	style = g_hash_table_lookup (scheme->priv->styles, style_name);

	if (style)
		return gtk_source_style_copy (style);

	if (scheme->priv->parent)
		return gtk_source_style_scheme_get_style (scheme->priv->parent,
							  style_name);
	else
		return NULL;
}

GtkSourceStyle *
gtk_source_style_scheme_get_matching_brackets_style (GtkSourceStyleScheme *scheme)
{
	g_return_val_if_fail (GTK_IS_SOURCE_STYLE_SCHEME (scheme), NULL);
	return gtk_source_style_scheme_get_style (scheme, STYLE_BRACKETS);
}

gboolean
gtk_source_style_scheme_get_current_line_color (GtkSourceStyleScheme *scheme,
						GdkColor             *color)
{
	GtkSourceStyle *style;
	gboolean ret = FALSE;

	g_return_val_if_fail (GTK_IS_SOURCE_STYLE_SCHEME (scheme), FALSE);
	g_return_val_if_fail (color != NULL, FALSE);

	style = gtk_source_style_scheme_get_style (scheme, STYLE_CURRENT_LINE);

	if (STYLE_HAS_FOREGROUND (style))
	{
		*color = style->foreground;
		ret = TRUE;
	}

	gtk_source_style_free (style);
	return ret;
}

static void
add_style (GtkSourceStyleScheme *scheme,
	   const char           *name,
	   GtkSourceStyleMask    mask,
	   const char           *foreground,
	   const char           *background,
	   gboolean              italic,
	   gboolean              bold,
	   gboolean              underline,
	   gboolean              strikethrough)
{
	GtkSourceStyle *style;

	style = gtk_source_style_new (mask);

	if (mask & GTK_SOURCE_STYLE_USE_FOREGROUND)
		gdk_color_parse (foreground, &style->foreground);
	if (mask & GTK_SOURCE_STYLE_USE_BACKGROUND)
		gdk_color_parse (background, &style->background);

	style->italic = italic;
	style->bold = bold;
	style->underline = underline;
	style->strikethrough = strikethrough;

	g_hash_table_insert (scheme->priv->styles, g_strdup (name), style);
}

GtkSourceStyleScheme *
_gtk_source_style_scheme_get_default (void)
{
	if (default_scheme)
		return g_object_ref (default_scheme);

	default_scheme = gtk_source_style_scheme_new ("default");
	g_object_add_weak_pointer (G_OBJECT (default_scheme),
				   (gpointer *) &default_scheme);

#define ADD_FORE(name,color)					\
	add_style (default_scheme, name,			\
		   GTK_SOURCE_STYLE_USE_FOREGROUND,		\
		   color, NULL, FALSE, FALSE, FALSE, FALSE)
#define ADD_FORE_BOLD(name,color)				\
	add_style (default_scheme, name,			\
		   GTK_SOURCE_STYLE_USE_FOREGROUND |		\
			GTK_SOURCE_STYLE_USE_BOLD,		\
		   color, NULL, FALSE, TRUE, FALSE, FALSE)

	ADD_FORE ("def:base-n-integer", "#FF00FF");
	ADD_FORE ("def:character", "#FF00FF");
	ADD_FORE ("def:comment", "#0000FF");
	ADD_FORE_BOLD ("def:data-type", "#2E8B57");
	ADD_FORE ("def:function", "#008A8C");
	ADD_FORE ("def:decimal", "#FF00FF");
	ADD_FORE ("def:floating-point", "#FF00FF");
	ADD_FORE_BOLD ("def:keyword", "#A52A2A");
	ADD_FORE ("def:preprocessor", "#A020F0");
	ADD_FORE ("def:string", "#FF00FF");

	add_style (default_scheme, "def:specials",
		   GTK_SOURCE_STYLE_USE_FOREGROUND |
			GTK_SOURCE_STYLE_USE_BACKGROUND,
		   "#FFFFFF", "#FF0000", FALSE, FALSE, FALSE, FALSE);

	ADD_FORE_BOLD ("def:others", "#2E8B57");
	ADD_FORE ("def:others2", "#008B8B");
	ADD_FORE ("def:others3", "#6A5ACD");

	add_style (default_scheme, "def:net-address",
		   GTK_SOURCE_STYLE_USE_FOREGROUND |
			GTK_SOURCE_STYLE_USE_ITALIC |
			GTK_SOURCE_STYLE_USE_UNDERLINE,
		   "#0000FF", NULL, TRUE, FALSE, TRUE, FALSE);

	add_style (default_scheme, "def:note",
		   GTK_SOURCE_STYLE_USE_FOREGROUND |
			GTK_SOURCE_STYLE_USE_BACKGROUND |
			GTK_SOURCE_STYLE_USE_BOLD,
		   "#0000FF", "#FFFF00", FALSE, TRUE, FALSE, FALSE);

	add_style (default_scheme, "def:error",
		   GTK_SOURCE_STYLE_USE_BACKGROUND |
			GTK_SOURCE_STYLE_USE_BOLD,
		   NULL, "#FF0000", FALSE, TRUE, FALSE, FALSE);

	ADD_FORE_BOLD ("def:package", "#FF00FF");
	ADD_FORE ("def:escape", "#9010D0");

	add_style (default_scheme, STYLE_BRACKETS,
		   GTK_SOURCE_STYLE_USE_BACKGROUND |
			GTK_SOURCE_STYLE_USE_FOREGROUND |
			GTK_SOURCE_STYLE_USE_BOLD,
		   "white", "gray", FALSE, TRUE, FALSE, FALSE);

#undef ADD_FORE
#undef ADD_FORE_BOLD

	return default_scheme;
}

static void
set_text_style (GtkWidget      *widget,
		GtkSourceStyle *style,
		GtkStateType    state)
{
	GdkColor *color;

	if (STYLE_HAS_BACKGROUND (style))
		color = &style->background;
	else
		color = NULL;

	gtk_widget_modify_base (widget, state, color);

	if (STYLE_HAS_FOREGROUND (style))
		color = &style->foreground;
	else
		color = NULL;

	gtk_widget_modify_text (widget, state, color);
}

static void
set_cursor_color (GtkWidget      *widget,
		  GtkSourceStyle *style)
{
	GdkColor *color;

	if (STYLE_HAS_FOREGROUND (style))
		color = &style->foreground;
	else
		color = &widget->style->text[GTK_STATE_NORMAL];

	g_print ("implement me\n");
}

void
_gtk_source_style_scheme_apply (GtkSourceStyleScheme *scheme,
				GtkWidget            *widget)
{
	GtkSourceStyle *style;

	g_return_if_fail (GTK_IS_SOURCE_STYLE_SCHEME (scheme));
	g_return_if_fail (GTK_IS_WIDGET (widget));

	gtk_widget_ensure_style (widget);

	style = gtk_source_style_scheme_get_style (scheme, STYLE_TEXT);
	set_text_style (widget, style, GTK_STATE_NORMAL);
	set_text_style (widget, style, GTK_STATE_ACTIVE);
	set_text_style (widget, style, GTK_STATE_PRELIGHT);
	set_text_style (widget, style, GTK_STATE_INSENSITIVE);
	gtk_source_style_free (style);

	style = gtk_source_style_scheme_get_style (scheme, STYLE_SELECTED);
	set_text_style (widget, style, GTK_STATE_SELECTED);
	gtk_source_style_free (style);

	style = gtk_source_style_scheme_get_style (scheme, STYLE_CURSOR);
	set_cursor_color (widget, style);
	gtk_source_style_free (style);
}

/* --- PARSER ---------------------------------------------------------------- */

typedef struct {
	GtkSourceStyleScheme *scheme;
	gboolean done;
} ParserData;

#define ERROR_QUARK (g_quark_from_static_string ("gtk-source-style-scheme-parser-error"))

static gboolean
str_to_bool (const gchar *string)
{
	return !g_ascii_strcasecmp (string, "true") ||
		!g_ascii_strcasecmp (string, "yes") ||
		!g_ascii_strcasecmp (string, "1");
}

static gboolean
parse_style (const gchar     *element_name,
	     const gchar    **names,
	     const gchar    **values,
	     gchar          **style_name_p,
	     GtkSourceStyle **style_p,
	     GError         **error)
{
	gchar *fg = NULL, *bg = NULL;
	GtkSourceStyle *style;
	char *style_name = NULL;

	style = gtk_source_style_new (0);

	for (; names && *names; names++, values++)
	{
		if (!strcmp (*names, "name"))
		{
			g_free (style_name);
			style_name = g_strdup (*values);
		}
		else if (!strcmp (*names, "foreground"))
		{
			g_free (fg);
			fg = g_strdup (*values);
		}
		else if (!strcmp (*names, "background"))
		{
			g_free (bg);
			bg = g_strdup (*values);
		}
		else if (!strcmp (*names, "italic"))
		{
			style->mask |= GTK_SOURCE_STYLE_USE_ITALIC;
			style->italic = str_to_bool (*values);
		}
		else if (!strcmp (*names, "bold"))
		{
			style->mask |= GTK_SOURCE_STYLE_USE_BOLD;
			style->bold = str_to_bool (*values);
		}
		else if (!strcmp (*names, "strikethrough"))
		{
			style->mask |= GTK_SOURCE_STYLE_USE_STRIKETHROUGH;
			style->strikethrough = str_to_bool (*values);
		}
		else if (!strcmp (*names, "underline"))
		{
			style->mask |= GTK_SOURCE_STYLE_USE_UNDERLINE;
			style->underline = str_to_bool (*values);
		}
		else
		{
			g_set_error (error, ERROR_QUARK, 0,
				     "unexpected attribute '%s' in element '%s'",
				     *names, element_name);
			gtk_source_style_free (style);
			g_free (style_name);
			return FALSE;
		}
	}

	if (!style_name)
	{
		g_set_error (error, ERROR_QUARK, 0,
			     "'name' attribute missing");
		gtk_source_style_free (style);
		g_free (style_name);
		return FALSE;
	}

	if (fg)
	{
		if (gdk_color_parse (fg, &style->foreground))
			style->mask |= GTK_SOURCE_STYLE_USE_FOREGROUND;
		else
			g_warning ("invalid color '%s'", fg);
	}

	if (bg)
	{
		if (gdk_color_parse (bg, &style->background))
			style->mask |= GTK_SOURCE_STYLE_USE_BACKGROUND;
		else
			g_warning ("invalid color '%s'", bg);
	}

	g_free (fg);
	g_free (bg);

	*style_p = style;
	*style_name_p = style_name;

	return TRUE;
}

static void
start_element (GMarkupParseContext *context,
	       const gchar         *element_name,
	       const gchar        **attribute_names,
	       const gchar        **attribute_values,
	       gpointer             user_data,
	       GError             **error)
{
	ParserData *data = user_data;
	GtkSourceStyle *style;
	gchar *style_name;

	if (data->done || (!data->scheme && strcmp (element_name, "style-scheme")))
	{
		g_set_error (error, ERROR_QUARK, 0,
			     "unexpected element '%s'",
			     element_name);
		return;
	}

	if (!data->scheme)
	{
		data->scheme = g_object_new (GTK_TYPE_SOURCE_STYLE_SCHEME, NULL);

		while (attribute_names && *attribute_names)
		{
			if (!strcmp (*attribute_names, "id"))
			{
				data->scheme->priv->id = g_strdup (*attribute_values);
			}
			else if (!strcmp (*attribute_names, "name"))
			{
				data->scheme->priv->name = g_strdup (*attribute_values);
			}
			else if (!strcmp (*attribute_names, "parent-scheme"))
			{
				g_warning ("%s: implement me", G_STRLOC);
			}
			else
			{
				g_set_error (error, ERROR_QUARK, 0,
					     "unexpected attribute '%s' in element 'style-scheme'",
					     *attribute_names);
				return;
			}

			attribute_names++;
			attribute_values++;
		}

		return;
	}

	if (strcmp (element_name, "style"))
	{
		g_set_error (error, ERROR_QUARK, 0,
			     "unexpected element '%s'",
			     element_name);
		return;
	}

	if (!parse_style (element_name, attribute_names, attribute_values,
			  &style_name, &style, error))
	{
		return;
	}

	g_hash_table_insert (data->scheme->priv->styles, style_name, style);
}

static void
end_element (GMarkupParseContext *context,
	     const gchar         *element_name,
	     gpointer             user_data,
	     GError             **error)
{
	ParserData *data = user_data;
	if (!strcmp (element_name, "style-scheme"))
		data->done = TRUE;
}

GtkSourceStyleScheme *
_gtk_source_style_scheme_new_from_file (const gchar *filename)
{
	GMarkupParseContext *ctx = NULL;
	GError *error = NULL;
	char *text = NULL;
	ParserData data;
	GMarkupParser parser = {start_element, end_element, NULL, NULL, NULL};

	g_return_val_if_fail (filename != NULL, NULL);

	if (!g_file_get_contents (filename, &text, NULL, &error))
	{
		g_warning ("could not load style scheme file '%s': %s",
			   filename, error->message);
		g_error_free (error);
		return NULL;
	}

	data.scheme = NULL;
	data.done = FALSE;

	ctx = g_markup_parse_context_new (&parser, 0, &data, NULL);

	if (!g_markup_parse_context_parse (ctx, text, -1, &error) ||
	    !g_markup_parse_context_end_parse (ctx, &error))
	{
		if (data.scheme)
			g_object_unref (data.scheme);
		data.scheme = NULL;
		g_warning ("could not load style scheme file '%s': %s",
			   filename, error->message);
		g_error_free (error);
	}

	g_markup_parse_context_free (ctx);
	g_free (text);
	return data.scheme;
}
