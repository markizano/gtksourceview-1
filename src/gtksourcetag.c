/*  gtksourcetag.c
 *
 *  Copyright (C) 2001
 *  Mikael Hermansson<tyan@linux.se>
 *  Chris Phelps <chicane@reninet.com>
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include "gtksourcetag.h"

static GObjectClass 	*parent_syntax_class  = NULL;
static GObjectClass 	*parent_pattern_class = NULL;

static void		 gtk_syntax_tag_init 		(GtkSyntaxTag       *text_tag);
static void		 gtk_syntax_tag_class_init 	(GtkSyntaxTagClass  *text_tag);
static void		 gtk_syntax_tag_finalize 	(GObject            *object);

static void		 gtk_pattern_tag_init 		(GtkPatternTag      *text_tag);
static void		 gtk_pattern_tag_class_init 	(GtkPatternTagClass *text_tag);
static void		 gtk_pattern_tag_finalize 	(GObject            *object);


/* Styntax tag */

GType
gtk_syntax_tag_get_type (void)
{
	static GType our_type = 0;

	if (our_type == 0) {
		static const GTypeInfo our_info = {
			sizeof (GtkSyntaxTagClass),
			(GBaseInitFunc) NULL,
			(GBaseFinalizeFunc) NULL,
			(GClassInitFunc) gtk_syntax_tag_class_init,
			NULL,	/* class_finalize */
			NULL,	/* class_data */
			sizeof (GtkSyntaxTag),
			0,	/* n_preallocs */
			(GInstanceInitFunc) gtk_syntax_tag_init
		};

		our_type =
		    g_type_register_static (GTK_TYPE_TEXT_TAG,
					    "GtkSyntaxTag", &our_info, 0);
	}

	return our_type;
}

static void
gtk_syntax_tag_class_init (GtkSyntaxTagClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_syntax_class	= g_type_class_peek_parent (klass);
	object_class->finalize	= gtk_syntax_tag_finalize;
}

static void
gtk_syntax_tag_init (GtkSyntaxTag *text_tag)
{
}


GtkTextTag *
gtk_syntax_tag_new (const gchar *name, 
		    const gchar *pattern_start,
		    const gchar *pattern_end)
{
	GtkSyntaxTag *tag;

	g_return_val_if_fail (pattern_start != NULL, NULL);
	g_return_val_if_fail (pattern_end != NULL, NULL);

	tag = GTK_SYNTAX_TAG (g_object_new (GTK_TYPE_SYNTAX_TAG, 
					    "name", name,
					    NULL));
	
	tag->start = g_strdup (pattern_start);
	
	if (!gtk_source_regex_compile (&tag->reg_start, pattern_start)) {
		g_warning ("Regex syntax start pattern failed [%s]", pattern_start);
		return NULL;
	}
	
	if (!gtk_source_regex_compile (&tag->reg_end, pattern_end)) {
		g_warning ("Regex syntax end pattern failed [%s]\n", pattern_end);
		return NULL;
	}

	return GTK_TEXT_TAG (tag);
}

static void
gtk_syntax_tag_finalize (GObject *object)
{
	GtkSyntaxTag *tag;

	tag = GTK_SYNTAX_TAG (object);
	
	g_free (tag->start);
	
	gtk_source_regex_destroy (&tag->reg_start);
	gtk_source_regex_destroy (&tag->reg_end);

	G_OBJECT_CLASS (parent_syntax_class)->finalize (object);
}


/* Pattern Tag */

GType
gtk_pattern_tag_get_type (void)
{
	static GType our_type = 0;

	if (our_type == 0) {
		static const GTypeInfo our_info = {
			sizeof (GtkPatternTagClass),
			(GBaseInitFunc) NULL,
			(GBaseFinalizeFunc) NULL,
			(GClassInitFunc) gtk_pattern_tag_class_init,
			NULL,	/* class_finalize */
			NULL,	/* class_data */
			sizeof (GtkPatternTag),
			0,	/* n_preallocs */
			(GInstanceInitFunc) gtk_pattern_tag_init
		};

		our_type =
		    g_type_register_static (GTK_TYPE_TEXT_TAG,
					    "GtkPatternTag", &our_info, 0);
	}
	return (our_type);
}

static void
gtk_pattern_tag_init (GtkPatternTag *text_tag)
{

}

static void
gtk_pattern_tag_class_init (GtkPatternTagClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_pattern_class	= g_type_class_peek_parent (klass);
	object_class->finalize	= gtk_pattern_tag_finalize;
}

GtkTextTag *
gtk_pattern_tag_new (const gchar *name, const gchar *pattern)
{
	GtkPatternTag *tag;

	g_return_val_if_fail (pattern != NULL, NULL);

	tag = GTK_PATTERN_TAG (g_object_new (GTK_TYPE_PATTERN_TAG, 
					     "name", name,
					     NULL));
	
	if (!gtk_source_regex_compile (&tag->reg_pattern, pattern)) {
		g_warning ("Regex pattern failed [%s]\n", pattern);
		return NULL;
	}

	return GTK_TEXT_TAG (tag);
}

static void
gtk_pattern_tag_finalize (GObject *object)
{
	GtkPatternTag *tag;

	tag = GTK_PATTERN_TAG (object);
	
	gtk_source_regex_destroy (&tag->reg_pattern);
	
	G_OBJECT_CLASS (parent_pattern_class)->finalize (object);
}



