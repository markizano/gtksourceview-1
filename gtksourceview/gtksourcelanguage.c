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

#include "gtksourceview-i18n.h"
#include "gtksourcetag.h"
#include "gtksourcebuffer.h"
#include "gtksourcelanguage.h"
#include "gtksourcelanguagesmanager.h"
#include "gtksourcelanguage-private.h"
#include "gtksourcesimpleengine.h"

#include "gtksourceview-marshal.h"

static void	 	  gtk_source_language_class_init 	(GtkSourceLanguageClass 	*klass);
static void		  gtk_source_language_init		(GtkSourceLanguage 		*lang);
static void		  gtk_source_language_finalize 		(GObject 			*object);

static GtkSourceLanguage *process_language_node 		(xmlTextReaderPtr 		 reader, 
								 const gchar 			*filename);
static GSList 		 *get_mime_types_from_file 		(GtkSourceLanguage 		*language);
static void               tag_style_changed_cb                  (GtkSourceLanguage              *language,
								 const gchar                    *id,
								 GtkSourceTag                   *tag);


/* Signals */
enum {
	TAG_STYLE_CHANGED = 0,
	LAST_SIGNAL
};

static GtkSourceTagStyle normal_style = { TRUE, 0 };

static guint 	 signals[LAST_SIGNAL] = { 0 };

static GObjectClass 	 *parent_class  = NULL;

static void
slist_deep_free (GSList *list)
{
	g_slist_foreach (list, (GFunc) g_free, NULL);
	g_slist_free (list);
}

GtkSourceLanguage *
_gtk_source_language_new_from_file (const gchar			*filename,
				    GtkSourceLanguagesManager	*lm)
{
	GtkSourceLanguage *lang = NULL;

	xmlTextReaderPtr reader;
	gint ret;
	
	g_return_val_if_fail (filename != NULL, NULL);
	g_return_val_if_fail (lm != NULL, NULL);

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

	signals[TAG_STYLE_CHANGED] = 
		g_signal_new ("tag_style_changed",
			  G_OBJECT_CLASS_TYPE (object_class),
			  G_SIGNAL_RUN_LAST,
			  G_STRUCT_OFFSET (GtkSourceLanguageClass, tag_style_changed),
			  NULL, NULL,
			  gtksourceview_marshal_VOID__STRING,
			  G_TYPE_NONE, 
			  1, 
			  G_TYPE_STRING);
}

static void
gtk_source_language_init (GtkSourceLanguage *lang)
{
	lang->priv = g_new0 (GtkSourceLanguagePrivate, 1);

	lang->priv->style_scheme = gtk_source_style_scheme_get_default ();
}

static void
gtk_source_language_finalize (GObject *object)
{
	GtkSourceLanguage *lang;

	lang = GTK_SOURCE_LANGUAGE (object);
		
	if (lang->priv != NULL)
	{
		g_free (lang->priv->lang_file_name);

		xmlFree (lang->priv->translation_domain);
		xmlFree (lang->priv->name);
		xmlFree (lang->priv->section);
		g_free  (lang->priv->id);

		slist_deep_free (lang->priv->mime_types);

		if (lang->priv->tag_id_to_style_name != NULL)
			g_hash_table_destroy (lang->priv->tag_id_to_style_name);

		if (lang->priv->tag_id_to_style != NULL)
			g_hash_table_destroy (lang->priv->tag_id_to_style);

		g_object_unref (lang->priv->style_scheme);

		g_free (lang->priv); 
	}
	
	G_OBJECT_CLASS (parent_class)->finalize (object);
}

gchar *
_gtk_source_language_strconvescape (gchar *source)
{
	gunichar cur_char;
	gunichar last_char = 0;
	gchar *dest;
	gchar *src;

	if (source == NULL)
		return NULL;

	src = dest = source;

	while (*src)
	{
		cur_char = g_utf8_get_char (src);
		src = g_utf8_next_char (src);
		if (last_char == '\\' && cur_char == 'n') {
			cur_char = '\n';
		} else if (last_char == '\\' && cur_char == 't') {
			cur_char = '\t';
		}
		dest += g_unichar_to_utf8 (cur_char, dest);
		last_char = cur_char;
	}
	*dest = '\0';

	return source;
}

