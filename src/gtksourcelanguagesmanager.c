/*  gtksourcelanguagesmanager.c
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
#include <libgnome/gnome-util.h>
#include <libgnome/gnome-macros.h>

#include "gtksourcelanguagesmanager.h"

#include "gtksourcelanguage-private.h"
#include "gtksourceview-i18n.h"

#include "gtksourcelanguage.h"
#include "gtksourcetag.h"

#define DEFAULT_GCONF_BASE_DIR		"/apps/gtksourceview"

#define DEFAULT_LANGUAGE_DIR		DATADIR "/gtksourceview-1.0/language-specs"
#define USER_LANGUAGE_DIR		"gtksourceview-1.0/language-specs"

enum {
	PROP_0,
	PROP_GCONF_BASE_DIR,
	PROP_LANG_SPECS_DIRS
};

struct _GtkSourceLanguagesManagerPrivate {

	GSList 		*available_languages;

	GSList		*language_specs_directories;
	gchar		*gconf_base_dir;
};


GNOME_CLASS_BOILERPLATE (GtkSourceLanguagesManager, 
			 gtk_source_languages_manager,
			 GObject, 
			 G_TYPE_OBJECT)

static void	 gtk_source_languages_manager_finalize	 	(GObject 		   *object);

static void	 slist_deep_free 				(GSList 		   *list);
static GSList 	*get_lang_files 				(GtkSourceLanguagesManager *lm);
static GSList	*build_file_listing 				(const gchar 		   *directory, 
					 			 GSList			   *filenames);

static void	 gtk_source_languages_manager_set_property 	(GObject 		   *object, 
					   			 guint 	 		    prop_id,
			    		   			 const GValue 		   *value, 
					   			 GParamSpec		   *pspec);
static void	 gtk_source_languages_manager_get_property 	(GObject 		   *object, 
					   			 guint 	 		    prop_id,
			    		   			 GValue 		   *value, 
					   			 GParamSpec		   *pspec);
static void	 gtk_source_languages_manager_set_specs_dirs	(GtkSourceLanguagesManager *lm,
								 const GSList 		   *dirs);
const GSList	*gtk_source_languages_manager_get_specs_dirs	(GtkSourceLanguagesManager *lm);

static void	 gtk_source_languages_manager_set_gconf_base_dir(GtkSourceLanguagesManager *lm,
								 const gchar		   *dir);


static void
gtk_source_languages_manager_class_init (GtkSourceLanguagesManagerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class		= g_type_class_peek_parent (klass);
	object_class->finalize	= gtk_source_languages_manager_finalize;

	object_class->set_property = gtk_source_languages_manager_set_property;
	object_class->get_property = gtk_source_languages_manager_get_property;

	g_object_class_install_property (object_class,
					 PROP_GCONF_BASE_DIR,
					 g_param_spec_string ("gconf_base_dir",
                                                              _("GConf base dir"),
                                                              _("GConf base dir used to store "
							        "user preferences"),
							      DEFAULT_GCONF_BASE_DIR,
							      (G_PARAM_READWRITE | 
							       G_PARAM_CONSTRUCT_ONLY)));

	g_object_class_install_property (object_class,
					 PROP_LANG_SPECS_DIRS,
					 g_param_spec_pointer ("lang_specs_dir",
						 	       _("Language spefications directories"),
							       _("List of directories where the "
								 "language spefication files (.lang) "
								 "are located"),
							       (G_PARAM_READWRITE | 
							        G_PARAM_CONSTRUCT_ONLY)));
}

static void
gtk_source_languages_manager_set_property (GObject 	*object, 
					   guint 	 prop_id,
			    		   const GValue *value, 
					   GParamSpec	*pspec)
{
	GtkSourceLanguagesManager *lm;

	g_return_if_fail (GTK_IS_SOURCE_LANGUAGES_MANAGER (object));

	lm = GTK_SOURCE_LANGUAGES_MANAGER (object);
    
	switch (prop_id) {
	    case PROP_GCONF_BASE_DIR:
		gtk_source_languages_manager_set_gconf_base_dir (
				lm, 
				g_value_get_string (value));
		break;
	
	    case PROP_LANG_SPECS_DIRS:
		gtk_source_languages_manager_set_specs_dirs (
				lm, 
				g_value_get_pointer (value));
		break;

	    default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gtk_source_languages_manager_get_property (GObject 	*object, 
					   guint 	 prop_id,
			    		   GValue 	*value, 
					   GParamSpec	*pspec)
{
	GtkSourceLanguagesManager *lm;

	g_return_if_fail (GTK_IS_SOURCE_LANGUAGES_MANAGER (object));

	lm = GTK_SOURCE_LANGUAGES_MANAGER (object);
    
	switch (prop_id) {
	    case PROP_GCONF_BASE_DIR:
		    g_value_set_string (value, lm->priv->gconf_base_dir);
		    break;
	
	    case PROP_LANG_SPECS_DIRS:
		    g_value_set_pointer (value, lm->priv->language_specs_directories);
		    break;
	    default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gtk_source_languages_manager_instance_init (GtkSourceLanguagesManager *lm)
{
	lm->priv = g_new0 (GtkSourceLanguagesManagerPrivate, 1);	
}


GtkSourceLanguagesManager *
gtk_source_languages_manager_new (void)
{
	return GTK_SOURCE_LANGUAGES_MANAGER (
					g_object_new (GTK_TYPE_SOURCE_LANGUAGES_MANAGER,
				      	NULL));
}

static void
gtk_source_languages_manager_finalize (GObject *object)
{
	GtkSourceLanguagesManager *lm;
       
	lm = GTK_SOURCE_LANGUAGES_MANAGER (object);
	
	g_free (lm->priv->gconf_base_dir);
	
	if (lm->priv->available_languages != NULL)
	{
		GSList *list = lm->priv->available_languages;
		
		g_slist_foreach (list, (GFunc) g_object_unref, NULL);
		g_slist_free (list);
	}

	slist_deep_free (lm->priv->language_specs_directories);

	g_free (lm->priv);

	(* G_OBJECT_CLASS (parent_class)->finalize) (object);
}

static void 
gtk_source_languages_manager_set_specs_dirs (GtkSourceLanguagesManager	*lm,
					     const GSList		*dirs)
{
	g_return_if_fail (GTK_IS_SOURCE_LANGUAGES_MANAGER (lm));
	g_return_if_fail (lm->priv->language_specs_directories == NULL);
			
	if (dirs == NULL)
	{
		lm->priv->language_specs_directories =
			g_slist_prepend (lm->priv->language_specs_directories,
					g_strdup (DEFAULT_LANGUAGE_DIR));
		lm->priv->language_specs_directories = 
			g_slist_prepend (lm->priv->language_specs_directories,
					gnome_util_home_file (USER_LANGUAGE_DIR));

		return;
	}

	while (dirs != NULL)
	{
		lm->priv->language_specs_directories = 
			g_slist_prepend (lm->priv->language_specs_directories,
					 g_strdup ((const gchar*)dirs->data));

		dirs = g_slist_next (dirs);
	}
}

const GSList *
gtk_source_languages_manager_get_specs_dirs (GtkSourceLanguagesManager *lm)
{
	g_return_val_if_fail (GTK_IS_SOURCE_LANGUAGES_MANAGER (lm), NULL);

	return lm->priv->language_specs_directories;
}

static void
gtk_source_languages_manager_set_gconf_base_dir (GtkSourceLanguagesManager 	*lm,
						 const gchar			*dir)
{
	g_return_if_fail (GTK_IS_SOURCE_LANGUAGES_MANAGER (lm));
	g_return_if_fail (lm->priv->gconf_base_dir == NULL);

	if (dir != NULL)
		lm->priv->gconf_base_dir = g_strdup (dir);
	else
		lm->priv->gconf_base_dir = g_strdup (DEFAULT_GCONF_BASE_DIR);
}

const GSList *
gtk_source_languages_manager_get_available_languages (GtkSourceLanguagesManager *lm)
{
	GSList *filenames;

	g_return_val_if_fail (GTK_IS_SOURCE_LANGUAGES_MANAGER (lm), NULL);

	if (lm->priv->available_languages != NULL)
	{
		return lm->priv->available_languages;
	}
	
	/* Build list of availables languages */
	filenames = get_lang_files (lm);
	
	while (filenames != NULL)
	{
		GtkSourceLanguage *lang;
		
		lang = _gtk_source_language_new_from_file ((const gchar*)filenames->data,
							   lm);

		if (lang == NULL)
		{
			g_warning ("Error reading language specification file '%s'", 
				   (const gchar*)filenames->data);
		}
		else
		{	
			lm->priv->available_languages = 
				g_slist_prepend (lm->priv->available_languages, lang);
		}

		filenames = g_slist_next (filenames);
	}

	slist_deep_free (filenames);

	/* TODO: remove duplicates - Paolo */

	return lm->priv->available_languages;
}

