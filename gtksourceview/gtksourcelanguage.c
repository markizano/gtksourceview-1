/*  gtksourcelanguage.c
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include <libxml/xmlreader.h>
#include <libgnome/gnome-util.h> /* For g_pointer_extesions */

#include "gtksourcelanguage.h"

struct _GtkSourceLanguagePrivate 
{
	gchar     *lang_file_name;

	gchar     *name;
	gchar     *section;

	GSList    *mime_types;
};

static void	 gtk_source_language_class_init 	(GtkSourceLanguageClass 	*klass);
static void	 gtk_source_language_init		(GtkSourceLanguage 		*lang);
static void	 gtk_source_language_finalize 		(GObject 			*object);

static GSList 	*build_file_listing 			(const gchar 			*directory, 
					 		 GSList				*filenames);
static GtkSourceLanguage *get_language_from_file 	(const gchar 			*filename);

static GSList	*language_specs_directories 	= NULL;
static GSList 	*available_languages 		= NULL;

static GObjectClass 	*parent_class  = NULL;

GType
gtk_source_language_get_type (void)
{
	static GType our_type = 0;

	if (our_type == 0) {
		static const GTypeInfo our_info = {
			sizeof (GtkSourceLanguageClass),
			(GBaseInitFunc) NULL,
			(GBaseFinalizeFunc) NULL,
			(GClassInitFunc) gtk_source_language_class_init,
			NULL,	/* class_finalize */
			NULL,	/* class_data */
			sizeof (GtkSourceLanguage),
			0,	/* n_preallocs */
			(GInstanceInitFunc) gtk_source_language_init
		};

		our_type =
		    g_type_register_static (G_TYPE_OBJECT,
					    "GtkSourceLanguage", &our_info, 0);
	}

	return our_type;
}

static void
gtk_source_language_class_init (GtkSourceLanguageClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class		= g_type_class_peek_parent (klass);
	object_class->finalize	= gtk_source_language_finalize;
}

static void
gtk_source_language_init (GtkSourceLanguage *lang)
{
	lang->priv = g_new0 (GtkSourceLanguagePrivate, 1);
}

static void
slist_deep_free (GSList *list)
{
	while (list != NULL)
	{
		g_free (list->data);

		list = g_slist_next (list);
	}

	g_slist_free (list);
}