static GtkSourceLanguage *
process_language_node (xmlTextReaderPtr reader, const gchar *filename)
{
	gchar *version;
	gchar *mimetypes;
	gchar** mtl;
	int i;
	xmlChar *tmp;
	GtkSourceLanguage *lang;
	
	lang = g_object_new (GTK_TYPE_SOURCE_LANGUAGE, NULL);

	lang->priv->lang_file_name = g_strdup (filename);
	
	lang->priv->translation_domain = xmlTextReaderGetAttribute (reader, "translation-domain");
	if (lang->priv->translation_domain == NULL)
	{
		lang->priv->translation_domain = xmlStrdup (GETTEXT_PACKAGE);
	}
	
	tmp = xmlTextReaderGetAttribute (reader, "_name");
	if (tmp == NULL)
	{
		lang->priv->name = xmlTextReaderGetAttribute (reader, "name");
		if (lang->priv->name == NULL)
		{
			g_warning ("Impossible to get language name from file '%s'",
				   filename);

			g_object_unref (lang);
			return NULL;
		}

		lang->priv->id = g_strdup (lang->priv->name);
	}
	else
	{
		lang->priv->id = g_strdup (tmp);
		lang->priv->name = xmlStrdup (dgettext (lang->priv->translation_domain, tmp));
		xmlFree (tmp);
	}

	tmp = xmlTextReaderGetAttribute (reader, "_section");
	if (tmp == NULL)
	{
		lang->priv->section = xmlTextReaderGetAttribute (reader, "section");
		if (lang->priv->section == NULL)
		{
			g_warning ("Impossible to get language section from file '%s'",
				   filename);
			
			g_object_unref (lang);
			return NULL;
		}
	}
	else
	{
		lang->priv->section = xmlStrdup (dgettext (lang->priv->translation_domain, tmp));
		xmlFree (tmp);
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
		if (strcmp (version , "1.0") == 0)
		{
			lang->priv->version = GTK_SOURCE_LANGUAGE_VERSION_1_0;
		}
		else if (strcmp (version, "2.0") == 0)
		{
			lang->priv->version = GTK_SOURCE_LANGUAGE_VERSION_2_0;
		}
		else
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

/**
 * gtk_source_language_get_id:
 * @language: a #GtkSourceLanguage.
 *
 * Returns the ID of the language. The ID is not locale-dependent.
 *
 * Return value: the ID of @language.
 **/
gchar *
gtk_source_language_get_id (GtkSourceLanguage *language)
{
	g_return_val_if_fail (GTK_IS_SOURCE_LANGUAGE (language), NULL);
	g_return_val_if_fail (language->priv->id != NULL, NULL);
		
	return g_strdup (language->priv->id);
}

/**
 * gtk_source_language_get_name:
 * @language: a #GtkSourceLanguage.
 *
 * Returns the localized name of the language.
 *
 * Return value: the name of @language.
 **/
gchar *
gtk_source_language_get_name (GtkSourceLanguage *language)
{
	g_return_val_if_fail (GTK_IS_SOURCE_LANGUAGE (language), NULL);
	g_return_val_if_fail (language->priv->name != NULL, NULL);

	return g_strdup (language->priv->name);
}

/**
 * gtk_source_language_get_section:
 * @language: a #GtkSourceLanguage.
 *
 * Returns the localized section of the language.
 * Each language belong to a section (ex. HTML belogs to the
 * Markup section)
 *
 * Return value: the section of @language.
 **/
gchar *
gtk_source_language_get_section	(GtkSourceLanguage *language)
{
	g_return_val_if_fail (GTK_IS_SOURCE_LANGUAGE (language), NULL);
	g_return_val_if_fail (language->priv->section != NULL, NULL);

	return g_strdup (language->priv->section);
}

gint 	
gtk_source_language_get_version (GtkSourceLanguage *language)
{
	g_return_val_if_fail (GTK_IS_SOURCE_LANGUAGE (language), 0);

	return language->priv->version;
}

GSList *
gtk_source_language_get_mime_types (GtkSourceLanguage *language)
{
	const GSList *l;
	GSList *mime_types = NULL;

	g_return_val_if_fail (GTK_IS_SOURCE_LANGUAGE (language), NULL);
	g_return_val_if_fail (language->priv->mime_types != NULL, NULL);

	/* Dup mime_types */
	
	l = language->priv->mime_types;
	
	while (l != NULL)
	{
		mime_types = g_slist_prepend (mime_types, g_strdup ((const gchar*)l->data));
		l = g_slist_next (l);
	}
	
	mime_types = g_slist_reverse (mime_types);

	return mime_types;
}

static GSList *
get_mime_types_from_file (GtkSourceLanguage *language)
{
	xmlTextReaderPtr reader;
	gint ret;
	GSList *mime_types = NULL;

	g_return_val_if_fail (GTK_IS_SOURCE_LANGUAGE (language), NULL);
	g_return_val_if_fail (language->priv->lang_file_name != NULL, NULL);
		
	reader = xmlNewTextReaderFilename (language->priv->lang_file_name);

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
			   				   language->priv->lang_file_name);

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
	            g_warning("Failed to parse '%s'", language->priv->lang_file_name);
		    return NULL;
		}
        }
	else 
	{
		g_warning("Unable to open '%s'", language->priv->lang_file_name);

    	}

	return mime_types;
}

