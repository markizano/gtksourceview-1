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

/*
 * This function has been taken from GConf.
 * Copyright (C) 1999, 2000 Red Hat Inc.
 */

static const gchar invalid_chars[] = " \t\r\n\"$&<>,+=#!()'|{}[]?~`;%\\";

/**
 * escape_id:
 * @arbitrary_text: some text in any encoding or format
 * @len: length of @arbitrary_text in bytes, or -1 if @arbitrary_text is nul-terminated
 * 
 * Return value: a nul-terminated valid part of a GConf key
 **/
static gchar*
escape_id (const gchar *arbitrary_text, gint len)
{
	const char *p;
	const char *end;
	GString *retval;

	g_return_val_if_fail (arbitrary_text != NULL, NULL);
  
	/* Nearly all characters we would normally use for escaping aren't allowed in key
	 * names, so we use @ for that.
	 *
	 * Invalid chars and @ itself are escaped as @xxx@ where xxx is the
	 * Latin-1 value in decimal
	 */

	if (len < 0)
		len = strlen (arbitrary_text);

	retval = g_string_new ("");

	p = arbitrary_text;
	end = arbitrary_text + len;
	while (p != end)
	{
		if (*p == '/' || *p == '.' || *p == '@' || ((guchar) *p) > 127 ||
			strchr (invalid_chars, *p))
		{
			g_string_append_c (retval, '@');
			g_string_append_printf (retval, "%u", (unsigned int) *p);
			g_string_append_c (retval, '@');
		}
		else
			g_string_append_c (retval, *p);
      
		++p;
	}

	return g_string_free (retval, FALSE);
}

static GtkSourceLanguage *
process_language_node (xmlTextReaderPtr reader, const gchar *filename)
{
	gchar *version;
	gchar *mimetypes;
	gchar** mtl;
	int i;
	xmlChar *tmp;
	xmlChar *id_temp;
	GtkSourceLanguage *lang;
	
	lang= g_object_new (GTK_TYPE_SOURCE_LANGUAGE, NULL);

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

		id_temp = xmlStrdup (lang->priv->name);
	}
	else
	{
		id_temp = xmlStrdup (tmp);
		lang->priv->name = xmlStrdup (dgettext (lang->priv->translation_domain, tmp));
		xmlFree (tmp);
	}

	g_return_val_if_fail (id_temp != NULL, NULL);
	lang->priv->id = escape_id (id_temp, -1);
	xmlFree (id_temp);

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

static gchar * 
build_keyword_list (const gchar  *id,
		    const GSList *keywords,
		    gboolean      case_sensitive,
		    gboolean      match_empty_string_at_beginning,
		    gboolean      match_empty_string_at_end,
		    const gchar  *beginning_regex,
		    const gchar  *end_regex)
{
	GString *str;
	gint keyword_count;
	
	g_return_val_if_fail (keywords != NULL, NULL);

	str =  g_string_new ("");

	if (keywords != NULL)
	{
		if (match_empty_string_at_beginning)
			g_string_append (str, "\\b");

		if (beginning_regex != NULL)
			g_string_append (str, beginning_regex);

		g_string_append (str, "(");
	
		keyword_count = 0;
		/* Due to a bug in GNU libc regular expressions
		 * implementation we can't have keyword lists of more
		 * than 250 or so elements, so we truncate such a
		 * list.  This is a temporary solution, as the correct
		 * approach would be to generate multiple keyword
		 * lists.  (See bug #110991) */

#define KEYWORD_LIMIT 250

		while (keywords != NULL && keyword_count < KEYWORD_LIMIT)
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
			
			keyword_count++;
			
			if (keywords != NULL && keyword_count < KEYWORD_LIMIT)
				g_string_append (str, "|");
		}
		g_string_append (str, ")");
		
		if (keyword_count >= KEYWORD_LIMIT)
		{
			g_warning ("Keyword list '%s' too long. Only the first %d "
				   "elements will be highlighted. See bug #110991 for "
				   "further details.", id, KEYWORD_LIMIT);
		}

		if (end_regex != NULL)
			g_string_append (str, end_regex);
		
		if (match_empty_string_at_end)
			g_string_append (str, "\\b");
	}

	return g_string_free (str, FALSE);
}

