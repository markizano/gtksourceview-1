/*  gtksourcetagstyle.h
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

#ifndef __GTK_SOURCE_TAG_STYLE_H__
#define __GTK_SOURCE_TAG_STYLE_H__

#include <glib.h>
#include <gdk/gdkcolor.h>

G_BEGIN_DECLS

typedef struct _GtkSourceTagStyle GtkSourceTagStyle;

struct _GtkSourceTagStyle {
	GdkColor foreground;
	GdkColor background;
	gboolean italic;
	gboolean bold;

	gboolean use_default;
};

/** LIFE CYCLE MANAGEMENT FUNCTIONS **/

gboolean	 	 gtk_source_tags_style_manager_init	 		(void);

/* This function must be called for releasing the resources used by the tags style manager */
void		 	 gtk_source_tags_style_manager_shutdown			(void);



const GSList		*gtk_source_tags_style_manager_get_available_themes	(void);

const gchar		*gtk_source_tags_style_manager_get_current_theme	(void);
gboolean		 gtk_source_tags_style_manager_set_current_theme	(const gchar 		 *theme);



const GtkSourceTagStyle *gtk_source_get_default_tag_style 			(const gchar 		 *klass);

void			*gtk_source_set_default_tag_style			(const gchar 		 *klass,
								 		 const GtkSourceTagStyle *style);
G_END_DECLS

#endif  /* __GTK_SOURCE_TAG_STYLE_H__ */

