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

#include <libgnome/gnome-i18n.h>

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


static GtkSourceTagStyle *
new_tag_style (gchar* foreground,
	       gchar* background,
	       gboolean bold,
	       gboolean italic,
	       gboolean use_default)
{
	GtkSourceTagStyle *ts;

	ts = g_new0 (GtkSourceTagStyle, 1);

	gdk_color_parse (foreground, &ts->foreground);

	if (background != NULL)
	{
		gdk_color_parse (background, &ts->background);
		ts->use_background = TRUE;
	}
	else
		ts->use_background = FALSE;
	
	ts->italic	= italic;
	ts->bold	= bold;
	ts->use_default	= use_default;

	return ts;
}

static Theme *
build_defaul_theme ()
{	
	Theme *theme;
	GtkSourceTagStyle *ts;

	theme = g_new0 (Theme, 1);

	theme->name = g_strdup (N_("Default"));

	theme->styles = g_hash_table_new_full ((GHashFunc)g_str_hash,
				               (GEqualFunc)g_str_equal,
					       (GDestroyNotify)g_free,
					       (GDestroyNotify)g_free);

	ts = new_tag_style ("#FF00FF", NULL, FALSE, FALSE, TRUE);
	g_hash_table_insert (theme->styles, 
			     g_strdup (N_("Base-N Integer")),
			     ts);

	ts = new_tag_style ("#FF00FF", NULL, FALSE, FALSE, TRUE);
	g_hash_table_insert (theme->styles, 
			     g_strdup (N_("Character")),
			     ts);

	ts = new_tag_style ("#0000FF", NULL, FALSE, FALSE, TRUE);
	g_hash_table_insert (theme->styles, 
			     g_strdup (N_("Comment")),
			     ts);

	ts = new_tag_style ("#2E8B57", NULL, TRUE, FALSE, TRUE);
	g_hash_table_insert (theme->styles, 
			     g_strdup (N_("Data Type")),
			     ts);

	ts = new_tag_style ("#FF00FF", NULL, FALSE, FALSE, TRUE);
	g_hash_table_insert (theme->styles, 
			     g_strdup (N_("Decimal")),
			     ts);

	ts = new_tag_style ("#FF00FF", NULL, FALSE, FALSE, TRUE);
	g_hash_table_insert (theme->styles, 
			     g_strdup (N_("Floating Point")),
			     ts);

	ts = new_tag_style ("#A52A2A", NULL, TRUE, FALSE, TRUE);
	g_hash_table_insert (theme->styles, 
			     g_strdup (N_("Keyword")),
			     ts);

	ts = new_tag_style ("#2E8B57", NULL, TRUE, FALSE, TRUE);
	g_hash_table_insert (theme->styles, 
			     g_strdup (N_("Others")),
			     ts);

	ts = new_tag_style ("#A020F0", NULL, FALSE, FALSE, TRUE);
	g_hash_table_insert (theme->styles, 
			     g_strdup (N_("Preprocessor")),
			     ts);


	ts = new_tag_style ("#FF00FF", NULL, FALSE, FALSE, TRUE);
	g_hash_table_insert (theme->styles, 
			     g_strdup (N_("String")),
			     ts);

	ts = new_tag_style ("#FFFFFF", "#FF0000", FALSE, FALSE, TRUE);
	g_hash_table_insert (theme->styles, 
			     g_strdup (N_("Specials")),
			     ts);

	return theme;
} 

static void
theme_free (Theme *theme)
{
	g_return_if_fail (theme != NULL);

	g_free (theme->name);

	g_hash_table_destroy (theme->styles);
}


gboolean 
gtk_source_tags_style_manager_init (void)
{
	if (tags_style_manager == NULL)
	{
		Theme *theme;
		
		tags_style_manager = g_new0 (GtkSourceTagsStyleManager, 1);

		theme = build_defaul_theme ();

		tags_style_manager->available_themes = g_slist_prepend (tags_style_manager->available_themes, theme);
		tags_style_manager->current_theme = theme->name;
	}

	return TRUE;
}

/* This function must be called for releasing the resources used by the tags style manager */
void
gtk_source_tags_style_manager_shutdown (void)
{
	if (tags_style_manager == NULL)
		return;

	g_slist_foreach (tags_style_manager->available_themes, (GFunc) theme_free, NULL);
	g_slist_free (tags_style_manager->available_themes);
}


const GSList *
gtk_source_tags_style_manager_get_available_themes (void)
{

	g_return_val_if_fail (tags_style_manager != NULL, NULL);

	return tags_style_manager->available_themes;
}


const gchar *
gtk_source_tags_style_manager_get_current_theme	(void)
{
	g_return_val_if_fail (tags_style_manager != NULL, NULL);

	return tags_style_manager->current_theme;
}

static const Theme*
get_theme (const gchar *name)
{
	GSList *list;
	
	list = tags_style_manager->available_themes;

	while ((list != NULL))
	{
		if (strcmp (name, (const gchar*)((Theme*)list->data)->name) == 0)
			return ((Theme*)list->data);

		list = g_slist_next (list);
	}

	return NULL;

}


gboolean
gtk_source_tags_style_manager_set_current_theme	(const gchar *theme)
{
	const Theme *t;
	
	g_return_val_if_fail (tags_style_manager != NULL, FALSE);

	if (theme == NULL)
	{
		tags_style_manager->current_theme = "Default";

		return TRUE;
	}

	t = get_theme (theme);
	if (t == NULL)
		return FALSE;
	
	tags_style_manager->current_theme = theme;

	return TRUE;
}

/* FIXME: it should be reimplemented when we will add theme support. */
const GtkSourceTagStyle *
gtk_source_get_default_tag_style (const gchar *klass)
{
	const Theme *theme;
	const gpointer *style;

	g_return_val_if_fail (tags_style_manager != NULL, NULL);

	theme = get_theme (tags_style_manager->current_theme);
	g_return_val_if_fail (theme != NULL, NULL);
			
	style = g_hash_table_lookup (theme->styles, klass);

	return (style != NULL) ? (const GtkSourceTagStyle*)style : NULL;
}

/*
void			*gtk_source_set_default_tag_style			(const gchar 		 *klass,
								 		 const GtkSourceTagStyle *style);
*/									

