/*  gtksourcelanguage-parser-2.c
 *  Language specification parser for 2.0 version .lang files
 *
 *  Copyright (C) 2003 - Gustavo Giráldez <gustavo.giraldez@gmx.net>
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

#include <libxml/parser.h>
#include "gtksourceview-i18n.h"
#include "gtksourcebuffer.h"
#include "gtksourceregex.h"
#include "gtksourcetag.h"
#include "gtksourcelanguage.h"
#include "gtksourcelanguagesmanager.h"
#include "gtksourcelanguage-private.h"


typedef struct _ParserCtxt ParserCtxt;

struct _ParserCtxt {
	GtkSourceLanguage    *language;
	GHashTable           *tags;
	xmlDocPtr             doc;
	GtkSourceStackEngine *engine;
	gboolean              populate_styles_table;
};


static void
create_tag (GtkSourceLanguage *language,
            GHashTable        *tags,
	    const gchar       *id,
	    const gchar       *name,
	    const gchar       *parent,
	    gboolean           populate_tables)
{
	g_return_if_fail (language != NULL);

	/* insert the id into our id -> style name hash */
	if (populate_tables &&
	    !g_hash_table_lookup (language->priv->tag_id_to_style_name, id))
		g_hash_table_insert (language->priv->tag_id_to_style_name,
		                     g_strdup (id),
				     g_strdup (parent));

	/* create the tag */
	if (tags && !g_hash_table_lookup (tags, id))
	{
		GtkTextTag *tag;
		GtkSourceTagStyle *ts;

		/* if no user visible name was provided, try searching the
		   id in the style scheme */
		if (!name)
		{
			gchar *translated_name;
			translated_name = gtk_source_style_scheme_get_style_name (
				language->priv->style_scheme, id);
			if (translated_name)
			{
				tag = gtk_source_tag_new (id, translated_name);
				g_free (translated_name);
			}
			else
				/* use the id as the visible name as a last
				   resource */
				tag = gtk_source_tag_new (id, id);
		}
		else
		{
			tag = gtk_source_tag_new (id, name);
		}
		g_hash_table_insert (tags, g_strdup (id), tag);

		/* Get the tag style from the language (not the style scheme) since
		   it could be overriden there.  The language will lookup the style
		   in the style scheme if not. */
		ts = gtk_source_language_get_tag_style (language, id);
		if (ts)
		{
			gtk_source_tag_set_style (GTK_SOURCE_TAG (tag), ts);
			gtk_source_tag_style_free (ts);
		}
	}
}

static void
spew_xml_warning (xmlNodePtr   node,
                  const gchar *fmt,
		  ...)
{
	va_list args;
	gchar *str;

	va_start (args, fmt);
	str = g_strdup_vprintf (fmt, args);
	va_end (args);

	g_warning ("%s (%s:%ld)", 
	           str, 
		   node->doc ? node->doc->name : "(no document)", 
		   xmlGetLineNo (node));

	g_free (str);
}

static void
parse_style (ParserCtxt *ctxt,
	     xmlNodePtr  node)
{
	xmlChar *id;
	xmlChar *name;
	xmlChar *inherits;

	/* get new style id */
	id = xmlGetProp (node, BAD_CAST ("id"));
	if (!id)
	{
		spew_xml_warning (node, "Style tag requires id attribute");
		return;
	}

	/* get user visible name */
	name = xmlGetProp (node, BAD_CAST ("_name"));
	if (!name)
		name = xmlGetProp (node, BAD_CAST ("name"));
	else
	{
		xmlChar *tmp = xmlStrdup (
			dgettext (ctxt->language->priv->translation_domain, name));
		xmlFree (name);
		name = tmp;
	}

	/* inherited style */
	inherits = xmlGetProp (node, BAD_CAST ("inherits"));
	
	create_tag (ctxt->language, ctxt->tags, id, name, inherits, 
	            ctxt->populate_styles_table);

	if (inherits) xmlFree (inherits);
	xmlFree (name);
	xmlFree (id);
}

static GtkSourceStackAction
parse_action (const gchar *action, gchar **target_out)
{
	gchar *target = NULL;
	GtkSourceStackAction op = GTK_SOURCE_STACK_STAY;

	if (action)
	{
		gchar *opening_paren;
		opening_paren = g_utf8_strchr (action, -1, '(');
		if (opening_paren)
		{
			gchar *closing_paren;	
			closing_paren = g_utf8_strchr (opening_paren, -1, ')');
			if (!closing_paren || closing_paren - action != strlen (action) - 1)
			{
				/* syntax error */
				g_warning ("Syntax error while parsing action '%s'", action);
			}
			else
			{
				gboolean ok = TRUE;

				if (strncmp (action, "stay", opening_paren - action) == 0)
				{
					op = GTK_SOURCE_STACK_STAY;
					if (closing_paren - opening_paren != 1)
					{
						g_warning ("stay doesn't take parameters");
						ok = FALSE;
					}
				}
				else if (strncmp (action, "jump", opening_paren - action) == 0)
				{
					op = GTK_SOURCE_STACK_JUMP_TO_CONTEXT;
					if (closing_paren - opening_paren <= 1)
					{
						g_warning ("jump action needs a target parameter");
						ok = FALSE;
					}
				}
				else if (strncmp (action, "push", opening_paren - action) == 0)
				{
					op = GTK_SOURCE_STACK_PUSH_CONTEXT;
					if (closing_paren - opening_paren <= 1)
					{
						g_warning ("push action needs a target parameter");
						ok = FALSE;
					}
				}
				else if (strncmp (action, "pop", opening_paren - action) == 0)
				{
					op = GTK_SOURCE_STACK_POP_CONTEXT;
					if (closing_paren - opening_paren != 1)
					{
						g_warning ("pop doesn't take parameters");
						ok = FALSE;
					}
				}
				else
				{
					g_warning ("Unkwown action type '%s'", action);
					ok = FALSE;
				}

				if (ok)
				{
					target = g_malloc (closing_paren - opening_paren);
					strncpy (target, opening_paren + 1, 
					         closing_paren - opening_paren - 1);
				}
				else
					op = GTK_SOURCE_STACK_STAY;
			}
		}
		else
		{
			/* consider the whole action as a jump to another context */
			target = g_strdup (action);
			op = GTK_SOURCE_STACK_JUMP_TO_CONTEXT;
		}
	}
	
	if (target_out)
		*target_out = target;
	else
		g_free (target);

	return op;
}