static void
gtk_source_language_finalize (GObject *object)
{
	GtkSourceLanguage *lang;

	lang = GTK_SOURCE_LANGUAGE (object);
	
	if (lang->priv != NULL)
	{
		g_free (lang->priv->lang_file_name);
		
		xmlFree (lang->priv->name);
		xmlFree (lang->priv->section);

		slist_deep_free (lang->priv->mime_types);
		
		g_free (lang->priv); 
	}
	
	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GSList *
build_file_listing (const gchar *directory, GSList *filenames)
{
	GDir *dir;
	const gchar *file_name;
	
	dir = g_dir_open (directory, 0, NULL);
	
	if (dir == NULL)
		return filenames;

	file_name = g_dir_read_name (dir);
	
	while (file_name != NULL)
	{
		gchar *full_path = g_build_filename (directory, file_name);

		if (!g_file_test (full_path, G_FILE_TEST_IS_DIR) && 
		    (strcmp (g_extension_pointer (full_path), "lang") == 0))
			filenames = g_slist_prepend (filenames, full_path);
		else
			g_free (full_path);

		file_name = g_dir_read_name (dir);
	}

	g_dir_close (dir);

	return filenames;
}

static GSList *
get_lang_files ()
{
	GSList *filenames = NULL;
	GSList *dirs;

	dirs = language_specs_directories;

	while (dirs != NULL)
	{
		filenames = build_file_listing ((const gchar*)dirs->data,
						filenames);

		dirs = g_slist_next (dirs);
	}

	return filenames;
}	

const GSList *
gtk_source_get_available_languages (void)
{
	GSList *filenames;

	if (available_languages != NULL)
	{
		return available_languages;
	}
	
	/* Build list of availables languages */
	filenames = get_lang_files ();
	
	while (filenames != NULL)
	{
		GtkSourceLanguage *lang = get_language_from_file ((const gchar*)filenames->data);

		if (lang == NULL)
		{
			g_warning ("Error reading language specification file '%s'", 
				   (const gchar*)filenames->data);
		}
		else
		{	
			available_languages = g_slist_prepend (available_languages, lang);
		}

		filenames = g_slist_next (filenames);
	}

	slist_deep_free (filenames);

	/* TODO: sorting available_languages */

	return available_languages;
}

void
gtk_source_set_language_specs_directories (const GSList *dirs)
{
	if (language_specs_directories != NULL)
	{
		slist_deep_free (language_specs_directories);
		language_specs_directories = NULL;
	}

	while (dirs != NULL)
	{
		language_specs_directories = 
			g_slist_prepend (language_specs_directories,
					 g_strdup ((const gchar*)dirs->data));

		dirs = g_slist_next (dirs);
	}
}

static GtkSourceLanguage *
process_language_node (xmlTextReaderPtr reader, const gchar *filename)
{
	gchar *version;
	gchar *mimetypes;
	gchar** mtl;
	int i;

	GtkSourceLanguage *lang;
	
	lang= g_object_new (GTK_TYPE_SOURCE_LANGUAGE, NULL);

	lang->priv->lang_file_name = g_strdup (filename);
	
	lang->priv->name = xmlTextReaderGetAttribute (reader, "name");
	if (lang->priv->name == NULL)
	{
		g_warning ("Impossible to get language name from file '%s'",
			   filename);

		g_object_unref (lang);
		return NULL;
	}

	lang->priv->section = xmlTextReaderGetAttribute (reader, "section");
	if (lang->priv->section == NULL)
	{
		g_warning ("Impossible to get language section from file '%s'",
			   filename);

		g_object_unref (lang);
		return NULL;
	}

	version = xmlTextReaderGetAttribute (reader, "version");
	if (version == NULL)
	{
		g_warning ("Impossible to get version number from file '%s'",
			   filename);

		g_object_unref (lang);
		return NULL;
	}
	else
	{
		if (strcmp (version , "1.0") != 0)
		{
			g_warning ("Usupported language spec version '%s' in file '%s'",
				   version, filename);

			xmlFree (version);
			
			g_object_unref (lang);
			return NULL;
		}

		xmlFree (version);
	}

	mimetypes = xmlTextReaderGetAttribute (reader, "mimetypes");
	if (mimetypes == NULL)
	{
		g_warning ("Impossible to get mimetypes from file '%s'",
			   filename);

		g_object_unref (lang);
		return NULL;
	}

	mtl = g_strsplit (mimetypes, ";" , 0);

	i = 0; 
	
	do
	{
		lang->priv->mime_types = g_slist_prepend (lang->priv->mime_types,
				g_strdup (mtl[i]));

		++i;
	} while (mtl[i] != NULL);

	g_strfreev (mtl);
	xmlFree (mimetypes);

	lang->priv->mime_types = g_slist_reverse (lang->priv->mime_types);

	return lang;
}

static GtkSourceLanguage *
get_language_from_file (const gchar *filename)
{
	GtkSourceLanguage *lang = NULL;

	xmlTextReaderPtr reader;
	gint ret;
	
	reader = xmlNewTextReaderFilename (filename);

	if (reader != NULL) 
	{
        	ret = xmlTextReaderRead (reader);
		
        	while (ret == 1) 
		{
			if (xmlTextReaderNodeType (reader) == 1)
			{
				xmlChar *name;

				name = xmlTextReaderName (reader);

				if (strcmp (name, "language") == 0)
				{

					lang = process_language_node (reader, filename);
					
					ret = 0;
				}

				xmlFree (name);
			}
			
			if (ret != 0)
				ret = xmlTextReaderRead (reader);
			
		}
	
		xmlFreeTextReader (reader);
        	
		if (ret != 0) 
		{
	            g_warning("Failed to parse '%s'", filename);
		    return NULL;
		}
        }
	else 
	{
		g_warning("Unable to open '%s'", filename);

    	}

	return lang;
}

const gchar *
gtk_source_language_get_name (const GtkSourceLanguage *language)
{
	g_return_val_if_fail (GTK_IS_SOURCE_LANGUAGE (language), NULL);

	return language->priv->name;
}

const gchar *
gtk_source_language_get_section	(const GtkSourceLanguage *language)
{
	g_return_val_if_fail (GTK_IS_SOURCE_LANGUAGE (language), NULL);

	return language->priv->section;
}

const GSList *
gtk_source_language_get_mime_types (const GtkSourceLanguage *language)
{
	g_return_val_if_fail (GTK_IS_SOURCE_LANGUAGE (language), NULL);

	return language->priv->mime_types;
}


