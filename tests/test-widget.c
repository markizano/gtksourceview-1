/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- 
 *  test-widget.c
 *
 *  Copyright (C) 2001
 *  Mikael Hermansson<tyan@linux.se>
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
#include <libgnomevfs/gnome-vfs-init.h>
#include <libgnomevfs/gnome-vfs-mime-utils.h>
#include <libgnomevfs/gnome-vfs-utils.h>
#include <gtksourceview.h>
#include <gtksourcelanguage.h>
#include <gtksourcelanguagesmanager.h>


/* Private data structures */

typedef struct {
	GtkSourceBuffer *buffer;
	GList           *windows;
	gboolean         show_markers;
	gboolean         show_numbers;
	guint            tab_stop;
	GtkItemFactory  *item_factory;
} ViewsData;

#define READ_BUFFER_SIZE   4096


/* Private prototypes */

static void       open_file_cb                   (ViewsData       *vd,
						  guint            callback_action,
						  GtkWidget       *widget);
static void       new_view_cb                    (ViewsData       *vd,
						  guint            callback_action,
						  GtkWidget       *widget);
static void       show_toggled_cb                (ViewsData       *vd,
						  guint            callback_action,
						  GtkWidget       *widget);

/* Menu definition */

#define SHOW_NUMBERS_PATH "/View/Show _Line Numbers"
#define SHOW_MARKERS_PATH "/View/Show _Markers"

static GtkItemFactoryEntry menu_items[] = {
	{ "/_File",                   NULL,         0,               0, "<Branch>" },
	{ "/File/_Open",              "<control>O", open_file_cb,    0, "<StockItem>", GTK_STOCK_OPEN },
	{ "/File/sep1",               NULL,         0,               0, "<Separator>" },
	{ "/File/_Quit",              "<control>Q", gtk_main_quit,   0 },
	
	{ "/_View",                   NULL,         0,               0, "<Branch>" },
	{ "/View/_New View",          NULL,         new_view_cb,     0, "<StockItem>", GTK_STOCK_NEW },
	{ "/View/sep1",               NULL,         0,               0, "<Separator>" },
	{ SHOW_NUMBERS_PATH,          NULL,         show_toggled_cb, 1, "<CheckItem>" },
	{ SHOW_MARKERS_PATH,          NULL,         show_toggled_cb, 2, "<CheckItem>" },
};

/* Implementation */

static void 
show_toggled_cb (ViewsData *vd,
		 guint      callback_action,
		 GtkWidget *widget)
{
	gboolean active;
	void (*set_func) (GtkSourceView *, gboolean);

	set_func = NULL;
	active = gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (widget));
	
	switch (callback_action)
	{
		case 1:
			vd->show_numbers = active;
			set_func = gtk_source_view_set_show_line_numbers;
			break;
		case 2:
			vd->show_markers = active;
			set_func = gtk_source_view_set_show_line_pixmaps;
			break;
		default:
			break;
	}

	if (set_func)
	{
		GList *l;
		for (l = vd->windows; l; l = l->next)
		{
			GtkWidget *window = l->data;
			GtkWidget *view = g_object_get_data (G_OBJECT (window), "view");
			set_func (GTK_SOURCE_VIEW (view), active);
		}
	}
}

static void
error_dialog (GtkWindow *parent, const gchar *msg, ...)
{
	va_list ap;
	gchar *tmp;
	GtkWidget *dialog;
	
	va_start (ap, msg);
	tmp = g_strdup_vprintf (msg, ap);
	va_end (ap);
	
	dialog = gtk_message_dialog_new (parent,
					 GTK_DIALOG_DESTROY_WITH_PARENT,
					 GTK_MESSAGE_ERROR,
					 GTK_BUTTONS_OK,
					 tmp);
	g_free (tmp);
	
	gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);
}

