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
#include <libgnome/gnome-util.h>
#include <gconf/gconf.h>
#include <gconf/gconf-client.h>

#include "gtksourcelanguage.h"
#include "gtksourcetag.h"

struct _GtkSourceLanguagePrivate 
{
	gchar     *lang_file_name;

	gchar     *name;
	gchar     *section;

	GSList    *mime_types;
};

static void		 gtk_source_language_class_init 	(GtkSourceLanguageClass 	*klass);
static void		 gtk_source_language_init		(GtkSourceLanguage 		*lang);
static void	 	 gtk_source_language_finalize 		(GObject 			*object);

static GSList	 	*build_file_listing 			(const gchar 			*directory, 
					 		 	 GSList				*filenames);
static GtkSourceLanguage *get_language_from_file 		(const gchar 			*filename);
static const gchar 	*get_gconf_base_dir 			(void);

static GSList	*language_specs_directories 	= NULL;
static GSList 	*available_languages 		= NULL;

static gchar	*gconf_base_dir		 	= NULL;

#define DEFAULT_GCONF_BASE_DIR		"/apps/gtksourceview"

#define DEFAULT_LANGUAGE_DIR		DATADIR "/gtksourceview/language-specs"
#define USER_LANGUAGE_DIR		"gtksourceview/language-specs"


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
	g_slist_foreach (list, (GFunc) g_free, NULL);
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

	if (language_specs_directories == NULL)
	{
		gtk_source_set_language_specs_directories (NULL);
	}

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

	if (dirs == NULL)
	{
		language_specs_directories =
			g_slist_prepend (language_specs_directories,
					g_strdup (DEFAULT_LANGUAGE_DIR));
		language_specs_directories = 
			g_slist_prepend (language_specs_directories,
					gnome_util_home_file (USER_LANGUAGE_DIR));

		return;
	}

	while (dirs != NULL)
	{
		language_specs_directories = 
			g_slist_prepend (language_specs_directories,
					 g_strdup ((const gchar*)dirs->data));

		dirs = g_slist_next (dirs);
	}
}

static GConfClient *
get_gconf_client ()
{
	static GConfClient *gconf_client = NULL;

	if (gconf_client == NULL)
	{
		gconf_client = gconf_client_get_default ();
		
		if (gconf_client == NULL)
		{
			g_warning ("Cannot connect to preferences manager.");
			return NULL;
		}
	}

	return gconf_client;
}

