/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- 
 *  gtksourcetag.h
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

#ifndef __GTK_SOURCE_TAG_H__
#define __GTK_SOURCE_TAG_H__

#include <gtk/gtktexttag.h>

#include "gtksourceregex.h"

G_BEGIN_DECLS

#define GTK_TYPE_SYNTAX_TAG            (gtk_syntax_tag_get_type ())
#define GTK_SYNTAX_TAG(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GTK_TYPE_SYNTAX_TAG, GtkSyntaxTag))
#define GTK_SYNTAX_TAG_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), GTK_TYPE_SYNTAX_TAG, GtkSyntaxTagClass))
#define GTK_IS_SYNTAX_TAG(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GTK_TYPE_SYNTAX_TAG))
#define GTK_IS_SYNTAX_TAG_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GTK_TYPE_SYNTAX_TAG))
#define GTK_SYNTAX_TAG_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), GTK_TYPE_SYNTAX_TAG, GtkSyntaxTagClass))

#define GTK_TYPE_PATTERN_TAG            (gtk_pattern_tag_get_type ())
#define GTK_PATTERN_TAG(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GTK_TYPE_PATTERN_TAG, GtkPatternTag))
#define GTK_PATTERN_TAG_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), GTK_TYPE_PATTERN_TAG, GtkPatternTagClass))
#define GTK_IS_PATTERN_TAG(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GTK_TYPE_PATTERN_TAG))
#define GTK_IS_PATTERN_TAG_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GTK_TYPE_PATTERN_TAG))
#define GTK_PATTERN_TAG_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), GTK_TYPE_PATTERN_TAG, GtkPatternTagClass))

#define GTK_TYPE_KEYWORD_TAG            (gtk_keyword_tag_get_type ())
#define GTK_KEYWORD_TAG(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GTK_TYPE_KEYWORD_TAG, GtkKeywordTag))
#define GTK_KEYWORD_TAG_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), GTK_TYPE_KEYWORD_TAG, GtkKeywordTagClass))
#define GTK_IS_KEYWORD_TAG(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GTK_TYPE_KEYWORD_TAG))
#define GTK_IS_KEYWORD_TAG_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GTK_TYPE_KEYWORD_TAG))
#define GTK_KEYWORD_TAG_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), GTK_TYPE_KEYWORD_TAG, GtkKeywordTagClass))


#define GTK_IS_SOURCE_TAG(obj)		(GTK_IS_PATTERN_TAG(obj) || GTK_IS_SYNTAX_TAG(obj))



typedef struct _GtkSyntaxTag        GtkSyntaxTag;
typedef struct _GtkSyntaxTagClass   GtkSyntaxTagClass;

typedef struct _GtkPatternTag       GtkPatternTag;
typedef struct _GtkPatternTagClass  GtkPatternTagClass;

typedef struct _GtkPatternTag       GtkKeywordTag;
typedef struct _GtkPatternTagClass  GtkKeywordTagClass;

struct _GtkSyntaxTag 
{
	GtkTextTag		 parent_instance;

	gchar			*start;  
	GtkSourceRegex		 reg_start;
	GtkSourceRegex		 reg_end;
};

struct _GtkSyntaxTagClass 
{
	GtkTextTagClass		 parent_class; 
};

struct _GtkPatternTag 
{
	GtkTextTag		 parent_instance;

	GtkSourceRegex		 reg_pattern;
};

struct _GtkPatternTagClass 
{
	GtkTextTagClass		 parent_class; 
};


GType		 gtk_syntax_tag_get_type	(void) G_GNUC_CONST;
GtkTextTag 	*gtk_syntax_tag_new		(const gchar 	*name, 
						 const gchar 	*pattern_start,
						 const gchar 	*pattern_end);

GType      	 gtk_pattern_tag_get_type	(void) G_GNUC_CONST;
GtkTextTag	*gtk_pattern_tag_new		(const gchar 	*name, 
						 const gchar 	*pattern);

#define gtk_keyword_tag_get_type	gtk_pattern_tag_get_type

GtkTextTag	*gtk_keyword_tag_new		(const gchar 	*name, 
						 const GSList 	*keywords,
						 gboolean	 case_sensitive,
						 gboolean	 match_empty_string_at_beginning,
						 gboolean	 match_empty_string_at_end,
						 const gchar    *beginning_regex,
						 const gchar    *end_regex);

#define gtk_block_comment_tag_get_type	gtk_syntax_tag_get_type
#define gtk_block_comment_tag_new	gtk_syntax_tag_new

#define gtk_line_comment_tag_get_type	gtk_syntax_tag_get_type

GtkTextTag	*gtk_line_comment_tag_new	(const gchar    *name,
						 const gchar    *pattern_start);

#define gtk_string_tag_get_type		gtk_syntax_tag_get_type

GtkTextTag	*gtk_string_tag_new		(const gchar    *name,
						 const gchar    *pattern_start,
						 const gchar	*pattern_end,
						 gboolean        end_at_line_end);


G_END_DECLS

#endif