static gboolean 
gtk_source_buffer_load_with_encoding (GtkSourceBuffer *source_buffer,
				      const gchar     *filename,
				      const gchar     *encoding,
				      GError         **error)
{
	GIOChannel *io;
	GtkTextIter iter;
	gchar *buffer;
	gboolean reading;
	
	g_return_val_if_fail (source_buffer != NULL, FALSE);
	g_return_val_if_fail (filename != NULL, FALSE);
	g_return_val_if_fail (GTK_IS_SOURCE_BUFFER (source_buffer), FALSE);

	*error = NULL;

	io = g_io_channel_new_file (filename, "r", error);
	if (!io)
	{
		error_dialog (NULL, "%s\nFile %s", (*error)->message, filename);
		return FALSE;
	}

	if (g_io_channel_set_encoding (io, encoding, error) != G_IO_STATUS_NORMAL)
	{
		error_dialog (NULL, "Failed to set encoding:\n%s\n%s",
			      filename, (*error)->message);
		return FALSE;
	}

	gtk_source_buffer_begin_not_undoable_action (source_buffer);

	gtk_text_buffer_set_text (GTK_TEXT_BUFFER (source_buffer), "", 0);
	buffer = g_malloc (READ_BUFFER_SIZE);
	reading = TRUE;
	while (reading)
	{
		gsize bytes_read;
		GIOStatus status;
		
		status = g_io_channel_read_chars (io, buffer,
						  READ_BUFFER_SIZE, &bytes_read,
						  error);
		switch (status)
		{
			case G_IO_STATUS_EOF:
				reading = FALSE;
				/* fall through */
				
			case G_IO_STATUS_NORMAL:
				if (bytes_read == 0)
				{
					continue;
				}
				
				gtk_text_buffer_get_end_iter (
					GTK_TEXT_BUFFER (source_buffer), &iter);
				gtk_text_buffer_insert (GTK_TEXT_BUFFER (source_buffer),
							&iter, buffer, bytes_read);
				break;
				
			case G_IO_STATUS_AGAIN:
				continue;

			case G_IO_STATUS_ERROR:
			default:
				error_dialog (NULL, "%s\nFile %s", (*error)->message, filename);

				/* because of error in input we clear already loaded text */
				gtk_text_buffer_set_text (GTK_TEXT_BUFFER (source_buffer), "", 0);
				
				reading = FALSE;
				break;
		}
	}
	g_free (buffer);
	
	gtk_source_buffer_end_not_undoable_action (source_buffer);

	g_io_channel_unref (io);

	if (*error)
		return FALSE;

	gtk_text_buffer_set_modified (GTK_TEXT_BUFFER (source_buffer), FALSE);

	/* move cursor to the beginning */
	gtk_text_buffer_get_start_iter (GTK_TEXT_BUFFER (source_buffer), &iter);
	gtk_text_buffer_place_cursor (GTK_TEXT_BUFFER (source_buffer), &iter);

	return TRUE;
}

static gboolean
open_file (GtkSourceBuffer *buffer, const gchar *filename)
{
	GtkSourceLanguagesManager *manager;
	GtkSourceLanguage *language = NULL;
	gchar *mime_type;
	GtkSourceTagTable *table;
	GError *err = NULL;
	gchar *uri;
	
	/* remove previous tags */
	table = GTK_SOURCE_TAG_TABLE (gtk_text_buffer_get_tag_table (GTK_TEXT_BUFFER (buffer)));
	gtk_source_tag_table_remove_all_source_tags (table);

	/* get the new language for the file mimetype */
	manager = g_object_get_data (G_OBJECT (buffer), "languages-manager");

	/* I hate this! */
	if (g_path_is_absolute (filename))
	{
		uri = gnome_vfs_get_uri_from_local_path (filename);
	}
	else
	{
		gchar *curdir, *path;
		
		curdir = g_get_current_dir ();
		path = g_strconcat (curdir, "/", filename, NULL);
		g_free (curdir);
		uri = gnome_vfs_get_uri_from_local_path (path);
		g_free (path);
	}

	mime_type = gnome_vfs_get_mime_type (uri);
	g_free (uri);
	if (mime_type)
	{
		language = gtk_source_languages_manager_get_language_from_mime_type (manager,
										     mime_type);

		if (language != NULL)
		{
			const GSList *list = NULL;
			
			list = gtk_source_language_get_tags (language);		
 			gtk_source_tag_table_add_all (table, list);
		}
		else
		{
			g_print ("No language found for mime type `%s'\n", mime_type);
		}
		g_free (mime_type);
	}
	else
	{
		g_warning ("Couldn't get mime type for file `%s'", filename);
	}

	gtk_source_buffer_load_with_encoding (buffer, filename, "utf-8", &err);
	
	if (err != NULL)
	{
		g_error_free (err);
		return FALSE;
	}
	return TRUE;
}

static void
file_selected_cb (GtkWidget *widget, GtkFileSelection *file_sel)
{
	ViewsData *vd;
	const gchar *filename;
	
	vd = g_object_get_data (G_OBJECT (file_sel), "viewsdata");
	filename = gtk_file_selection_get_filename (file_sel);
	open_file (vd->buffer, filename);
}