static void
parse_regex (ParserCtxt *ctxt,
             xmlNodePtr  node,
	     xmlChar    *id,
	     gboolean    is_string)
{
	xmlChar *regex_id;
	xmlChar *style;
	xmlChar *action;
	xmlChar *text;

	text = xmlGetProp (node, BAD_CAST ("text"));
	if (!text)
	{
		spew_xml_warning (node, "Attribute 'text' is required");
		return;
	}

	/* optional attributes */
	regex_id = xmlGetProp (node, BAD_CAST ("id"));
	style = xmlGetProp (node, BAD_CAST ("style"));
	action = xmlGetProp (node, BAD_CAST ("action"));

	if (style && (ctxt->tags || ctxt->populate_styles_table))
		create_tag (ctxt->language, ctxt->tags, 
		            style, NULL, NULL, 
			    ctxt->populate_styles_table);

	if (ctxt->engine)
	{
		gchar *regex_text;
		gchar *target_ctxt = NULL;
		GtkSourceStackAction action_id;

		if (is_string)
			regex_text = gtk_source_regex_escape_string (text);
		else
			regex_text = g_strdup (text);

		action_id = parse_action (action, &target_ctxt);

		/* create match id */
		if (!gtk_source_stack_engine_add_regex (ctxt->engine,
							id,
							regex_id,
							style,
							regex_text,
							action_id,
							target_ctxt))
		{
			spew_xml_warning (node, "Failed to add regular expression "
			                  "entry for context %s", id);
		}
		g_free (regex_text);
		g_free (target_ctxt);
	}

	xmlFree (text);
	if (action) xmlFree (action);
	if (style) xmlFree (style);
	if (regex_id) xmlFree (regex_id);
}

static void
parse_context (ParserCtxt  *ctxt,
	       xmlNodePtr   node)
{
	xmlChar *id;
	xmlChar *style;

	id = xmlGetProp (node, BAD_CAST ("id"));
	if (!id)
	{
		spew_xml_warning (node, "The context tag needs an id");
		return;
	}

	style = xmlGetProp (node, BAD_CAST ("style"));

	if (style && (ctxt->tags || ctxt->populate_styles_table))
		create_tag (ctxt->language, ctxt->tags, 
		            style, NULL, NULL, 
			    ctxt->populate_styles_table);

	/* create the context in the engine */
	if (ctxt->engine)
	{
		if (!gtk_source_stack_engine_create_context (ctxt->engine,
							     id,
							     style))
		{
			spew_xml_warning (node, "Failed to create context %s", id);
		}
	}
	
	node = node->xmlChildrenNode;
	while (node)
	{
		/* parse string and regex tags */
		if (xmlStrEqual (node->name, BAD_CAST ("string")))
		{
			parse_regex (ctxt, node, id, TRUE);
		}
		else if (xmlStrEqual (node->name, BAD_CAST ("regex")))
		{
			parse_regex (ctxt, node, id, FALSE);
		}
		else
		{
			spew_xml_warning (node, "Unkwown tag '%s' while parsing context '%s'",
			                  node->name, id);
		}
		node = node->next;
	}

	if (style) xmlFree (style);
	xmlFree (id);
}

static gboolean
build_tag_list (gpointer key,
		gpointer value,
		gpointer user_data)
{
	GSList **tags = user_data;
	GtkSourceTag *tag = value;

	*tags = g_slist_prepend (*tags, tag);

	return TRUE;
}

gboolean 
_gtk_source_language_file_parse_version2 (GtkSourceLanguage     *language,
					  GSList               **tags,
					  GtkSourceStackEngine  *engine,
					  gboolean               populate_styles_table)
{
	xmlDocPtr doc;
	xmlNodePtr cur;
	GHashTable *htags = NULL;
	ParserCtxt ctxt;

	if (!tags && !engine)
		return FALSE;

	xmlKeepBlanksDefault (0);
	xmlLineNumbersDefault (1);

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
	
	if (tags)
	{
		/* use this hash table so we don't get duplicate style ids */
		htags = g_hash_table_new_full (g_str_hash, g_str_equal,
		                               (GDestroyNotify) g_free,
					       NULL);
	}

	ctxt.language = language;
	ctxt.tags = htags;
	ctxt.engine = engine;
	ctxt.doc = doc;
	ctxt.populate_styles_table = populate_styles_table;
				
	while (cur != NULL)
	{
		if (xmlStrEqual (cur->name, BAD_CAST ("style")))
		{
			if (tags || populate_styles_table)
				parse_style (&ctxt, cur);
		}
		else if (xmlStrEqual (cur->name, BAD_CAST ("context")))
		{
			parse_context (&ctxt, cur);
		}
		else
		{
			spew_xml_warning (cur, "Unkwown tag '%s'", cur->name);
			break;
		}
		
		cur = cur->next;
	}

	if (tags)
	{
		g_hash_table_foreach (htags, (GHFunc) build_tag_list, tags);
		g_hash_table_destroy (htags);
	}
      
	xmlFreeDoc (doc);

	return TRUE;
}