void 
gtk_source_language_set_mime_types (GtkSourceLanguage	*language,
				    const GSList	*mime_types)
{
	g_return_if_fail (GTK_IS_SOURCE_LANGUAGE (language));
	g_return_if_fail (language->priv->mime_types != NULL);
		
	slist_deep_free (language->priv->mime_types);
	language->priv->mime_types = NULL;

	if (mime_types != NULL)
	{
		const GSList *l;

		/* Dup mime_types */
		l = mime_types;
		while (l != NULL)
		{
			language->priv->mime_types = g_slist_prepend (language->priv->mime_types,
							      g_strdup ((const gchar*)l->data));
			l = g_slist_next (l);
		}
	
		language->priv->mime_types = g_slist_reverse (language->priv->mime_types);
	}
	else
		language->priv->mime_types = get_mime_types_from_file (language);
}

/* Tags managment ------------------------------------------------------ */

static void 
tag_style_changed_cb (GtkSourceLanguage *language,
		      const gchar       *id,
		      GtkSourceTag	*tag)
{
	GtkSourceTagStyle *ts;
	gchar *tag_id;

	g_object_get (G_OBJECT (tag), "name", &tag_id, NULL);
	if (strcmp (tag_id, id) != 0)
	{
		g_free (tag_id);
		return;
	}
	g_free (tag_id);

	ts = gtk_source_language_get_tag_style (language, id);

	if (ts != NULL)
	{
		gtk_source_tag_set_style (GTK_SOURCE_TAG (tag), ts);
		gtk_source_tag_style_free (ts);
	}
}