static void 
open_file_cb (ViewsData *vd,
	      guint      callback_action,
	      GtkWidget *widget)
{
	GtkWidget *file_sel;

	file_sel = gtk_file_selection_new ("Open file...");
	g_object_set_data (G_OBJECT (file_sel), "viewsdata", vd);

	g_signal_connect (GTK_FILE_SELECTION (file_sel)->ok_button,
			  "clicked",
			  G_CALLBACK (file_selected_cb),
			  file_sel);

	g_signal_connect_swapped (GTK_FILE_SELECTION (file_sel)->ok_button, 
				  "clicked", G_CALLBACK (gtk_widget_destroy),
				  file_sel);
	g_signal_connect_swapped (GTK_FILE_SELECTION (file_sel)->cancel_button, 
				  "clicked", G_CALLBACK (gtk_widget_destroy),
				  file_sel);

	gtk_widget_show (file_sel);
}

/* Stolen from gedit */

static void
update_cursor_position (GtkTextBuffer *buffer, GtkWidget *label)
{
	gchar *msg;
	gint row, col, chars;
	GtkTextIter iter, start;
	
	gtk_text_buffer_get_iter_at_mark (buffer,
					  &iter,
					  gtk_text_buffer_get_insert (buffer));
	
	chars = gtk_text_iter_get_offset (&iter);
	row = gtk_text_iter_get_line (&iter) + 1;
	
	start = iter;
	gtk_text_iter_set_line_offset (&start, 0);
	col = 0;

	while (!gtk_text_iter_equal (&start, &iter))
	{
		if (gtk_text_iter_get_char (&start) == '\t')
		{
			col += (8 - (col % 8));
		}
		else
			++col;
		
		gtk_text_iter_forward_char (&start);
	}
	
	msg = g_strdup_printf ("char: %d, line: %d, column: %d", chars, row, col);
	gtk_label_set_text (GTK_LABEL (label), msg);
      	g_free (msg);
}

static void 
move_cursor_cb (GtkTextBuffer *buffer,
		GtkTextIter   *cursoriter,
		GtkTextMark   *mark,
		gpointer       data)
{
	if (mark != gtk_text_buffer_get_insert (buffer))
		return;
	
	update_cursor_position (buffer, GTK_WIDGET (data));
}

static gboolean
window_deleted_cb (GtkWidget *widget, GdkEvent *ev, ViewsData *vd)
{
	if (g_list_nth_data (vd->windows, 0) == widget)
	{
		/* Main (first in the list) window was closed, so exit
		 * the application */
		gtk_main_quit ();
	}
	else
	{
		vd->windows = g_list_remove (vd->windows, widget);

		/* we return FALSE since we want the window destroyed */
		return FALSE;
	}
	
	return TRUE;
}

static GtkWidget *
create_window (ViewsData *vd)
{
	GtkWidget *window, *sw, *view, *vbox;
	PangoFontDescription *font_desc = NULL;
	
	/* window */
	window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_container_set_border_width (GTK_CONTAINER (window), 0);
	gtk_window_set_title (GTK_WINDOW (window), "GtkSourceView Demo");
	g_signal_connect (window, "delete-event", (GCallback) window_deleted_cb, vd);
	vd->windows = g_list_append (vd->windows, window);

	/* vbox */
	vbox = gtk_vbox_new (0, FALSE);
	gtk_container_add (GTK_CONTAINER (window), vbox);
	g_object_set_data (G_OBJECT (window), "vbox", vbox);
	gtk_widget_show (vbox);

	/* scrolled window */
	sw = gtk_scrolled_window_new (NULL, NULL);
	gtk_box_pack_end (GTK_BOX (vbox), sw, TRUE, TRUE, 0);
	gtk_widget_show (sw);
	
	/* view */
	view = gtk_source_view_new_with_buffer (vd->buffer);
	g_object_set_data (G_OBJECT (window), "view", view);
	gtk_container_add (GTK_CONTAINER (sw), view);
	gtk_widget_show (view);

	/* setup view */
	font_desc = pango_font_description_from_string ("monospace 10");
	if (font_desc != NULL)
	{
		gtk_widget_modify_font (view, font_desc);
		pango_font_description_free (font_desc);
	}
	g_signal_connect (view, "realize",
			  (GCallback) gtk_source_view_set_tab_stop,
			  GINT_TO_POINTER (vd->tab_stop));
	gtk_source_view_set_show_line_numbers (GTK_SOURCE_VIEW (view), vd->show_numbers);
	gtk_source_view_set_show_line_pixmaps (GTK_SOURCE_VIEW (view), vd->show_markers);

	return window;
}

static void
new_view_cb (ViewsData *vd, guint callback_action, GtkWidget *widget)
{
	GtkWidget *window;
	
	window = create_window (vd);
	gtk_window_set_default_size (GTK_WINDOW (window), 400, 400);
	gtk_widget_show (window);
}