static gchar *
get_gconf_key (const GtkSourceLanguage *lang, const gchar *k)
{
	gchar *temp;
	gchar *key;
	gchar *name;
       
	name = gconf_escape_key (gtk_source_language_get_name (lang), -1);
	g_return_val_if_fail (name != NULL, NULL);

	temp = gconf_concat_dir_and_key (get_gconf_base_dir (), name);
	g_return_val_if_fail (gconf_valid_key (temp, NULL), NULL);

	g_free (name);

	key = gconf_concat_dir_and_key (temp, k);
	g_return_val_if_fail (gconf_valid_key (key, NULL), NULL);

	g_free (temp);
	
	return key;
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

	if (lang != NULL)
	{
		GConfClient *gconf_client = get_gconf_client ();
		
		if (get_gconf_client != NULL)
		{
			GSList *mime_types = NULL;
			gchar *key;

			key = get_gconf_key (lang, "mime_types");
			g_return_val_if_fail (key != NULL, lang);

			mime_types = gconf_client_get_list (gconf_client,
							    key,
							    GCONF_VALUE_STRING, 
							    NULL);

			/* Get mime types from gconf if needed */

			if (mime_types != NULL)
			{
				slist_deep_free (lang->priv->mime_types);
				lang->priv->mime_types = mime_types;
			}
		}
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

GtkSourceLanguage *
gtk_source_language_get_from_mime_type (const gchar *mime_type)
{
	const GSList *languages;
	g_return_val_if_fail (mime_type != NULL, NULL);

	languages = gtk_source_get_available_languages ();

	while (languages != NULL)
	{
		const GSList *mime_types;

		GtkSourceLanguage *lang = GTK_SOURCE_LANGUAGE (languages->data);
		
		mime_types = gtk_source_language_get_mime_types (lang);

		while (mime_types != NULL)
		{
			/* FIXME: is this right ? - Paolo */
			if (strcmp ((const gchar*)mime_types->data, mime_type) == 0)
			{
				g_object_ref (lang);
				
				return lang;
			}

			mime_types = g_slist_next (mime_types);
		}

		languages = g_slist_next (languages);
	}

	return NULL;
}

const gchar *
get_gconf_base_dir ()
{
	if (gconf_base_dir == NULL)
		gtk_source_set_gconf_base_dir (NULL);

	g_return_val_if_fail (gconf_base_dir != NULL, NULL);

	return gconf_base_dir;
}

void 
gtk_source_set_gconf_base_dir (const gchar *dir)
{
	g_free (gconf_base_dir);

	if (dir != NULL)
		gconf_base_dir = g_strdup (dir);
	else
		gconf_base_dir = g_strdup (DEFAULT_GCONF_BASE_DIR);
}

static GSList *
get_mime_types_from_file (const gchar *filename)
{
	xmlTextReaderPtr reader;
	gint ret;
	GSList *mime_types = NULL;
	
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
					gchar *mimetypes;
					gchar** mtl;
					gint i;

					mimetypes = xmlTextReaderGetAttribute (reader, "mimetypes");
					
					if (mimetypes == NULL)
					{
						g_warning ("Impossible to get mimetypes from file '%s'",
			   				   filename);

						ret = 0;
					}
					else
					{

						mtl = g_strsplit (mimetypes, ";" , 0);

						i = 0; 
	
						do
						{
							mime_types = g_slist_prepend (mime_types,
										      g_strdup (mtl[i]));

							++i;
						} while (mtl[i] != NULL);

						g_strfreev (mtl);
						xmlFree (mimetypes);

						ret = 0;
					}
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

	return mime_types;
}

void 
gtk_source_language_set_mime_types (GtkSourceLanguage	*language,
				    GSList		*mime_types)
{
	GSList *l;
	gchar *key;
	GConfClient *gconf_client;
	g_return_if_fail (GTK_IS_SOURCE_LANGUAGE (language));

	key = get_gconf_key (language, "mime_types");
	g_return_if_fail (key != NULL);
	
	slist_deep_free (language->priv->mime_types);
	language->priv->mime_types = NULL;

	/* Dup mime_types */
	l = mime_types;
	while (l != NULL)
	{
		language->priv->mime_types = g_slist_prepend (language->priv->mime_types,
							      g_strdup ((const gchar*)l->data));
		l = g_slist_next (l);
	}
	language->priv->mime_types = g_slist_reverse (language->priv->mime_types);

	gconf_client = get_gconf_client ();
	
	if (mime_types != NULL)
	{
		if (gconf_client != NULL)
			gconf_client_set_list (gconf_client,
					       key,
					       GCONF_VALUE_STRING,
					       mime_types,
					       NULL);
	}
	else
	{
		if (gconf_client != NULL)
			gconf_client_unset (gconf_client,
					    key,
					    NULL);
					    
		/* Get mime types from XML file */
		language->priv->mime_types = get_mime_types_from_file (
						language->priv->lang_file_name);
	}

}

static void
parseTag (xmlDocPtr doc, xmlNodePtr cur, GSList *tag_list)
{
	GtkTextTag *tag = NULL;
	xmlChar *name;
	xmlChar *style;
	
	xmlNodePtr child;

	name = xmlGetProp(cur, "name");
	style = xmlGetProp(cur, "style");

	if (name == NULL)
	{
		g_warning ("Impossible to get the tag name (%s, line %ld)", 
			   doc->name, xmlGetLineNo (cur));
		return;	
	}
	if (style == NULL)
	{
		xmlFree (name);

		g_warning ("Impossible to get the tag style (%s, line %ld)", 
			   doc->name, xmlGetLineNo (cur));
		return;	

	}
	
	if (!xmlStrcmp (cur->name, (const xmlChar *)"line-comment"))
	{
		child = cur->xmlChildrenNode;
		
		if ((child != NULL) && !xmlStrcmp (child->name, (const xmlChar *)"start-regex"))
		{
			xmlChar *start_regex;
			
			start_regex = xmlNodeListGetString (doc, child->xmlChildrenNode, 1);
			
			tag = gtk_line_comment_tag_new (name, start_regex);

			g_print ("line commen: %s, %s, %s", name, style, start_regex);

			xmlFree (start_regex);
		}
		else
		{
			g_warning ("Missing start-regex in tag 'line-comment' (%s, line %ld)", 
				   doc->name, xmlGetLineNo (child));
		}
	}

	if (tag != NULL)
		tag_list = g_slist_prepend (tag_list, tag);

	xmlFree (name);
	xmlFree (style);
}

const GSList *
gtk_source_language_get_tags (const GtkSourceLanguage *language)
{
	GSList *tag_list = NULL;
	
	xmlDocPtr doc;
	xmlNodePtr cur;

	g_return_val_if_fail (GTK_IS_SOURCE_LANGUAGE (language), NULL);

	xmlKeepBlanksDefault (0);

	doc = xmlParseFile (language->priv->lang_file_name);
	if (doc == NULL)
	{
		g_warning ("Impossible to parse file '%s'", 
			   language->priv->lang_file_name);
		return NULL;
	}

	cur = xmlDocGetRootElement(doc);
	
	if (cur == NULL) 
	{
		g_warning ("The lang file '%s' is empty", 
			   language->priv->lang_file_name);

		xmlFreeDoc(doc);
		return NULL;
	}

	if (xmlStrcmp(cur->name, (const xmlChar *) "language")) {
		g_warning ("File '%s' is of the wrong type",
			   language->priv->lang_file_name);
		
		xmlFreeDoc(doc);
		return NULL;
	}

	/* FIXME: check that the language name, version, etcc are the 
	 * right ones - Paolo */

	cur = cur->xmlChildrenNode;
	while (cur != NULL)
	{
		parseTag (doc, cur, tag_list);
		
		cur = cur->next;
	}
      
	xmlFreeDoc(doc);

	return tag_list;
}



