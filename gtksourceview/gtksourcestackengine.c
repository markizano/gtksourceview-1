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

struct _GtkSourceStackEnginePrivate 
{
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

	priv->buffer = NULL;
}

static void
gtk_source_stack_engine_finalize (GObject *object)
{
	GtkSourceStackEngine *se;

	g_return_if_fail (object != NULL);
	g_return_if_fail (GTK_IS_SOURCE_STACK_ENGINE (object));

	se = GTK_SOURCE_STACK_ENGINE (object);

	/* disconnect buffer (if there is one), which destroys almost eveything */
	gtk_source_stack_engine_attach_buffer (GTK_SOURCE_ENGINE (se), NULL);
	
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
	return FALSE;
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
	return FALSE;
}


