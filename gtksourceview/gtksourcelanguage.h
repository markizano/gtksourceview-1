/*  gtksourcelanguage.h
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

#ifndef __GTK_SOURCE_LANGUAGE_H__
#define __GTK_SOURCE_LANGUAGE_H__

#include <glib.h>
#include <glib-object.h> 
#include <gtk/gtk.h> 

G_BEGIN_DECLS

#define GTK_TYPE_SOURCE_LANGUAGE		(gtk_source_language_get_type ())
#define GTK_SOURCE_LANGUAGE(obj)		(GTK_CHECK_CAST ((obj), GTK_TYPE_SOURCE_LANGUAGE, GtkSourceLanguage))
#define GTK_SOURCE_LANGUAGE_CLASS(klass)	(GTK_CHECK_CLASS_CAST ((klass), GTK_TYPE_SOURCE_LANGUAGE, GtkSourceLanguageClass))
#define GTK_IS_SOURCE_LANGUAGE(obj)		(GTK_CHECK_TYPE ((obj), GTK_TYPE_SOURCE_LANGUAGE))
#define GTK_IS_SOURCE_LANGUAGE_CLASS(klass)	(GTK_CHECK_CLASS_TYPE ((klass), GTK_TYPE_SOURCE_LANGUAGE))


typedef struct _GtkSourceLanguage		GtkSourceLanguage;
typedef struct _GtkSourceLanguageClass		GtkSourceLanguageClass;

typedef struct _GtkSourceLanguagePrivate	GtkSourceLanguagePrivate;

typedef struct _GtkSourceTagStyle GtkSourceTagStyle;

struct _GtkSourceLanguage 
{
	GObject                   parent;

	GtkSourceLanguagePrivate *priv;
};

struct _GtkSourceLanguageClass 
{
	GObjectClass              parent_class;
};


/* Do we really need these functions ? */
void			 gtk_source_set_language_specs_directories	(const GSList 		 *dirs);
void			 gtk_source_set_gconf_base_dir			(const gchar		 *dir);

const GSList 		*gtk_source_get_available_languages		(void);


GType            	 gtk_source_language_get_type 			(void) G_GNUC_CONST;

GtkSourceLanguage	*gtk_source_language_get_from_mime_type		(const gchar             *mime_type);

const gchar	 	*gtk_source_language_get_name			(const GtkSourceLanguage *language);
const gchar		*gtk_source_language_get_section		(const GtkSourceLanguage *language);

/*
const GSList		*gtk_source_language_get_tags			(const GtkSourceLanguage *language);
*/

const GSList		*gtk_source_language_get_mime_types		(const GtkSourceLanguage *language);


void			 gtk_source_language_set_mime_types		(GtkSourceLanguage       *language,
								 	 GSList			 *mime_types);
/*
const GtkSourceTagStyle	*gtk_source_language_get_tag_style		(const GtkSourceLanguage *language,
									 const gchar		 *tag_name);
void			*gtk_source_language_set_tag_style		(const GtkSourceLanguage *language,
									 const gchar		 *tag_name,
								 	 const GtkSourceTagStyle *style);

const GtkSourceTagStyle	*gtk_source_language_get_default_tag_style	(const GtkSourceLanguage *language,
								 	 const gchar		 *tag_name);
*/
G_END_DECLS				

#endif /* __GTK_SOURCE_LANGUAGE_H__ */

