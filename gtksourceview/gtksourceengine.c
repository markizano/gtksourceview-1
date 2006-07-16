/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8; coding: utf-8 -*-
 *
 *  gtksourceengine.c - Abstract base class for highlighting engines
 *
 *  Copyright (C) 2003 - Gustavo Giráldez
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

#include "gtksourcebuffer.h"
#include "gtksourceengine.h"


G_DEFINE_TYPE (GtkSourceEngine, gtk_source_engine, G_TYPE_OBJECT)


static void
gtk_source_engine_class_init (GtkSourceEngineClass *klass)
{
	klass->attach_buffer = NULL;
}


static void
gtk_source_engine_init (GtkSourceEngine *engine)
{
}


void
gtk_source_engine_attach_buffer (GtkSourceEngine *engine,
				 GtkTextBuffer   *buffer)
{
	g_return_if_fail (GTK_IS_SOURCE_ENGINE (engine));
	g_return_if_fail (GTK_SOURCE_ENGINE_GET_CLASS (engine)->attach_buffer);

	GTK_SOURCE_ENGINE_GET_CLASS (engine)->attach_buffer (engine, buffer);
}