static void 
parseLineComment (xmlDocPtr              doc,
		  xmlNodePtr             cur,
		  gchar                 *id,
		  xmlChar               *style,
		  GtkSourceSimpleEngine *se)
{
	xmlNodePtr child;

	child = cur->xmlChildrenNode;
		
	if ((child != NULL) && !xmlStrcmp (child->name, (const xmlChar *)"start-regex"))
	{
		xmlChar *start_regex;
			
		start_regex = xmlNodeListGetString (doc, child->xmlChildrenNode, 1);
			
		gtk_source_simple_engine_add_syntax_pattern (se, id, style,
							     strconvescape (start_regex), 
							     "\n");

		xmlFree (start_regex);
	}
	else
	{
		g_warning ("Missing start-regex in tag 'line-comment' (%s, line %ld)", 
			   doc->name, xmlGetLineNo (child));
	}
}

static void
parseBlockComment (xmlDocPtr              doc,
		   xmlNodePtr             cur,
		   gchar                 *id,
		   xmlChar               *style,
		   GtkSourceSimpleEngine *se)
{
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

		return;
	}

	if (end_regex == NULL)
	{
		xmlFree (start_regex);

		g_warning ("Missing end-regex in tag 'block-comment' (%s, line %ld)", 
			   doc->name, xmlGetLineNo (cur));

		return;
	}

	gtk_source_simple_engine_add_syntax_pattern (se, id, style,
						     strconvescape (start_regex), 
						     strconvescape (end_regex));
	
	xmlFree (start_regex);
	xmlFree (end_regex);
}

static void
parseString (xmlDocPtr              doc,
	     xmlNodePtr             cur,
	     gchar                 *id,
	     xmlChar               *style,
	     GtkSourceSimpleEngine *se)
{
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

		return;
	}

	if (end_regex == NULL)
	{
		xmlFree (start_regex);

		g_warning ("Missing end-regex in tag 'string' (%s, line %ld)", 
			   doc->name, xmlGetLineNo (cur));

		return;
	}

	strconvescape (start_regex);
	strconvescape (end_regex);
	
	if (!end_at_line_end)
		gtk_source_simple_engine_add_syntax_pattern (se, id, style,
							     start_regex,
							     end_regex);
	else
	{
		gchar *end;
		
		end = g_strdup_printf ("%s|\n", end_regex);

		gtk_source_simple_engine_add_syntax_pattern (se, id, style,
							     start_regex,
							     end);
		
		g_free (end);
	}

	xmlFree (start_regex);
	xmlFree (end_regex);
}

static void
parseKeywordList (xmlDocPtr              doc,
		  xmlNodePtr             cur,
		  gchar                 *id,
		  xmlChar               *style,
		  GtkSourceSimpleEngine *se)
{
	gboolean case_sensitive = TRUE;
	gboolean match_empty_string_at_beginning = TRUE;
	gboolean match_empty_string_at_end = TRUE;
	gchar  *beginning_regex = NULL;
	gchar  *end_regex = NULL;

	GSList *list = NULL;
	gchar *regex;
	
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
		
		return;
	}

	regex = build_keyword_list (id,
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

	gtk_source_simple_engine_add_simple_pattern (se, id, style, regex);

	g_free (regex);
}

