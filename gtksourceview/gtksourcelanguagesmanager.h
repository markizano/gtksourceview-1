/*  gtksourcelanguagesmanager.h
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

#ifndef __GTK_SOURCE_LANGUAGES_MANAGER_H__
#define __GTK_SOURCE_LANGUAGES_MANAGER_H__

#include <glib.h>

G_BEGIN_DECLS

/** LIFE CYCLE MANAGEMENT FUNCTIONS **/

gboolean	 gtk_source_languages_manager_init	 		(void);

/* This function must be called for releasing the resources used by the languages manager */
void		 gtk_source_languages_manager_shutdown 			(void);

/* Set a list of dirs containing .lang files */
void		 gtk_source_languages_manager_set_specs_dirs		(const GSList 	*dirs);

void		 gtk_source_languages_manager_set_gconf_base_dir	(const gchar	*dir);

const GSList	*gtk_source_languages_manager_get_available_languages	(void);




G_END_DECLS				

#endif /* __GTK_SOURCE_LANGUAGES_MANAGER_H__ */

