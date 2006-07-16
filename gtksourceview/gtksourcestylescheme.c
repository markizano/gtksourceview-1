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

struct _GtkSourceStyleSchemePrivate
{
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

	if (scheme->priv->parent)
		g_object_unref (scheme->priv->parent);

	G_OBJECT_CLASS (gtk_source_style_scheme_parent_class)->finalize (object);
}

static void
gtk_source_style_scheme_class_init (GtkSourceStyleSchemeClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = gtk_source_style_scheme_finalize;

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

GtkSourceStyleScheme *
gtk_source_style_scheme_new (GtkSourceStyleScheme *parent)
{
	GtkSourceStyleScheme *scheme;

	g_return_val_if_fail (!parent || GTK_IS_SOURCE_STYLE_SCHEME (parent), NULL);

	scheme = g_object_new (GTK_TYPE_SOURCE_STYLE_SCHEME, NULL);

	if (parent)
		scheme->priv->parent = g_object_ref (parent);

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
gtk_source_style_scheme_get_default (void)
{
	if (default_scheme)
		return g_object_ref (default_scheme);

	default_scheme = gtk_source_style_scheme_new (NULL);
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

#undef ADD_FORE
#undef ADD_FORE_BOLD

	return default_scheme;
}