GSList *
gtk_source_language_get_tags (GtkSourceLanguage *language)
{
	GSList *tag_list = NULL, *tmp;
	gboolean populate_styles_table = FALSE;

	g_return_val_if_fail (GTK_IS_SOURCE_LANGUAGE (language), NULL);
	
	if (language->priv->tag_id_to_style_name == NULL)
	{
		g_return_val_if_fail (language->priv->tag_id_to_style == NULL, NULL);

		language->priv->tag_id_to_style_name =
			g_hash_table_new_full ((GHashFunc)g_str_hash,
					       (GEqualFunc)g_str_equal,
					       (GDestroyNotify)g_free,
					       (GDestroyNotify)g_free);

		language->priv->tag_id_to_style =
			g_hash_table_new_full ((GHashFunc)g_str_hash,
					       (GEqualFunc)g_str_equal,
					       (GDestroyNotify)g_free,
					       (GDestroyNotify)gtk_source_tag_style_free);

		populate_styles_table = TRUE;
	}

	switch (language->priv->version)
	{
	case GTK_SOURCE_LANGUAGE_VERSION_1_0:
		_gtk_source_language_file_parse_version1 (language, 
		                                          &tag_list, 
							  NULL, 
							  populate_styles_table);
		break;
	case GTK_SOURCE_LANGUAGE_VERSION_2_0:
		_gtk_source_language_file_parse_version2 (language,
		                                          &tag_list,
							  NULL,
							  populate_styles_table);
		break;
	}
	
	for (tmp = tag_list; tmp; tmp = tmp->next)
	{
		GtkTextTag *tag = tmp->data;
		g_signal_connect_object (language, 
					 "tag_style_changed",
					 G_CALLBACK (tag_style_changed_cb),
					 tag,
					 0);
	}

	return tag_list;
}

static gboolean
gtk_source_language_lazy_init_hash_tables (GtkSourceLanguage *language)
{
	if (language->priv->tag_id_to_style_name == NULL)
	{
		GSList *list;

		g_return_val_if_fail (language->priv->tag_id_to_style == NULL, FALSE);

		list = gtk_source_language_get_tags (language);

		g_slist_foreach (list, (GFunc)g_object_unref, NULL);
		g_slist_free (list);
	
		g_return_val_if_fail (language->priv->tag_id_to_style_name != NULL, FALSE);
		g_return_val_if_fail (language->priv->tag_id_to_style != NULL, FALSE);
	}

	return TRUE;
}

static GtkSourceTagStyle *
gtk_source_language_get_tag_default_style (GtkSourceLanguage *language,
					   const gchar       *tag_id)
{
	const gchar *style_name;
	
	g_return_val_if_fail (GTK_IS_SOURCE_LANGUAGE (language), NULL);
	g_return_val_if_fail (tag_id != NULL, NULL);

	if (!gtk_source_language_lazy_init_hash_tables (language))
			return NULL;

	style_name = (const gchar*)g_hash_table_lookup (language->priv->tag_id_to_style_name,
							tag_id);

	if (style_name != NULL)
	{
		GtkSourceTagStyle *tmp;

		g_return_val_if_fail (language->priv->style_scheme != NULL, NULL);

		tmp = gtk_source_style_scheme_get_tag_style (language->priv->style_scheme,
							     style_name);
		if (tmp == NULL)
			return gtk_source_tag_style_copy (&normal_style);
		else
			return tmp;
	}
	else
		return gtk_source_tag_style_copy (&normal_style);
}

GtkSourceTagStyle *
gtk_source_language_get_tag_style (GtkSourceLanguage *language, 
				   const gchar       *tag_id)
{
	const GtkSourceTagStyle *style;

	g_return_val_if_fail (GTK_IS_SOURCE_LANGUAGE (language), NULL);
	g_return_val_if_fail (tag_id != NULL, NULL);

	if (!gtk_source_language_lazy_init_hash_tables (language))
		return NULL;

	style = (const GtkSourceTagStyle*)g_hash_table_lookup (language->priv->tag_id_to_style,
							       tag_id);

	if (style == NULL)
	{
		return gtk_source_language_get_tag_default_style (language, tag_id);
	}
	else
	{
		return gtk_source_tag_style_copy (style);
	}
}

/**
 * gtk_source_language_set_tag_style:
 * @language: a #GtkSourceLanguage.
 * @tag_id: the ID of a #GtkSourceTag
 * @style: a #GtkSourceTagStyle
 *
 * Set the @style of the tag whose ID is @tag_id. If @style is NULL
 * restore the default style.
 **/
