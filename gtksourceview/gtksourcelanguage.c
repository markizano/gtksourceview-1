/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8; coding: utf-8 -*-
 *  gtksourcelanguage.c
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

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <string.h>
#include <fcntl.h>

#include <libxml/xmlreader.h>
#include <glib/gstdio.h>
#include "gtksourceview-i18n.h"
#include "gtksourcelanguage-private.h"
#include "gtksourcelanguage.h"
#include "gtksourceview-marshal.h"


G_DEFINE_TYPE (GtkSourceLanguage, gtk_source_language, G_TYPE_OBJECT)


static void		  gtk_source_language_finalize 		(GObject 			*object);

static GtkSourceLanguage *process_language_node 		(xmlTextReaderPtr 		 reader,
								 const gchar 			*filename);
static GSList 		 *get_mime_types_from_file 		(GtkSourceLanguage 		*language);

/* Signals */
enum {
	LAST_SIGNAL
};

// static guint 	 signals[LAST_SIGNAL] = { 0 };


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

	xmlTextReaderPtr reader = NULL;
	gint ret;
	int fd;

	g_return_val_if_fail (filename != NULL, NULL);
	g_return_val_if_fail (lm != NULL, NULL);

	/*
	 * Use fd instead of filename so that it's utf8 safe on w32.
	 */
	fd = g_open (filename, O_RDONLY, 0);
	if (fd != -1)
		reader = xmlReaderForFd (fd, filename, NULL, 0);

	if (reader != NULL)
	{
        	ret = xmlTextReaderRead (reader);

        	while (ret == 1)
		{
			if (xmlTextReaderNodeType (reader) == 1)
			{
				xmlChar *name;

				name = xmlTextReaderName (reader);

				if (xmlStrcmp (name, BAD_CAST "language") == 0)
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
		close (fd);

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

	lang->priv->languages_manager = lm;

	return lang;
}

static void
gtk_source_language_class_init (GtkSourceLanguageClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize	= gtk_source_language_finalize;
}

static void
gtk_source_language_init (GtkSourceLanguage *lang)
{
	lang->priv = g_new0 (GtkSourceLanguagePrivate, 1);
	lang->priv->styles = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
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
		xmlFree (lang->priv->id);

		slist_deep_free (lang->priv->mime_types);
		g_hash_table_destroy (lang->priv->styles);

		g_free (lang->priv);
	}

	G_OBJECT_CLASS (gtk_source_language_parent_class)->finalize (object);
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
	xmlChar *version;
	xmlChar *mimetypes;
	gchar** mtl;
	int i;
	xmlChar *tmp;
	xmlChar *untranslated_name;
	GtkSourceLanguage *lang;

	lang = g_object_new (GTK_TYPE_SOURCE_LANGUAGE, NULL);

	lang->priv->lang_file_name = g_strdup (filename);

	lang->priv->translation_domain = (gchar *) xmlTextReaderGetAttribute (
			reader, BAD_CAST "translation-domain");
	if (lang->priv->translation_domain == NULL)
	{
		/* if the attribute "translation-domain" exists then
		 * lang->priv->translation_domain is a xmlChar so it must always
		 * be a xmlChar, this is why xmlStrdup() is used instead of
		 * g_strdup() */
		lang->priv->translation_domain = (gchar *)xmlStrdup (BAD_CAST GETTEXT_PACKAGE);
	}

	tmp = xmlTextReaderGetAttribute (reader, BAD_CAST "_name");
	if (tmp == NULL)
	{
		lang->priv->name = (gchar *)xmlTextReaderGetAttribute (reader,
					BAD_CAST "name");
		untranslated_name = xmlStrdup (BAD_CAST lang->priv->name);
		if (lang->priv->name == NULL)
		{
			g_warning ("Impossible to get language name from file '%s'",
				   filename);

			g_object_unref (lang);
			return NULL;
		}
	}
	else
	{
		untranslated_name = tmp;
		/* if tmp is NULL then lang->priv->name is a xmlChar so it must
		 * always be a xmlChar, this is why xmlStrdup() is used instead
		 * of g_strdup() */
		lang->priv->name = (gchar *)xmlStrdup (BAD_CAST dgettext (
					lang->priv->translation_domain,
					(gchar *)tmp));
	}

	tmp = xmlTextReaderGetAttribute (reader, BAD_CAST "id");
	if (tmp != NULL)
	{
		lang->priv->id = (gchar *) tmp;
		xmlFree (untranslated_name);
	}
	else
	{
		lang->priv->id = (gchar *) untranslated_name;
	}

	tmp = xmlTextReaderGetAttribute (reader, BAD_CAST "_section");
	if (tmp == NULL)
	{
		lang->priv->section = (gchar *)xmlTextReaderGetAttribute (reader, BAD_CAST "section");
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
		/* if tmp is NULL then lang->priv->section is a xmlChar so it
		 * must always be a xmlChar, this is why xmlStrdup() is used
		 * instead of g_strdup() */
		lang->priv->section = (gchar *)xmlStrdup (BAD_CAST dgettext (
					lang->priv->translation_domain,
					(gchar *)tmp));
		xmlFree (tmp);
	}

	version = xmlTextReaderGetAttribute (reader, BAD_CAST "version");
	if (version == NULL)
	{
		g_warning ("Impossible to get version number from file '%s'",
			   filename);

		g_object_unref (lang);
		return NULL;
	}
	else
	{
		if (xmlStrcmp (version , BAD_CAST "1.0") == 0)
		{
			lang->priv->version = GTK_SOURCE_LANGUAGE_VERSION_1_0;
		}
		else if (xmlStrcmp (version, BAD_CAST "2.0") == 0)
		{
			lang->priv->version = GTK_SOURCE_LANGUAGE_VERSION_2_0;
		}
		else
		{
			g_warning ("Usupported language spec version '%s' in file '%s'",
				   (gchar *)version, filename);

			xmlFree (version);

			g_object_unref (lang);
			return NULL;
		}

		xmlFree (version);
	}

	mimetypes = xmlTextReaderGetAttribute (reader, BAD_CAST "mimetypes");
	if (mimetypes == NULL)
	{
		g_warning ("Impossible to get mimetypes from file '%s'",
			   filename);

		g_object_unref (lang);
		return NULL;
	}

	mtl = g_strsplit ((gchar *)mimetypes, ";", 0);

	for (i = 0; mtl[i] != NULL; i++)
	{
		/* steal the strings from the array */
		lang->priv->mime_types = g_slist_prepend (lang->priv->mime_types,
				g_strdup (mtl[i]));

	}

	g_free (mtl);
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
 * Markup section).
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

/**
 * gtk_source_language_get_mime_types:
 * @language: a #GtkSourceLanguage.
 *
 * Returns a list of mime types for the given @language.  After usage you should
 * free each element of the list as well as the list itself.
 *
 * Return value: a list of mime types (strings).
 **/
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

/**
 * _gtk_source_language_get_languages_manager:
 * @language: a #GtkSourceLanguage.
 *
 * Returns the #GtkSourceLanguagesManager for the #GtkSourceLanguage.
 *
 * Return value: #GtkSourceLanguagesManager for @language.
 **/
GtkSourceLanguagesManager *
_gtk_source_language_get_languages_manager (GtkSourceLanguage *language)
{
	g_return_val_if_fail (GTK_IS_SOURCE_LANGUAGE (language), NULL);
	g_return_val_if_fail (language->priv->id != NULL, NULL);

	return language->priv->languages_manager;
}

static GSList *
get_mime_types_from_file (GtkSourceLanguage *language)
{
	xmlTextReaderPtr reader = NULL;
	gint ret;
	GSList *mime_types = NULL;
	int fd;

	g_return_val_if_fail (GTK_IS_SOURCE_LANGUAGE (language), NULL);
	g_return_val_if_fail (language->priv->lang_file_name != NULL, NULL);

	/*
	 * Use fd instead of filename so that it's utf8 safe on w32.
	 */
	fd = g_open (language->priv->lang_file_name, O_RDONLY, 0);
	if (fd != -1)
		reader = xmlReaderForFd (fd, language->priv->lang_file_name, NULL, 0);

	if (reader != NULL)
	{
        	ret = xmlTextReaderRead (reader);

        	while (ret == 1)
		{
			if (xmlTextReaderNodeType (reader) == 1)
			{
				xmlChar *name;

				name = xmlTextReaderName (reader);

				if (xmlStrcmp (name, BAD_CAST "language") == 0)
				{
					gchar *mimetypes;

					mimetypes = (gchar *)xmlTextReaderGetAttribute (reader,
							BAD_CAST "mimetypes");

					if (mimetypes == NULL)
					{
						g_warning ("Impossible to get mimetypes from file '%s'",
			   				   language->priv->lang_file_name);

						ret = 0;
					}
					else
					{
						gchar **mtl;
						gint i;

						mtl = g_strsplit (mimetypes, ";", 0);

						for (i = 0; mtl[i] != NULL; i++)
						{
							/* steal the strings from the array */
							mime_types = g_slist_prepend (mime_types,
										      mtl[i]);
						}

						g_free (mtl);
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
		close (fd);

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

/**
 * gtk_source_language_set_mime_types:
 * @language: a #GtkSourceLanguage
 * @mime_types: a list of mime types (strings).
 *
 * Sets a list of @mime_types for the given @language.
 * If @mime_types is %NULL this function will use the default mime
 * types from the language file.
 **/
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

/* Highlighting engine creation ------------------------------------------ */

GtkSourceEngine *
_gtk_source_language_create_engine (GtkSourceLanguage *language)
{
	GtkSourceEngine *engine = NULL;

	switch (language->priv->version)
	{
	case GTK_SOURCE_LANGUAGE_VERSION_1_0:
		g_return_val_if_reached (NULL);
		break;

	case GTK_SOURCE_LANGUAGE_VERSION_2_0:
		engine = _gtk_source_context_engine_new (language);
		if (!_gtk_source_language_file_parse_version2 (language,
							       GTK_SOURCE_CONTEXT_ENGINE (engine)))
		{
			g_object_unref (engine);
			engine = NULL;
		}
		break;
	}

	return engine;
}
