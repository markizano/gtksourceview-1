/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8; coding: utf-8 -*- 
 *  gtksourcestackengine.c
 *
 *  Copyright (C) 2003 - Gustavo Giráldez <gustavo.giraldez@gmx.net>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <gtk/gtk.h>

#include "gtksourceview-i18n.h"
#include "gtksourcetag.h"
#include "gtksourcebuffer.h"
#include "gtksourcestackengine.h"
#include "gtksourceregex.h"
#include "gtktextregion.h"

#define ENABLE_DEBUG
#define ENABLE_PROFILE

#ifdef ENABLE_DEBUG
#define DEBUG(x) (x)
#else
#define DEBUG(x)
#endif

#ifdef ENABLE_PROFILE
#define PROFILE(x) (x)
#else
#define PROFILE(x)
#endif

/* define this to always highlight in an idle handler, and not
 * possibly in the expose method of the view */
#undef LAZIEST_MODE

/* in milliseconds */
#define WORKER_TIME_SLICE                   30
#define INITIAL_WORKER_BATCH                40960
#define MINIMUM_WORKER_BATCH                1024

#define STACK_ENGINE_GET_PRIVATE(obj)  (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GTK_TYPE_SOURCE_STACK_ENGINE, GtkSourceStackEnginePrivate))

typedef struct _RegexItem                   RegexItem;
typedef struct _SyntacticContext            SyntacticContext;
typedef struct _GtkSourceStackEnginePrivate GtkSourceStackEnginePrivate;

struct _RegexItem
{
	gchar                 *id;       /* id might be NULL */

	gchar                 *style;

	gchar                 *regex;
	GtkSourceRegex        *compiled_regex;

	GtkSourceStackAction   action;
	gchar                 *target_id;
};

struct _SyntacticContext
{
	gchar                 *id;
	gchar                 *style;

	GList                 *switching_regexs;
	GList                 *normal_regexs;
};

struct _GtkSourceStackEnginePrivate 
{
	/* syntactic contexts */
	GHashTable            *contexts;

	/* Start of buffer dependant data --------------------------- */
	GtkSourceBuffer       *buffer;

	/* whether or not to actually highlight the buffer */
	gboolean               highlight;
	
	/* Region covering the unhighlighted text */
	GtkTextRegion         *refresh_region;

	/* views highlight requests */
	GtkTextRegion         *highlight_requests;
};



static GtkSourceEngineClass *parent_class = NULL;

static void   gtk_source_stack_engine_class_init           (GtkSourceStackEngineClass  *klass);
static void   gtk_source_stack_engine_init                 (GtkSourceStackEngine       *engine);
static void   gtk_source_stack_engine_finalize             (GObject                    *object);

static void   gtk_source_stack_engine_attach_buffer        (GtkSourceEngine            *engine,
							    GtkSourceBuffer            *buffer);

static void
regex_item_destroy (RegexItem *item)
{
	g_free (item->id);
	g_free (item->style);
	g_free (item->regex);
	gtk_source_regex_destroy (item->compiled_regex);
	g_free (item->target_id);

	g_free (item);
}

static gint
regex_item_id_compare (gconstpointer a, gconstpointer b)
{
	const RegexItem *ra;
	ra = (RegexItem *) a;

	if (ra->id == NULL)
		return -1;

	return strcmp (ra->id, (const gchar *) b);
}

static void
syntactic_context_destroy (SyntacticContext *ctxt)
{
	g_free (ctxt->id);
	g_free (ctxt->style);

	g_list_foreach (ctxt->switching_regexs, (GFunc) regex_item_destroy, NULL);
	g_list_foreach (ctxt->normal_regexs, (GFunc) regex_item_destroy, NULL);
	g_list_free (ctxt->switching_regexs);
	g_list_free (ctxt->normal_regexs);

	g_free (ctxt);
}

GType
gtk_source_stack_engine_get_type (void)
{
	static GType our_type = 0;

	if (!our_type) {
		static const GTypeInfo our_info = {
			sizeof (GtkSourceStackEngineClass),
			NULL,	/* base_init */
			NULL,	/* base_finalize */
			(GClassInitFunc) gtk_source_stack_engine_class_init,
			NULL,	/* class_finalize */
			NULL,	/* class_data */
			sizeof (GtkSourceStackEngine),
			0,	/* n_preallocs */
			(GInstanceInitFunc) gtk_source_stack_engine_init
		};

		our_type = g_type_register_static (GTK_TYPE_SOURCE_ENGINE,
						   "GtkSourceStackEngine",
						   &our_info, 
						   0);
	}
	
	return our_type;
}