void 
gtk_source_language_set_tag_style (GtkSourceLanguage       *language,
				   const gchar             *tag_id,
				   const GtkSourceTagStyle *style)
{
	g_return_if_fail (GTK_SOURCE_LANGUAGE (language));
	g_return_if_fail (tag_id != NULL);

	if (!gtk_source_language_lazy_init_hash_tables (language))
			return;	
	
	if (style != NULL)
	{
		GtkSourceTagStyle *ts;
		
		ts = gtk_source_tag_style_copy (style);

		g_hash_table_insert (language->priv->tag_id_to_style,
				     g_strdup (tag_id),
				     ts);
	}
	else
	{
		g_hash_table_remove (language->priv->tag_id_to_style,
				     tag_id);
	}

	g_signal_emit (G_OBJECT (language),
		       signals[TAG_STYLE_CHANGED], 
		       0, 
		       tag_id);
}

GtkSourceStyleScheme *
gtk_source_language_get_style_scheme (GtkSourceLanguage *language)
{
	g_return_val_if_fail (GTK_IS_SOURCE_LANGUAGE (language), NULL);

	return language->priv->style_scheme;

}

static gboolean
emit_tag_style_changed_signal (gpointer  key, 
			       gpointer  value,
			       gpointer  user_data)
{
	GtkSourceLanguage *language = GTK_SOURCE_LANGUAGE (user_data);

	g_signal_emit (G_OBJECT (language),
		       signals[TAG_STYLE_CHANGED], 
		       0, 
		       (gchar*)key);

	return TRUE;
}

static void
style_changed_cb (GtkSourceStyleScheme *scheme,
		  const gchar          *tag_id,
		  gpointer              user_data)
{
	GtkSourceLanguage *language = GTK_SOURCE_LANGUAGE (user_data);

	g_signal_emit (G_OBJECT (language),
		       signals[TAG_STYLE_CHANGED], 
		       0, 
		       tag_id);	
}

void 
gtk_source_language_set_style_scheme (GtkSourceLanguage    *language,
				      GtkSourceStyleScheme *scheme)
{	
	g_return_if_fail (GTK_IS_SOURCE_LANGUAGE (language));
	g_return_if_fail (GTK_IS_SOURCE_STYLE_SCHEME (scheme));
	g_return_if_fail (language->priv->style_scheme != NULL);

	if (language->priv->style_scheme == scheme)
		return;

	g_object_unref (language->priv->style_scheme);
	
	language->priv->style_scheme = scheme;
	g_object_ref (language->priv->style_scheme);

	if (!gtk_source_language_lazy_init_hash_tables (language))
		return;

	g_hash_table_foreach (language->priv->tag_id_to_style_name,
			      (GHFunc) emit_tag_style_changed_signal,
			      (gpointer) language);

	g_signal_connect (G_OBJECT (scheme), "style_changed",
			  G_CALLBACK (style_changed_cb), language);
}

/* Highlighting engine creation ------------------------------------------ */

GtkSourceEngine *
gtk_source_language_create_engine (GtkSourceLanguage *language)
{
	GtkSourceEngine *engine = NULL;

	switch (language->priv->version)
	{
	case GTK_SOURCE_LANGUAGE_VERSION_1_0:
		engine = gtk_source_simple_engine_new ();
		if (!_gtk_source_language_file_parse_version1 (language,
							       NULL,
							       GTK_SOURCE_SIMPLE_ENGINE (engine),
							       FALSE))
		{
			g_object_unref (engine);
			engine = NULL;
		}
		break;

	case GTK_SOURCE_LANGUAGE_VERSION_2_0:
		engine = gtk_source_stack_engine_new ();
		if (!_gtk_source_language_file_parse_version2 (language,
							       NULL,
							       GTK_SOURCE_STACK_ENGINE (engine),
							       FALSE))
		{
			g_object_unref (engine);
			engine = NULL;
		}
		break;
	}
	
	return engine;
}

