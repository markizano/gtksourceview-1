/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8; coding: utf-8 -*- 
 *  test-new-widget.c
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

#include <stdio.h>
#include <string.h>
#include <gtk/gtk.h>
#include <gtksourceview/gtksourceview.h>
#include <gtksourceview/gtksourcelanguage.h>
#include <gtksourceview/gtksourcelanguagesmanager.h>
#include "../gtksourceview/gtksourcelanguage-private.h"


/* Program entry point ------------------------------------------------------------ */

int
main (int argc, char *argv[])
{
	GtkSourceLanguagesManager *lm;
	GtkSourceLanguage *lang;
	GtkSourceEngine *engine;
	
	/* initialization */
	gtk_init (&argc, &argv);
	
	/* create buffer */
	lm = gtk_source_languages_manager_new ();
	lang = _gtk_source_language_new_from_file (
		"../gtksourceview/language-specs/xml2.lang", lm);
	engine = gtk_source_language_create_engine (lang);
	gtk_source_language_get_tags (lang);

	g_object_unref (engine);
	g_object_unref (lang);
	g_object_unref (lm);

	return 0;
}
