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

#include "gtksourcelanguagesmanager.h"

#include "gtksourcelanguage.h"
#include "gtksourcetag.h"


#define DEFAULT_GCONF_BASE_DIR		"/apps/gtksourceview"

#define DEFAULT_LANGUAGE_DIR		DATADIR "/gtksourceview/language-specs"
#define USER_LANGUAGE_DIR		"gtksourceview/language-specs"

typedef struct _GtkSourceLanguagesManager	GtkSourceLanguagesManager;

struct _GtkSourceLanguagesManager {

	GSList 		*available_languages;

	GSList		*language_specs_directories;
	gchar		*gconf_base_dir;
	
	GConfClient 	*gconf_client;
};

struct _GtkSourceLanguagePrivate 
{
	gchar		*lang_file_name;

	gchar		*name;
	gchar		*section;

	GSList		*mime_types;
};

static void		 gtk_source_language_class_init 	(GtkSourceLanguageClass 	*klass);
static void		 gtk_source_language_init		(GtkSourceLanguage 		*lang);
static void	 	 gtk_source_language_finalize 		(GObject 			*object);

static GSList 		 *get_lang_files 			(void);
static GtkSourceLanguage *get_language_from_file 		(const gchar 			*filename);

static GSList	 	 *build_file_listing 			(const gchar 			*directory, 
					 		 	 GSList				*filenames);

static GtkSourceLanguage *process_language_node 		(xmlTextReaderPtr 		 reader, 
								 const gchar 			*filename);
static gchar 		 *get_gconf_key 			(const GtkSourceLanguage 	*lang, 
								 const gchar 			*k);

static GObjectClass 	 *parent_class  = NULL;

static GtkSourceLanguagesManager *languages_manager = NULL;


static void
slist_deep_free (GSList *list)
{
	g_slist_foreach (list, (GFunc) g_free, NULL);
	g_slist_free (list);
}

gboolean
gtk_source_languages_manager_init (void)
{
	if (languages_manager == NULL)
	{
		GConfClient *gconf_client;

		languages_manager = g_new0 (GtkSourceLanguagesManager, 1);

		gconf_client = gconf_client_get_default ();
		if (gconf_client == NULL)
			goto init_error;

		languages_manager->gconf_client = gconf_client;

		gtk_source_languages_manager_set_gconf_base_dir (NULL);
		gtk_source_languages_manager_set_specs_dirs (NULL);		
	}

	return TRUE;

init_error:

	g_warning ("Error initializing the languages manager.");

	g_free (languages_manager);
	languages_manager = NULL;

	return FALSE;
}

void
gtk_source_languages_manager_shutdown (void)
{
	g_return_if_fail (languages_manager != NULL);

	g_object_unref (languages_manager->gconf_client);

	g_free (languages_manager->gconf_base_dir);
	
	if (languages_manager->available_languages != NULL)
	{
		GSList *list = languages_manager->available_languages;
		
		g_slist_foreach (list, (GFunc) g_object_unref, NULL);
		g_slist_free (list);
	}

	slist_deep_free (languages_manager->language_specs_directories);

	g_free (languages_manager);
	languages_manager = NULL;
}


void 
gtk_source_languages_manager_set_gconf_base_dir (const gchar *dir)
{
	g_return_if_fail (languages_manager != NULL);
	
	g_free (languages_manager->gconf_base_dir);

	if (dir != NULL)
		languages_manager->gconf_base_dir = g_strdup (dir);
	else
		languages_manager->gconf_base_dir = g_strdup (DEFAULT_GCONF_BASE_DIR);
}

void
gtk_source_languages_manager_set_specs_dirs (const GSList *dirs)
{
	g_return_if_fail (languages_manager != NULL);

	if (languages_manager->language_specs_directories != NULL)
	{
		slist_deep_free (languages_manager->language_specs_directories);
		languages_manager->language_specs_directories = NULL;
	}

	if (dirs == NULL)
	{
		languages_manager->language_specs_directories =
			g_slist_prepend (languages_manager->language_specs_directories,
					g_strdup (DEFAULT_LANGUAGE_DIR));
		languages_manager->language_specs_directories = 
			g_slist_prepend (languages_manager->language_specs_directories,
					gnome_util_home_file (USER_LANGUAGE_DIR));

		return;
	}

	while (dirs != NULL)
	{
		languages_manager->language_specs_directories = 
			g_slist_prepend (languages_manager->language_specs_directories,
					 g_strdup ((const gchar*)dirs->data));

		dirs = g_slist_next (dirs);
	}
}