GtkSourceLanguage *
gtk_source_languages_manager_get_language_from_mime_type (GtkSourceLanguagesManager 	*lm,
							  const gchar 			*mime_type)
{
	const GSList *languages;
	g_return_val_if_fail (mime_type != NULL, NULL);

	languages = gtk_source_languages_manager_get_available_languages (lm);

	while (languages != NULL)
	{
		const GSList *mime_types;

		GtkSourceLanguage *lang = GTK_SOURCE_LANGUAGE (languages->data);
		
		mime_types = gtk_source_language_get_mime_types (lang);

		while (mime_types != NULL)
		{
			/* FIXME: is this right ? - Paolo */
			if (strcmp ((const gchar*)mime_types->data, mime_type) == 0)
			{				
				return lang;
			}

			mime_types = g_slist_next (mime_types);
		}

		languages = g_slist_next (languages);
	}

	return NULL;
}

static GSList *
get_lang_files (GtkSourceLanguagesManager *lm)
{
	GSList *filenames = NULL;
	GSList *dirs;

	g_return_val_if_fail (lm->priv->language_specs_directories != NULL, NULL);

	dirs = lm->priv->language_specs_directories;

	while (dirs != NULL)
	{
		filenames = build_file_listing ((const gchar*)dirs->data,
						filenames);

		dirs = g_slist_next (dirs);
	}

	return filenames;
}

static GSList *
build_file_listing (const gchar *directory, GSList *filenames)
{
	GDir *dir;
	const gchar *file_name;
	
	dir = g_dir_open (directory, 0, NULL);
	
	if (dir == NULL)
		return filenames;

	file_name = g_dir_read_name (dir);
	
	while (file_name != NULL)
	{
		/* FIXME: crash on PPC. Dunno why! - Paolo */
		gchar *full_path = g_build_filename (directory, file_name);

		if (!g_file_test (full_path, G_FILE_TEST_IS_DIR) && 
		    (strcmp (g_extension_pointer (full_path), "lang") == 0))
			filenames = g_slist_prepend (filenames, full_path);
		else
			g_free (full_path);

		file_name = g_dir_read_name (dir);
	}

	g_dir_close (dir);

	return filenames;
}

static void
slist_deep_free (GSList *list)
{
	g_slist_foreach (list, (GFunc) g_free, NULL);
	g_slist_free (list);
}

