/*  gtksourcetagstyle.c
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

#include "gtksourcetagstyle.h"

typedef struct _GtkSourceTagsStyleManager	GtkSourceTagsStyleManager;

struct _GtkSourceTagsStyleManager {

	const gchar	*current_theme;

	GSList		*available_themes;
};

typedef struct _Theme				Theme;

struct _Theme {
	gchar 		*name;
	
	GHashTable	*styles;
};


static GtkSourceTagsStyleManager	*tags_style_manager = NULL;

/*
static Theme *
build_defaul_theme ()
{	
	Theme *theme;

	theme = g_new0 (Theme, 1);

	theme->name = g_strdup (_("Default"));

	theme->styles = g_hash_table_new_full ((GHashFunc)g_str_hash,
				               (GEqualFunc)g_str_equal,
					       (GDestroyNotify)g_free,
					       (GDestroyNotify)g_free);

	return theme;
}

void
theme_free (Theme *theme)
{
	g_return_if_fail (theme != NULL);

	g_free (theme->name);

	g_hash_table_destroy (theme->styles);
}
*/

gboolean 
gtk_source_tags_style_manager_init (void)
{
	if (tags_style_manager != NULL)
	{
		tags_style_manager = g_new0 (GtkSourceTagsStyleManager, 1);
	}

	return TRUE;
}

/* This function must be called for releasing the resources used by the tags style manager */
void
gtk_source_tags_style_manager_shutdown (void)
{
	if (tags_style_manager == NULL)
		return;

	/* FIXME - Free available_themes */
}



const GSList *
gtk_source_tags_style_manager_get_available_themes (void)
{
	g_return_val_if_fail (tags_style_manager != NULL, NULL);

	return tags_style_manager->available_themes;
}


/* 
 * If current_them == NULL then the default theme 
 */
const gchar *
gtk_source_tags_style_manager_get_current_theme	(void)
{
	g_return_val_if_fail (tags_style_manager != NULL, NULL);

	return tags_style_manager->current_theme;
}

/* FIXME: it should be reimplemented when we will add theme support. */
gboolean
gtk_source_tags_style_manager_set_current_theme	(const gchar *theme)
{
	GSList *list;
	gboolean found = FALSE;
	
	g_return_val_if_fail (tags_style_manager != NULL, FALSE);

	if (theme == NULL)
	{
		tags_style_manager->current_theme = NULL;

		return TRUE;
	}

	list = tags_style_manager->available_themes;

	while ((list != NULL) && !found)
	{
		if (strcmp (theme, (const gchar*)list->data) == 0)
			found = TRUE;

		list = g_slist_next (list);
	}

	if (found)
		tags_style_manager->current_theme = theme;

	return found;
}

/* FIXME: it should be reimplemented when we will add theme support. */
const GtkSourceTagStyle *
gtk_source_get_default_tag_style (const gchar *klass)
{
	return NULL;
}

/*
void			*gtk_source_set_default_tag_style			(const gchar 		 *klass,
								 		 const GtkSourceTagStyle *style);
*/									