/* Class and object lifecycle ------------------------------------------- */

static void
gtk_source_stack_engine_class_init (GtkSourceStackEngineClass *klass)
{
	GObjectClass         *object_class;
	GtkSourceEngineClass *engine_class;

	object_class 	= G_OBJECT_CLASS (klass);
	parent_class 	= g_type_class_peek_parent (klass);
	engine_class	= GTK_SOURCE_ENGINE_CLASS (klass);
		
	object_class->finalize	       = gtk_source_stack_engine_finalize;

	engine_class->attach_buffer    = gtk_source_stack_engine_attach_buffer;

	g_type_class_add_private (object_class, sizeof (GtkSourceStackEnginePrivate));
}

static void
gtk_source_stack_engine_init (GtkSourceStackEngine *engine)
{
	GtkSourceStackEnginePrivate *priv = STACK_ENGINE_GET_PRIVATE (engine);

	/* create syntactic contexts hash */
	priv->contexts = g_hash_table_new_full (g_str_hash, g_str_equal,
						(GDestroyNotify) g_free,
						(GDestroyNotify) syntactic_context_destroy);
}

static void
gtk_source_stack_engine_finalize (GObject *object)
{
	GtkSourceStackEngine *se;
	GtkSourceStackEnginePrivate *priv;

	g_return_if_fail (object != NULL);
	g_return_if_fail (GTK_IS_SOURCE_STACK_ENGINE (object));

	se = GTK_SOURCE_STACK_ENGINE (object);
	priv = STACK_ENGINE_GET_PRIVATE (se);

	/* disconnect buffer (if there is one), which destroys almost eveything */
	gtk_source_stack_engine_attach_buffer (GTK_SOURCE_ENGINE (se), NULL);

	/* destroy buffer independant data */
	g_hash_table_destroy (priv->contexts);
	priv->contexts = NULL;
	
	G_OBJECT_CLASS (parent_class)->finalize (object);
}


/* Buffer attachment and change tracking functions ------------------------------ */

static void 
text_inserted_cb (GtkSourceBuffer   *buffer,
		  const GtkTextIter *start,
		  const GtkTextIter *end,
		  gpointer           data)
{
	/* FIXME */
}

static void 
text_deleted_cb (GtkSourceBuffer   *buffer,
		 const GtkTextIter *iter,
		 const gchar       *text,
		 gpointer           data)
{
	/* FIXME */
}

static void 
update_highlight_cb (GtkSourceBuffer   *buffer,
		     const GtkTextIter *start,
		     const GtkTextIter *end,
		     gboolean           synchronous,
		     gpointer           data)
{
	/* FIXME */
}

static void
buffer_notify_cb (GObject    *object,
		  GParamSpec *pspec,
		  gpointer    user_data)
{
	/* FIXME */
}

static void
gtk_source_stack_engine_attach_buffer (GtkSourceEngine *engine,
				       GtkSourceBuffer *buffer)
{
	GtkSourceStackEngine *se;
	GtkSourceStackEnginePrivate *priv;
		
	g_return_if_fail (GTK_IS_SOURCE_STACK_ENGINE (engine));
	g_return_if_fail (buffer == NULL || GTK_IS_SOURCE_BUFFER (buffer));
	se = GTK_SOURCE_STACK_ENGINE (engine);
	priv = STACK_ENGINE_GET_PRIVATE (engine);
	
	/* detach previous buffer if there is one */
	if (priv->buffer)
	{
		gtk_text_region_destroy (priv->refresh_region, FALSE);
		gtk_text_region_destroy (priv->highlight_requests, FALSE);
		priv->refresh_region = NULL;
		priv->highlight_requests = NULL;
	}

	priv->buffer = buffer;

	if (buffer)
	{
		priv->highlight = gtk_source_buffer_get_highlight (buffer);
		
		/* highlight data */
		priv->refresh_region = gtk_text_region_new (GTK_TEXT_BUFFER (buffer));
		priv->highlight_requests = gtk_text_region_new (GTK_TEXT_BUFFER (buffer));
		
		g_signal_connect (buffer, "text_inserted",
				  G_CALLBACK (text_inserted_cb), se);
		g_signal_connect (buffer, "text_deleted",
				  G_CALLBACK (text_deleted_cb), se);
		g_signal_connect (buffer, "update_highlight",
				  G_CALLBACK (update_highlight_cb), se);
		g_signal_connect (buffer, "notify::highlight",
				  G_CALLBACK (buffer_notify_cb), se);
	}
}