static GtkWidget *
create_main_window (ViewsData *vd)
{
	GtkWidget *window;
	GtkAccelGroup *accel_group;
	GtkWidget *vbox;
	GtkWidget *label;
	GtkWidget *menu;
	
	window = create_window (vd);
	vbox = g_object_get_data (G_OBJECT (window), "vbox");

	/* item factory/menu */
	accel_group = gtk_accel_group_new ();
	vd->item_factory = gtk_item_factory_new (GTK_TYPE_MENU_BAR, "<main>", accel_group);
	gtk_window_add_accel_group (GTK_WINDOW (window), accel_group);
	g_object_unref (accel_group);
	gtk_item_factory_create_items (vd->item_factory,
				       G_N_ELEMENTS (menu_items),
				       menu_items,
				       vd);
	menu = gtk_item_factory_get_widget (vd->item_factory, "<main>");
	gtk_box_pack_start (GTK_BOX (vbox), menu, FALSE, FALSE, 0);
	gtk_widget_show (menu);
	
	/* preselect menu checkitems */
	gtk_check_menu_item_set_active (
		GTK_CHECK_MENU_ITEM (gtk_item_factory_get_item (vd->item_factory,
								"/View/Show Line Numbers")),
		vd->show_numbers);
	gtk_check_menu_item_set_active (
		GTK_CHECK_MENU_ITEM (gtk_item_factory_get_item (vd->item_factory,
								"/View/Show Markers")),
		vd->show_markers);

	/* cursor position label */
	label = gtk_label_new ("label");
	gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, FALSE, 0);
	g_signal_connect_closure (vd->buffer, "mark_set",
				  g_cclosure_new_object ((GCallback) move_cursor_cb,
							 G_OBJECT (label)),
				  TRUE);
	g_signal_connect_closure (vd->buffer, "changed",
				  g_cclosure_new_object ((GCallback) update_cursor_position,
							 G_OBJECT (label)),
				  TRUE);
	gtk_widget_show (label);
	
#if 0
	/* FIXME: update when we have a stable marker API */
	
	pixbuf = gdk_pixbuf_new_from_file ("/usr/share/pixmaps/apple-green.png", NULL);
	gtk_source_view_add_pixbuf (GTK_SOURCE_VIEW (tw), "one", pixbuf, FALSE);
	g_object_unref (pixbuf);
	
	pixbuf = gdk_pixbuf_new_from_file ("/usr/share/pixmaps/no.xpm", NULL);
	gtk_source_view_add_pixbuf (GTK_SOURCE_VIEW (tw), "two", pixbuf, FALSE);
	g_object_unref (pixbuf);
	
	pixbuf = gdk_pixbuf_new_from_file ("/usr/share/pixmaps/yes.xpm", NULL);
	gtk_source_view_add_pixbuf (GTK_SOURCE_VIEW (tw), "three", pixbuf, FALSE);
	g_object_unref (pixbuf);
#endif
	
	return window;
}

static GtkSourceBuffer *
create_source_buffer (GtkSourceLanguagesManager *manager)
{
	GtkSourceBuffer *buffer;
	
	buffer = GTK_SOURCE_BUFFER (gtk_source_buffer_new (NULL));
	g_object_ref (manager);
	g_object_set_data_full (G_OBJECT (buffer), "languages-manager",
				manager, (GDestroyNotify) g_object_unref);
	
	return buffer;
}

int
main (int argc, char *argv[])
{
	GtkWidget *window;
	GtkSourceLanguagesManager *lm;
	ViewsData *vd;
	
	/* initialization */
	gtk_init (&argc, &argv);
	gnome_vfs_init ();
	
	lm = gtk_source_languages_manager_new ();
	
	/* setup... */
	vd = g_new0 (ViewsData, 1);
	vd->buffer = create_source_buffer (lm);
	g_object_unref (lm);
	vd->windows = NULL;
	vd->show_numbers = TRUE;
	vd->show_markers = TRUE;
	
	window = create_main_window (vd);
	if (argc > 1)
		open_file (vd->buffer, argv [1]);
	else
		open_file (vd->buffer, "../src/gtksourcebuffer.c");

	gtk_window_set_default_size (GTK_WINDOW (window), 500, 500);
	gtk_widget_show (window);

	/* ... and action! */
	gtk_main ();

	/* cleanup */
	g_object_unref (vd->buffer);
	g_object_unref (vd->item_factory);
	g_list_foreach (vd->windows, (GFunc) gtk_widget_destroy, NULL);
	g_list_free (vd->windows);
	g_free (vd);

	gnome_vfs_shutdown ();
	
	return 0;
}