const GSList *
gtk_source_languages_manager_get_available_languages (void)
{
	GSList *filenames;

	g_return_val_if_fail (languages_manager != NULL, NULL);

	if (languages_manager->available_languages != NULL)
	{
		return languages_manager->available_languages;
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
			languages_manager->available_languages = 
				g_slist_prepend (languages_manager->available_languages, lang);
		}

		filenames = g_slist_next (filenames);
	}

	slist_deep_free (filenames);

	/* TODO: sorting available_languages */

	return languages_manager->available_languages;
}

static GSList *
get_lang_files ()
{
	GSList *filenames = NULL;
	GSList *dirs;

	g_return_val_if_fail (languages_manager->language_specs_directories != NULL, NULL);

	dirs = languages_manager->language_specs_directories;

	while (dirs != NULL)
	{
		filenames = build_file_listing ((const gchar*)dirs->data,
						filenames);

		dirs = g_slist_next (dirs);
	}

	return filenames;
}

static GtkSourceLanguage *
get_language_from_file (const gchar *filename)
{
	GtkSourceLanguage *lang = NULL;

	xmlTextReaderPtr reader;
	gint ret;
	
	g_return_val_if_fail (languages_manager != NULL, NULL);

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
		if (languages_manager->gconf_client != NULL)
		{
			GSList *mime_types = NULL;
			gchar *key;

			key = get_gconf_key (lang, "mime_types");
			g_return_val_if_fail (key != NULL, lang);

			mime_types = gconf_client_get_list (languages_manager->gconf_client,
							    key,
							    GCONF_VALUE_STRING, 
							    NULL);

			g_free (key);
			
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


static gchar *
get_gconf_key (const GtkSourceLanguage *lang, const gchar *k)
{
	gchar *temp;
	gchar *key;
	gchar *name;
       
	g_return_val_if_fail (languages_manager != NULL, NULL);

	name = gconf_escape_key (gtk_source_language_get_name (lang), -1);
	g_return_val_if_fail (name != NULL, NULL);

	temp = gconf_concat_dir_and_key (languages_manager->gconf_base_dir, name);
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

	languages = gtk_source_languages_manager_get_available_languages ();

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
	
	g_return_if_fail (GTK_IS_SOURCE_LANGUAGE (language));
	g_return_if_fail (languages_manager != NULL);
	
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

	if (mime_types != NULL)
	{
		gconf_client_set_list (languages_manager->gconf_client,
				       key,
				       GCONF_VALUE_STRING,
				       mime_types,
				       NULL);
	}
	else
	{
		gconf_client_unset (languages_manager->gconf_client,
				    key,
				    NULL);
					    
		/* Get mime types from XML file */
		language->priv->mime_types = get_mime_types_from_file (
						language->priv->lang_file_name);
	}

	g_free (key);
}

/* FIXME: is this function UTF-8 aware? - Paolo */
static gchar *
strconvescape (gchar *source)
{
	gchar cur_char;
	gchar last_char = '\0';
	gint iterations = 0;
	gint max_chars;
	gchar *dest;

	if (source == NULL)
		return NULL;

	max_chars = strlen (source);
	dest = source;

	for (iterations = 0; iterations < max_chars; iterations++) {
		cur_char = source[iterations];
		*dest = cur_char;
		if (last_char == '\\' && cur_char == 'n') {
			dest--;
			*dest = '\n';
		} else if (last_char == '\\' && cur_char == 't') {
			dest--;
			*dest = '\t';
		}
		last_char = cur_char;
		dest++;
	}
	*dest = '\0';

	return source;
}


static GtkTextTag *
parseLineComment (xmlDocPtr doc, xmlNodePtr cur, xmlChar *name)
{
	GtkTextTag *tag = NULL;
	
	xmlNodePtr child;

	child = cur->xmlChildrenNode;
		
	if ((child != NULL) && !xmlStrcmp (child->name, (const xmlChar *)"start-regex"))
	{
		xmlChar *start_regex;
			
		start_regex = xmlNodeListGetString (doc, child->xmlChildrenNode, 1);
			
		tag = gtk_line_comment_tag_new (name, strconvescape (start_regex));

		xmlFree (start_regex);
	}
	else
	{
		g_warning ("Missing start-regex in tag 'line-comment' (%s, line %ld)", 
			   doc->name, xmlGetLineNo (child));
	}

	return tag;
}

static GtkTextTag *
parseBlockComment (xmlDocPtr doc, xmlNodePtr cur, xmlChar *name)
{
	GtkTextTag *tag = NULL;

	xmlChar *start_regex = NULL;
	xmlChar *end_regex = NULL;

	xmlNodePtr child;

	child = cur->xmlChildrenNode;
	
	while (child != NULL)
	{	
		if (!xmlStrcmp (child->name, (const xmlChar *)"start-regex"))
		{
			start_regex = xmlNodeListGetString (doc, child->xmlChildrenNode, 1);
		}
		else
		if (!xmlStrcmp (child->name, (const xmlChar *)"end-regex"))
		{
			end_regex = xmlNodeListGetString (doc, child->xmlChildrenNode, 1);
		}

		child = child->next;
	}

	if (start_regex == NULL)
	{
		g_warning ("Missing start-regex in tag 'block-comment' (%s, line %ld)", 
			   doc->name, xmlGetLineNo (cur));

		return NULL;
	}

	if (end_regex == NULL)
	{
		xmlFree (start_regex);

		g_warning ("Missing end-regex in tag 'block-comment' (%s, line %ld)", 
			   doc->name, xmlGetLineNo (cur));

		return NULL;
	}

	tag = gtk_block_comment_tag_new (name, 
					 strconvescape (start_regex), 
					 strconvescape (end_regex));

	xmlFree (start_regex);
	xmlFree (end_regex);

	return tag;
}

static GtkTextTag *
parseString (xmlDocPtr doc, xmlNodePtr cur, xmlChar *name)
{
	GtkTextTag *tag = NULL;

	xmlChar *start_regex = NULL;
	xmlChar *end_regex = NULL;

	xmlChar *prop = NULL;
	gboolean end_at_line_end = TRUE;

	xmlNodePtr child;

	prop = xmlGetProp (cur, "end-at-line-end");
	if (prop != NULL)
	{
		if (!xmlStrcasecmp (prop, (const xmlChar *)"TRUE") ||
		    !xmlStrcmp (prop, (const xmlChar *)"1"))

				end_at_line_end = TRUE;
			else
				end_at_line_end = FALSE;

		xmlFree (prop);	
	}
	
	child = cur->xmlChildrenNode;
	
	while (child != NULL)
	{	
		if (!xmlStrcmp (child->name, (const xmlChar *)"start-regex"))
		{
			start_regex = xmlNodeListGetString (doc, child->xmlChildrenNode, 1);
		}
		else
		if (!xmlStrcmp (child->name, (const xmlChar *)"end-regex"))
		{
			end_regex = xmlNodeListGetString (doc, child->xmlChildrenNode, 1);
		}
	
		child = child->next;
	}

	if (start_regex == NULL)
	{
		g_warning ("Missing start-regex in tag 'string' (%s, line %ld)", 
			   doc->name, xmlGetLineNo (cur));

		return NULL;
	}

	if (end_regex == NULL)
	{
		xmlFree (start_regex);

		g_warning ("Missing end-regex in tag 'string' (%s, line %ld)", 
			   doc->name, xmlGetLineNo (cur));

		return NULL;
	}

	tag = gtk_string_tag_new (name, 
				  strconvescape (start_regex), 
				  strconvescape (end_regex), 
				  end_at_line_end);

	xmlFree (start_regex);
	xmlFree (end_regex);

	return tag;
}

static GtkTextTag *
parseKeywordList (xmlDocPtr doc, xmlNodePtr cur, xmlChar *name)
{
	GtkTextTag *tag = NULL;

	gboolean case_sensitive = TRUE;
	gboolean match_empty_string_at_beginning = TRUE;
	gboolean match_empty_string_at_end = TRUE;
	gchar  *beginning_regex = NULL;
	gchar  *end_regex = NULL;

	GSList *list = NULL;

	xmlChar *prop;

	xmlNodePtr child;

	prop = xmlGetProp (cur, "case-sensitive");
	if (prop != NULL)
	{
		if (!xmlStrcasecmp (prop, (const xmlChar *)"TRUE") ||
		    !xmlStrcmp (prop, (const xmlChar *)"1"))

				case_sensitive = TRUE;
			else
				case_sensitive = FALSE;

		xmlFree (prop);	
	}

	prop = xmlGetProp (cur, "match-empty-string-at-beginning");
	if (prop != NULL)
	{
		if (!xmlStrcasecmp (prop, (const xmlChar *)"TRUE") ||
		    !xmlStrcmp (prop, (const xmlChar *)"1"))

				match_empty_string_at_beginning = TRUE;
			else
				match_empty_string_at_beginning = FALSE;

		xmlFree (prop);	
	}

	prop = xmlGetProp (cur, "match-empty-string-at-end");
	if (prop != NULL)
	{
		if (!xmlStrcasecmp (prop, (const xmlChar *)"TRUE") ||
		    !xmlStrcmp (prop, (const xmlChar *)"1"))

				match_empty_string_at_end = TRUE;
			else
				match_empty_string_at_end = FALSE;

		xmlFree (prop);	
	}

	prop = xmlGetProp (cur, "beginning-regex");
	if (prop != NULL)
	{
		beginning_regex = g_strdup (prop);
		
		xmlFree (prop);	
	}

	prop = xmlGetProp (cur, "end-regex");
	if (prop != NULL)
	{
		end_regex = g_strdup (prop);
		
		xmlFree (prop);	
	}

	child = cur->xmlChildrenNode;
	
	while (child != NULL)
	{
		if (!xmlStrcmp (child->name, (const xmlChar *)"keyword"))
		{
			xmlChar *keyword;
			keyword = xmlNodeListGetString (doc, child->xmlChildrenNode, 1);
			
			list = g_slist_prepend (list, strconvescape (keyword));
		}

		child = child->next;
	}

	list = g_slist_reverse (list);

	if (list == NULL)
	{
		g_warning ("No keywords in tag 'keyword-list' (%s, line %ld)", 
			   doc->name, xmlGetLineNo (cur));

		g_free (beginning_regex),
		g_free (end_regex);
		
		return NULL;
	}

	tag = gtk_keyword_list_tag_new (name, 
					list,
					case_sensitive,
					match_empty_string_at_beginning,
					match_empty_string_at_end,
					strconvescape (beginning_regex),
					strconvescape (end_regex));

	g_free (beginning_regex),
	g_free (end_regex);

	g_slist_foreach (list, (GFunc) xmlFree, NULL);
	g_slist_free (list);

	return tag;
}


static void
parseTag (xmlDocPtr doc, xmlNodePtr cur, GSList *tag_list)
{
	GtkTextTag *tag = NULL;
	xmlChar *name;
	xmlChar *style;
	
	name = xmlGetProp (cur, "name");
	style = xmlGetProp (cur, "style");

	if (name == NULL)
	{
		return;	
	}
	if (style == NULL)
	{
		/* FIXME */
		style = xmlStrdup ("Normal");
	}
	
	if (!xmlStrcmp (cur->name, (const xmlChar *)"line-comment"))
	{
		tag = parseLineComment (doc, cur, name);		
	}
	else if (!xmlStrcmp (cur->name, (const xmlChar *)"block-comment"))
	{
		tag = parseBlockComment (doc, cur, name);		
	}
	else if (!xmlStrcmp (cur->name, (const xmlChar *)"string"))
	{
		tag = parseString (doc, cur, name);		
	}
	else if (!xmlStrcmp (cur->name, (const xmlChar *)"keyword-list"))
	{
		tag = parseKeywordList (doc, cur, name);		
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



