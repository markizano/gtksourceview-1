/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- 
 *  gtksourcetag.c
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

#include "gtksourceview-i18n.h"
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


static gchar *
case_insesitive_keyword (const gchar *keyword)
{
	GString *str;
	gint length;
	
	const gchar *cur;
	const gchar *end;

	g_return_val_if_fail (keyword != NULL, NULL);

	length = strlen (keyword);

	str = g_string_new ("");

	cur = keyword;
	end = keyword + length;
	
	while (cur != end) 
	{
		gunichar cur_char;
		cur_char = g_utf8_get_char (cur);
		
		if (((cur_char >= 'A') && (cur_char <= 'Z')) ||
		    ((cur_char >= 'a') && (cur_char <= 'z')))
		{
			gunichar cur_char_upper;
		       	gunichar cur_char_lower;
	
			cur_char_upper = g_unichar_toupper (cur_char);
			cur_char_lower = g_unichar_tolower (cur_char);
		
			g_string_append_c (str, '[');
			g_string_append_unichar (str, cur_char_lower);
			g_string_append_unichar (str, cur_char_upper);
			g_string_append_c (str, ']');
		}
		else
			g_string_append_unichar (str, cur_char);

		cur = g_utf8_next_char (cur);
	}
			
	return g_string_free (str, FALSE);
}

GtkTextTag *
gtk_keyword_list_tag_new (const gchar  *name, 
			  const GSList *keywords,
			  gboolean      case_sensitive,
			  gboolean      match_empty_string_at_beginning,
			  gboolean      match_empty_string_at_end,
			  const gchar  *beginning_regex,
			  const gchar  *end_regex)
{
	
	GtkTextTag *tag;
	GString *str;

	g_return_val_if_fail (keywords != NULL, NULL);

	str =  g_string_new ("");

	if (match_empty_string_at_beginning)
		g_string_append (str, "\\b");

	if (beginning_regex != NULL)
		g_string_append (str, beginning_regex);

	g_string_append (str, "\\(");
	
	while (keywords != NULL)
	{
		gchar *k;
		
		if (case_sensitive)
			k = (gchar*)keywords->data;
		else
			k = case_insesitive_keyword ((gchar*)keywords->data);

		g_string_append (str, k);

		if (!case_sensitive)
			g_free (k);

		keywords = g_slist_next (keywords);

		if (keywords != NULL)
			g_string_append (str, "\\|");
	}

	g_string_append (str, "\\)");

	if (end_regex != NULL)
		g_string_append (str, end_regex);

	if (match_empty_string_at_end)
		g_string_append (str, "\\b");

	tag = gtk_pattern_tag_new (name, str->str);

	g_string_free (str, TRUE);
	
	return tag;
}

GtkTextTag *
gtk_line_comment_tag_new (const gchar *name, const gchar *pattern_start)
{
	g_return_val_if_fail (pattern_start != NULL, NULL);

	return gtk_syntax_tag_new (name, pattern_start, "\n");
}

GtkTextTag *
gtk_string_tag_new (const gchar    *name,
	            const gchar    *pattern_start,
		    const gchar    *pattern_end,
		    gboolean        end_at_line_end)
{
	g_return_val_if_fail (pattern_start != NULL, NULL);
	g_return_val_if_fail (pattern_end != NULL, NULL);

	if (!end_at_line_end)
		return gtk_syntax_tag_new (name, pattern_start, pattern_end);
	else
	{
		GtkTextTag *tag;
		gchar *end;
		
		end = g_strdup_printf ("\\(%s\\|\n\\)", pattern_end);

		tag = gtk_syntax_tag_new (name, pattern_start, end);

		g_free (end);

		return tag;
	}
}