static void 
parsePatternItem (xmlDocPtr              doc,
		  xmlNodePtr             cur,
		  gchar                 *id,
		  xmlChar               *style,
		  GtkSourceSimpleEngine *se)
{
	xmlNodePtr child;

	child = cur->xmlChildrenNode;
		
	if ((child != NULL) && !xmlStrcmp (child->name, (const xmlChar *)"regex"))
	{
		xmlChar *regex;
			
		regex = xmlNodeListGetString (doc, child->xmlChildrenNode, 1);
			
		gtk_source_simple_engine_add_simple_pattern (se, id, style,
							     strconvescape (regex));

		xmlFree (regex);
	}
	else
	{
		g_warning ("Missing regex in tag 'pattern-item' (%s, line %ld)", 
			   doc->name, xmlGetLineNo (child));
	}
}

static void 
parseSyntaxItem (xmlDocPtr              doc,
		 xmlNodePtr             cur,
		 const gchar           *id,
		 xmlChar               *style,
		 GtkSourceSimpleEngine *se)
{
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
		g_warning ("Missing start-regex in tag 'syntax-item' (%s, line %ld)", 
			   doc->name, xmlGetLineNo (cur));

		return;
	}

	if (end_regex == NULL)
	{
		xmlFree (start_regex);

		g_warning ("Missing end-regex in tag 'syntax-item' (%s, line %ld)", 
			   doc->name, xmlGetLineNo (cur));

		return;
	}

	gtk_source_simple_engine_add_syntax_pattern (se, id, style,
						     strconvescape (start_regex),
						     strconvescape (end_regex));

	xmlFree (start_regex);
	xmlFree (end_regex);
}

static void 
parseTag (GtkSourceLanguage     *language,
	  xmlDocPtr              doc,
	  xmlNodePtr             cur,
	  GSList               **tags,
	  GtkSourceSimpleEngine *engine,
	  gboolean               populate_styles_table)
{
	xmlChar *name;
	xmlChar *style;
	xmlChar *id_temp;
	gchar   *id;
	
	name = xmlGetProp (cur, "_name");
	if (name == NULL)
	{
		name = xmlGetProp (cur, "name");
		id_temp =xmlStrdup (name);
	}
	else
	{
		xmlChar *tmp = xmlStrdup (dgettext (language->priv->translation_domain, name));
		id_temp = xmlStrdup (name);
		xmlFree (name);
		name = tmp;
	}
	
	style = xmlGetProp (cur, "style");

	if (name == NULL)
	{
		return;
	}

	g_return_if_fail (id_temp != NULL);
	id = escape_id (id_temp, -1);
	xmlFree (id_temp);

	if (style == NULL)
	{
		/* FIXME */
		style = xmlStrdup ("Normal");
	}
	
	if (engine)
	{
		if (!xmlStrcmp (cur->name, (const xmlChar *)"line-comment"))
		{
			parseLineComment (doc, cur, id, style, engine);
		}
		else if (!xmlStrcmp (cur->name, (const xmlChar *)"block-comment"))
		{
			parseBlockComment (doc, cur, id, style, engine);		
		}
		else if (!xmlStrcmp (cur->name, (const xmlChar *)"string"))
		{
			parseString (doc, cur, id, style, engine);
		}
		else if (!xmlStrcmp (cur->name, (const xmlChar *)"keyword-list"))
		{
			parseKeywordList (doc, cur, id, style, engine);
		}
		else if (!xmlStrcmp (cur->name, (const xmlChar *)"pattern-item"))
		{
			parsePatternItem (doc, cur, id, style, engine);
		}
		else if (!xmlStrcmp (cur->name, (const xmlChar *)"syntax-item"))
		{
			parseSyntaxItem (doc, cur, id, style, engine);
		}
		else
		{
			g_print ("Unknown tag: %s\n", cur->name);
		}
	}
	
	if (populate_styles_table)
		g_hash_table_insert (language->priv->tag_id_to_style_name, 
				     g_strdup (id), 
				     g_strdup (style));

	if (tags)
	{
		GtkTextTag *tag;
		GtkSourceTagStyle *ts;

		tag = gtk_source_tag_new (id, name);
		*tags = g_slist_prepend (*tags, tag);

		ts = gtk_source_language_get_tag_style (language, id);

		if (ts != NULL)
		{
			gtk_source_tag_set_style (GTK_SOURCE_TAG (tag), ts);
			gtk_source_tag_style_free (ts);
		}
		
		g_signal_connect_object (language, 
					 "tag_style_changed",
					 G_CALLBACK (tag_style_changed_cb),
					 tag,
					 0);
	}
		
	xmlFree (name);
	xmlFree (style);
	g_free (id);
}