/* Public API ------------------------------------------------------------ */

/**
 * gtk_source_stack_engine_new:
 * 
 * Return value: a new stack automata highlighting engine
 **/
GtkSourceEngine *
gtk_source_stack_engine_new ()
{
	GtkSourceEngine *engine;

	engine = GTK_SOURCE_ENGINE (g_object_new (GTK_TYPE_SOURCE_STACK_ENGINE, 
						  NULL));
	
	return engine;
}

gboolean
gtk_source_stack_engine_create_context (GtkSourceStackEngine *engine,
					const gchar          *id,
					const gchar          *style)
{
	GtkSourceStackEnginePrivate *priv;
	SyntacticContext *ctxt;
	
	g_return_val_if_fail (engine != NULL && GTK_IS_SOURCE_STACK_ENGINE (engine), FALSE);
	g_return_val_if_fail (id != NULL, FALSE);

	priv = STACK_ENGINE_GET_PRIVATE (engine);

	ctxt = g_hash_table_lookup (priv->contexts, id);
	if (ctxt)
	{
		g_warning ("Attempt to add an already existing context `%s' to the engine %p",
			   id, engine);
		return FALSE;
	}

	ctxt = g_new0 (SyntacticContext, 1);
	ctxt->id = g_strdup (id);
	ctxt->style = g_strdup (style);
	g_hash_table_insert (priv->contexts, g_strdup (id), ctxt);

	return TRUE;
}

gboolean
gtk_source_stack_engine_add_regex (GtkSourceStackEngine *engine,
				   const gchar          *ctxt_id,
				   const gchar          *id,
				   const gchar          *style,
				   const gchar          *regex,
				   GtkSourceStackAction  action,
				   const gchar          *target_ctxt)
{
	GtkSourceStackEnginePrivate *priv;
	SyntacticContext *ctxt;
	RegexItem *item;
	GtkSourceRegex *compiled;
	
	g_return_val_if_fail (engine != NULL && GTK_IS_SOURCE_STACK_ENGINE (engine), FALSE);
	g_return_val_if_fail (ctxt_id != NULL, FALSE);
	g_return_val_if_fail (regex != NULL, FALSE);

	priv = STACK_ENGINE_GET_PRIVATE (engine);

	ctxt = g_hash_table_lookup (priv->contexts, ctxt_id);
	if (ctxt == NULL)
	{
		g_warning ("Unkwown context `%s' while adding regular expression `%s'",
			   ctxt_id, id);
		return FALSE;
	}

	if (id != NULL &&
	    (g_list_find_custom (ctxt->switching_regexs, id, regex_item_id_compare) ||
	     g_list_find_custom (ctxt->normal_regexs, id, regex_item_id_compare)))
	{
		g_warning ("Regular expression `%s' already added to context `%s'",
			   id, ctxt_id);
		return FALSE;
	}

	if (action != GTK_SOURCE_STACK_STAY && target_ctxt == NULL)
	{
		g_warning ("Regular expression `%s' (context `%s') is missing a target "
			   "context for its action", id, ctxt_id);
		return FALSE;
	}
	
	compiled = gtk_source_regex_compile (regex);
	if (compiled == NULL)
	{
		g_warning ("Failed to compile regular expression `%s' for context `%s'.  "
			   "The failed expression was: %s", id, ctxt_id, regex);
		return FALSE;
	}
	
	item = g_new0 (RegexItem, 1);
	item->id = g_strdup (id);
	item->style = g_strdup (style);
	item->regex = g_strdup (regex);
	item->compiled_regex = compiled;
	item->action = action;
	item->target_id = g_strdup (target_ctxt);
	
	switch (action)
	{
		case GTK_SOURCE_STACK_STAY:
			ctxt->normal_regexs = g_list_prepend (ctxt->normal_regexs, item);
			break;

		case GTK_SOURCE_STACK_JUMP_TO_CONTEXT:
		case GTK_SOURCE_STACK_PUSH_CONTEXT:
		case GTK_SOURCE_STACK_POP_CONTEXT:
			ctxt->switching_regexs = g_list_prepend (ctxt->switching_regexs, item);
			break;
	}

	return TRUE;
}

