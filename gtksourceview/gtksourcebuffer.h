/*  gtksourcebuffer.h
 *
 *  Copyright (C) 1999,2000,2001,2002 by:
 *          Mikael Hermansson <tyan@linux.se>
 *          Chris Phelps <chicane@reninet.com>
 *          Jeroen Zwartepoorte <jeroen@xs4all.nl>
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

#ifndef __GTK_SOURCE_BUFFER_H__
#define __GTK_SOURCE_BUFFER_H__

#include <regex.h>
#include <gtk/gtk.h>
#include "gtksourcetag.h"

G_BEGIN_DECLS

#define GTK_TYPE_SOURCE_BUFFER			(gtk_source_buffer_get_type ())
#define GTK_SOURCE_BUFFER(obj)			(GTK_CHECK_CAST ((obj), GTK_TYPE_SOURCE_BUFFER, GtkSourceBuffer))
#define GTK_SOURCE_BUFFER_CLASS(klass)		(GTK_CHECK_CLASS_CAST ((klass), GTK_TYPE_SOURCE_BUFFER, GtkSourceBufferClass))
#define GTK_IS_SOURCE_BUFFER(obj)		(GTK_CHECK_TYPE ((obj), GTK_TYPE_SOURCE_BUFFER))
#define GTK_IS_SOURCE_BUFFER_CLASS(klass)	(GTK_CHECK_CLASS_TYPE ((klass), GTK_TYPE_SOURCE_BUFFER))

typedef struct _GtkSourceBuffer			GtkSourceBuffer;
typedef struct _GtkSourceBufferClass		GtkSourceBufferClass;
typedef struct _GtkSourceBufferPrivate		GtkSourceBufferPrivate;
typedef struct _GtkSourceBufferMarker		GtkSourceBufferMarker;

struct _GtkSourceBuffer 
{
	GtkTextBuffer text_buffer;

	GtkSourceBufferPrivate *priv;
};

struct _GtkSourceBufferClass 
{
	GtkTextBufferClass parent_class;

	void (* can_undo)		(GtkSourceBuffer *buffer,
					 gboolean         can_undo);
	void (* can_redo)		(GtkSourceBuffer *buffer,
					 gboolean         can_redo);
};

struct _GtkSourceBufferMarker 
{
	gint   line;
	gchar *name;
};

/* Creation. */
GType            gtk_source_buffer_get_type 		(void) G_GNUC_CONST;
GtkSourceBuffer *gtk_source_buffer_new 			(GtkTextTagTable       *table);

/* Properties. */
void             gtk_source_buffer_set_check_brackets	(GtkSourceBuffer       *buffer,
						       	 gboolean               check_brackets);

gboolean         gtk_source_buffer_get_highlight	(const GtkSourceBuffer *buffer);
void             gtk_source_buffer_set_highlight	(GtkSourceBuffer       *buffer,
							 gboolean               highlight);

/* FIXME: TO BE REMOVED - Paolo */
GList		*gtk_source_buffer_get_regex_tags	(const GtkSourceBuffer *buffer);
void		 gtk_source_buffer_purge_regex_tags	(GtkSourceBuffer       *buffer);
void		 gtk_source_buffer_install_regex_tags	(GtkSourceBuffer       *buffer,
							 GList                 *entries);
/* Utility method */
gboolean	 gtk_source_buffer_find_bracket_match 	(GtkTextIter           *iter);

/* Undo/redo methods */
gboolean	 gtk_source_buffer_can_undo		(const GtkSourceBuffer *buffer);
gboolean	 gtk_source_buffer_can_redo		(const GtkSourceBuffer *buffer);

void		 gtk_source_buffer_undo			(GtkSourceBuffer       *buffer);
void		 gtk_source_buffer_redo			(GtkSourceBuffer       *buffer);

gint		 gtk_source_buffer_get_undo_levels	(const GtkSourceBuffer *buffer);
void		 gtk_source_buffer_set_undo_levels	(GtkSourceBuffer       *buffer,
						    	 gint                   undo_levels);

void		 gtk_source_buffer_begin_not_undoable_action (GtkSourceBuffer  *buffer);
void		 gtk_source_buffer_end_not_undoable_action   (GtkSourceBuffer  *buffer);

/* Line marker methods. */
void		 gtk_source_buffer_line_add_marker	(GtkSourceBuffer        *buffer,
							 gint                    line,
							 const gchar            *marker);
void             gtk_source_buffer_line_set_marker	(GtkSourceBuffer        *buffer,
							 gint                    line,
							 const gchar            *marker);
gboolean         gtk_source_buffer_line_remove_marker	(GtkSourceBuffer        *buffer,
							 gint                    line,
							 const gchar            *marker);
const GList     *gtk_source_buffer_line_get_markers	(const GtkSourceBuffer  *buffer,
							 gint                    line);
gint             gtk_source_buffer_line_has_markers	(const GtkSourceBuffer  *buffer,
							 gint                    line);
gint             gtk_source_buffer_line_remove_markers	(GtkSourceBuffer        *buffer,
							 gint                    line);
GList           *gtk_source_buffer_get_all_markers	(const GtkSourceBuffer  *buffer);
gint             gtk_source_buffer_remove_all_markers	(GtkSourceBuffer        *buffer,
							 gint                    line_start,
							 gint                    line_end);
G_END_DECLS

#endif /* __GTK_SOURCE_BUFFER_H__ */