static gboolean 
language_file_parse (GtkSourceLanguage     *language,
		     GSList               **tags,
		     GtkSourceSimpleEngine *engine,
		     gboolean               populate_styles_table)
{
	xmlDocPtr doc;
	xmlNodePtr cur;

	xmlKeepBlanksDefault (0);

	doc = xmlParseFile (language->priv->lang_file_name);
	if (doc == NULL)
	{
		g_warning ("Impossible to parse file '%s'",
			   language->priv->lang_file_name);
		return FALSE;
	}

	cur = xmlDocGetRootElement (doc);
	
	if (cur == NULL) 
	{
		g_warning ("The lang file '%s' is empty",
			   language->priv->lang_file_name);

		xmlFreeDoc (doc);
		return FALSE;
	}

	if (xmlStrcmp (cur->name, (const xmlChar *) "language")) {
		g_warning ("File '%s' is of the wrong type",
			   language->priv->lang_file_name);
		
		xmlFreeDoc (doc);
		return FALSE;
	}

	/* FIXME: check that the language name, version, etc. are the 
	 * right ones - Paolo */

	cur = xmlDocGetRootElement (doc);
	cur = cur->xmlChildrenNode;
	g_return_val_if_fail (cur != NULL, FALSE);
	
	while (cur != NULL)
	{
		if (!xmlStrcmp (cur->name, (const xmlChar *)"escape-char"))
		{
			xmlChar *escape;
			gunichar esc_char;
			
			escape = xmlNodeListGetString (doc, cur->xmlChildrenNode, 1);
			esc_char = g_utf8_get_char_validated (escape, -1);
			if (esc_char < 0)
			{
				g_warning ("Invalid (non UTF8) escape character in file '%s'",
					   language->priv->lang_file_name);
				esc_char = 0;
			}
			xmlFree (escape);
			language->priv->escape_char = esc_char;

			if (engine)
				gtk_source_simple_engine_set_escape_char (engine, esc_char);
			
			if (!tags && !engine)
				break;
		}
		else if (tags || engine)
		{
			parseTag (language,
				  doc, 
				  cur, 
				  tags,
				  engine,
				  populate_styles_table);
		}
		
		cur = cur->next;
	}
	language->priv->escape_char_valid = TRUE;

	if (tags)
		*tags = g_slist_reverse (*tags);
      
	xmlFreeDoc (doc);

	return TRUE;
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
	GSList *tag_list = NULL;
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

	if (language->priv->version == GTK_SOURCE_LANGUAGE_VERSION_1_0)
		language_file_parse (language, &tag_list, NULL, populate_styles_table);
	
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

GtkSourceTagStyle*
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
		const GtkSourceTagStyle *tmp;

		g_return_val_if_fail (language->priv->style_scheme != NULL, NULL);

		tmp = gtk_source_style_scheme_get_tag_style (language->priv->style_scheme,
							     style_name);
		if (tmp == NULL)
			return NULL;
		else
			return gtk_source_tag_style_copy (tmp);
	}
	else
		return NULL;
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

	if (language->priv->version == GTK_SOURCE_LANGUAGE_VERSION_1_0)
	{
		engine = gtk_source_simple_engine_new ();
		if (!language_file_parse (language,
					  NULL,
					  GTK_SOURCE_SIMPLE_ENGINE (engine),
					  FALSE))
		{
			g_object_unref (engine);
			engine = NULL;
		}
	}
	
	return engine;
}

