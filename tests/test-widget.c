/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8; coding: utf-8 -*-
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

/* If TEST_XML_MEM is defined the test program will try to detect memory
 * allocated by xmlMalloc() but not freed by xmlFree() or freed by xmlFree()
 * but not allocated by xmlMalloc(). */
#define TEST_XML_MEM

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <gtk/gtk.h>
#include <gtksourceview/gtksourceview.h>
#include <gtksourceview/gtksourcelanguage.h>
#include <gtksourceview/gtksourcelanguagemanager.h>
#include <gtksourceview/gtksourcestyleschememanager.h>
#include <gtksourceview/gtksourceprintcompositor.h>
#ifdef TEST_XML_MEM
#include <libxml/xmlreader.h>
#endif
#ifdef USE_GNOME_VFS
#include <libgnomevfs/gnome-vfs-init.h>
#include <libgnomevfs/gnome-vfs-mime-utils.h>
#include <libgnomevfs/gnome-vfs-utils.h>
#endif

/* Global list of open windows */

static GList *windows = NULL;
static GtkSourceStyleScheme *style_scheme = NULL;

/* Private data structures */

#define READ_BUFFER_SIZE   4096

#define MARKER_TYPE_1      "one"
#define MARKER_TYPE_2      "two"


/* Private prototypes -------------------------------------------------------- */

static void       open_file_cb                   (GtkAction       *action,
						  gpointer         user_data);
static void       print_file_cb                  (GtkAction       *action,
						  gpointer         user_data);						  
static void       debug_thing_3_cb		 (GtkAction       *action,
						  gpointer         user_data);

static void       new_view_cb                    (GtkAction       *action,
						  gpointer         user_data);
static void       numbers_toggled_cb             (GtkAction       *action,
						  gpointer         user_data);
static void       markers_toggled_cb             (GtkAction       *action,
						  gpointer         user_data);
static void       margin_toggled_cb              (GtkAction       *action,
						  gpointer         user_data);
static void       hl_line_toggled_cb             (GtkAction       *action,
						  gpointer         user_data);
static void       wrap_lines_toggled_cb          (GtkAction       *action,
						  gpointer         user_data);
static void       auto_indent_toggled_cb         (GtkAction       *action,
						  gpointer         user_data);
static void       insert_spaces_toggled_cb       (GtkAction       *action,
						  gpointer         user_data);
static void       tabs_toggled_cb                (GtkAction       *action,
						  GtkAction       *current,
						  gpointer         user_data);
static void       indent_toggled_cb              (GtkAction       *action,
						  GtkAction       *current,
						  gpointer         user_data);

static GtkWidget *create_view_window             (GtkSourceBuffer *buffer,
						  GtkSourceView   *from);


/* Actions & UI definition ---------------------------------------------------- */

static GtkActionEntry buffer_action_entries[] = {
	{ "Open", GTK_STOCK_OPEN, "_Open", "<control>O",
	  "Open a file", G_CALLBACK (open_file_cb) },
	{ "Quit", GTK_STOCK_QUIT, "_Quit", "<control>Q",
	  "Exit the application", G_CALLBACK (gtk_main_quit) }
};

static GtkActionEntry view_action_entries[] = {
	{ "FileMenu", NULL, "_File" },
	{ "Print", GTK_STOCK_PRINT, "_Print", "<control>P",
	  "Print the current file", G_CALLBACK (print_file_cb) },
	{ "ViewMenu", NULL, "_View" },
	{ "NewView", GTK_STOCK_NEW, "_New View", NULL,
	  "Create a new view of the file", G_CALLBACK (new_view_cb) },
	{ "TabWidth", NULL, "_Tab Width" },
	{ "IndentWidth", NULL, "I_ndent Width" },
	{ "SmartHomeEnd", NULL, "_Smart Home/End" },
	{ "DebugThing3", GTK_STOCK_FIND_AND_REPLACE, "Search and _Replace", "<control>R",
	  "Search and Replace", G_CALLBACK (debug_thing_3_cb) },
};

static GtkToggleActionEntry toggle_entries[] = {
	{ "ShowNumbers", NULL, "Show _Line Numbers", NULL,
	  "Toggle visibility of line numbers in the left margin",
	  G_CALLBACK (numbers_toggled_cb), FALSE },
	{ "ShowMarkers", NULL, "Show _Markers", NULL,
	  "Toggle visibility of markers in the left margin",
	  G_CALLBACK (markers_toggled_cb), FALSE },
	{ "ShowMargin", NULL, "Show Right M_argin", NULL,
	  "Toggle visibility of right margin indicator",
	  G_CALLBACK (margin_toggled_cb), FALSE },
	{ "HlLine", NULL, "_Highlight Current Line", NULL,
	  "Toggle highlighting of current line",
	  G_CALLBACK (hl_line_toggled_cb), FALSE },
	{ "WrapLines", NULL, "_Wrap Lines", NULL,
	  "Toggle line wrapping",
	  G_CALLBACK (wrap_lines_toggled_cb), FALSE },
	{ "AutoIndent", NULL, "Enable _Auto Indent", NULL,
	  "Toggle automatic auto indentation of text",
	  G_CALLBACK (auto_indent_toggled_cb), FALSE },
	{ "InsertSpaces", NULL, "Insert _Spaces Instead of Tabs", NULL,
	  "Whether to insert space characters when inserting tabulations",
	  G_CALLBACK (insert_spaces_toggled_cb), FALSE }
};

static GtkRadioActionEntry tabs_radio_entries[] = {
	{ "TabWidth4", NULL, "4", NULL, "Set tabulation width to 4 spaces", 4 },
	{ "TabWidth6", NULL, "6", NULL, "Set tabulation width to 6 spaces", 6 },
	{ "TabWidth8", NULL, "8", NULL, "Set tabulation width to 8 spaces", 8 },
	{ "TabWidth10", NULL, "10", NULL, "Set tabulation width to 10 spaces", 10 },
	{ "TabWidth12", NULL, "12", NULL, "Set tabulation width to 12 spaces", 12 }
};

static GtkRadioActionEntry indent_radio_entries[] = {
	{ "IndentWidthUnset", NULL, "Use Tab Width", NULL, "Set indent width same as tab width", -1 },
	{ "IndentWidth4", NULL, "4", NULL, "Set indent width to 4 spaces", 4 },
	{ "IndentWidth6", NULL, "6", NULL, "Set indent width to 6 spaces", 6 },
	{ "IndentWidth8", NULL, "8", NULL, "Set indent width to 8 spaces", 8 },
	{ "IndentWidth10", NULL, "10", NULL, "Set indent width to 10 spaces", 10 },
	{ "IndentWidth12", NULL, "12", NULL, "Set indent width to 12 spaces", 12 }
};

static GtkRadioActionEntry smart_home_end_entries[] = {
	{ "SmartHomeEndDisabled", NULL, "Disabled", NULL,
	  "Smart Home/End disabled", GTK_SOURCE_SMART_HOME_END_DISABLED },
	{ "SmartHomeEndBefore", NULL, "Before", NULL,
	  "Smart Home/End before", GTK_SOURCE_SMART_HOME_END_BEFORE },
	{ "SmartHomeEndAfter", NULL, "After", NULL,
	  "Smart Home/End after", GTK_SOURCE_SMART_HOME_END_AFTER },
	{ "SmartHomeEndAlways", NULL, "Always", NULL,
	  "Smart Home/End always", GTK_SOURCE_SMART_HOME_END_ALWAYS }
};

static const gchar *view_ui_description =
"<ui>"
"  <menubar name=\"MainMenu\">"
"    <menu action=\"FileMenu\">"
"      <!--"
"      <menuitem action=\"PrintPreview\"/>"
"      -->"
"    </menu>"
"    <menu action=\"ViewMenu\">"
"      <menuitem action=\"NewView\"/>"
"      <separator/>"
"      <menuitem action=\"ShowNumbers\"/>"
"      <menuitem action=\"ShowMarkers\"/>"
"      <menuitem action=\"ShowMargin\"/>"
"      <menuitem action=\"HlLine\"/>"
"      <menuitem action=\"WrapLines\"/>"
"      <separator/>"
"      <menuitem action=\"AutoIndent\"/>"
"      <menuitem action=\"InsertSpaces\"/>"
"      <separator/>"
"      <menu action=\"TabWidth\">"
"        <menuitem action=\"TabWidth4\"/>"
"        <menuitem action=\"TabWidth6\"/>"
"        <menuitem action=\"TabWidth8\"/>"
"        <menuitem action=\"TabWidth10\"/>"
"        <menuitem action=\"TabWidth12\"/>"
"      </menu>"
"      <menu action=\"IndentWidth\">"
"        <menuitem action=\"IndentWidthUnset\"/>"
"        <menuitem action=\"IndentWidth4\"/>"
"        <menuitem action=\"IndentWidth6\"/>"
"        <menuitem action=\"IndentWidth8\"/>"
"        <menuitem action=\"IndentWidth10\"/>"
"        <menuitem action=\"IndentWidth12\"/>"
"      </menu>"
"      <separator/>"
"      <menu action=\"SmartHomeEnd\">"
"        <menuitem action=\"SmartHomeEndDisabled\"/>"
"        <menuitem action=\"SmartHomeEndBefore\"/>"
"        <menuitem action=\"SmartHomeEndAfter\"/>"
"        <menuitem action=\"SmartHomeEndAlways\"/>"
"      </menu>"
"    </menu>"
"  </menubar>"
"</ui>";

static const gchar *buffer_ui_description =
"<ui>"
"  <menubar name=\"MainMenu\">"
"    <menu action=\"FileMenu\">"
"      <menuitem action=\"Open\"/>"
"      <menuitem action=\"Print\"/>"
"      <menuitem action=\"DebugThing3\"/>"
"      <separator/>"
"      <menuitem action=\"Quit\"/>"
"    </menu>"
"    <menu action=\"ViewMenu\">"
"    </menu>"
"  </menubar>"
"</ui>";


/* File loading code ----------------------------------------------------------------- */

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
gtk_source_buffer_load_file (GtkSourceBuffer *source_buffer,
			     const gchar     *filename,
			     GError         **error)
{
	GtkTextIter iter;
	gchar *buffer;
	GError *error_here = NULL;

	g_return_val_if_fail (GTK_IS_SOURCE_BUFFER (source_buffer), FALSE);
	g_return_val_if_fail (filename != NULL, FALSE);

	if (!g_file_get_contents (filename, &buffer, NULL, &error_here))
	{
		error_dialog (NULL, "%s\nFile %s", error_here->message, filename);
		g_propagate_error (error, error_here);
		return FALSE;
	}

	gtk_source_buffer_begin_not_undoable_action (source_buffer);
	gtk_text_buffer_set_text (GTK_TEXT_BUFFER (source_buffer), buffer, -1);
	gtk_source_buffer_end_not_undoable_action (source_buffer);
	gtk_text_buffer_set_modified (GTK_TEXT_BUFFER (source_buffer), FALSE);

	/* move cursor to the beginning */
	gtk_text_buffer_get_start_iter (GTK_TEXT_BUFFER (source_buffer), &iter);
	gtk_text_buffer_place_cursor (GTK_TEXT_BUFFER (source_buffer), &iter);

	{
		GtkTextIter start, end;
		char *text;
		gtk_text_buffer_get_bounds (GTK_TEXT_BUFFER (source_buffer), &start, &end);
		text = gtk_text_buffer_get_text (GTK_TEXT_BUFFER (source_buffer), &start, &end, TRUE);
		g_assert (!strcmp (text, buffer));
		g_free (text);
	}

	g_free (buffer);
	return TRUE;
}

static void
remove_all_markers (GtkSourceBuffer *buffer)
{
	GSList *markers;
	GtkTextIter begin, end;

	gtk_text_buffer_get_bounds (GTK_TEXT_BUFFER (buffer), &begin, &end);
	markers = gtk_source_buffer_get_markers_in_region (buffer, &begin, &end);
	while (markers)
	{
		GtkSourceMarker *marker = markers->data;

		gtk_source_buffer_delete_marker (buffer, marker);
		markers = g_slist_delete_link (markers, markers);
	}
}

/* Note this is wrong for several reasons, e.g. g_pattern_match is broken
 * for glob matching. */
static GtkSourceLanguage *
get_language_for_filename (const gchar *filename)
{
	const gchar * const *languages;
	gchar *filename_utf8;
	GtkSourceLanguageManager *manager;

	filename_utf8 = g_filename_to_utf8 (filename, -1, NULL, NULL, NULL);
	g_return_val_if_fail (filename_utf8 != NULL, NULL);

	manager = gtk_source_language_manager_get_default ();
	languages = gtk_source_language_manager_get_language_ids (manager);

	while (*languages != NULL)
	{
		GtkSourceLanguage *lang;
		gchar **globs, **p;

		lang = gtk_source_language_manager_get_language (manager,
								 *languages);
		g_return_val_if_fail (GTK_IS_SOURCE_LANGUAGE (lang), NULL);
		++languages;

		globs = gtk_source_language_get_globs (lang);
		if (globs == NULL)
			continue;

		for (p = globs; *p != NULL; p++)
		{
			if (g_pattern_match_simple (*p, filename_utf8))
			{
				g_strfreev (globs);
				g_free (filename_utf8);

				return lang;
			}
		}

		g_strfreev (globs);
	}

	g_free (filename_utf8);
	return NULL;
}

/* Note this is wrong, because it ignores mime parent types and subtypes.
 * It's fine to use in a simplish program like this, but is unacceptable
 * in a serious text editor. */
static GtkSourceLanguage *
get_language_for_mime_type (const gchar *mime)
{
	const gchar * const *languages;
	GtkSourceLanguageManager *manager;

	manager = gtk_source_language_manager_get_default ();
	languages = gtk_source_language_manager_get_language_ids (manager);

	while (*languages != NULL)
	{
		GtkSourceLanguage *lang;
		gchar **mimetypes, **p;

		lang = gtk_source_language_manager_get_language (manager,
								 *languages);
		g_return_val_if_fail (GTK_IS_SOURCE_LANGUAGE (lang), NULL);
		++languages;

		mimetypes = gtk_source_language_get_mime_types (lang);

		if (mimetypes == NULL)
			continue;

		for (p = mimetypes; *p != NULL; p++)
		{
			if (strcmp (*p, mime) == 0)
			{
				g_strfreev (mimetypes);
				return lang;
			}
		}

		g_strfreev (mimetypes);
	}

	return NULL;
}

static GtkSourceLanguage *
get_language_for_file (const gchar *filename)
{
	GtkSourceLanguage *language = NULL;

#if defined(USE_GNOME_VFS)
	gchar *mime_type;
	gchar *uri;

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

	if ((mime_type = gnome_vfs_get_mime_type (uri)))
		language = get_language_for_mime_type (mime_type);

	g_free (mime_type);
	g_free (uri);
#endif

	if (!language)
		language = get_language_for_filename (filename);

	return language;
}

static GtkSourceLanguage *
get_language_by_id (const gchar *id)
{
	GtkSourceLanguageManager *manager;
	manager = gtk_source_language_manager_get_default ();
	return gtk_source_language_manager_get_language (manager, id);
}

static GtkSourceLanguage *
get_language (GtkTextBuffer *buffer, const gchar *filename)
{
	GtkSourceLanguage *language = NULL;
	GtkTextIter start, end;
	gchar *text;
	const gchar *lang_string;

	gtk_text_buffer_get_start_iter (buffer, &start);
	end = start;
	gtk_text_iter_forward_line (&end);

#define LANG_STRING "gtk-source-lang:"
	text = gtk_text_iter_get_slice (&start, &end);
	lang_string = strstr (text, LANG_STRING);
	if (lang_string != NULL)
	{
		gchar **tokens;

		lang_string += strlen (LANG_STRING);
		g_strchug (lang_string);

		tokens = g_strsplit_set (lang_string, " \t\n", 2);

		if (tokens != NULL && tokens[0] != NULL)
			language = get_language_by_id (tokens[0]);

		g_strfreev (tokens);
	}

	if (!language)
		language = get_language_for_file (filename);

	g_free (text);
	return language;
}

static gboolean
open_file (GtkSourceBuffer *buffer, const gchar *filename)
{
	GtkSourceLanguage *language = NULL;
	gchar *freeme = NULL;
	gboolean success = FALSE;

	if (!g_path_is_absolute (filename))
	{
		gchar *curdir = g_get_current_dir ();
		freeme = g_build_filename (curdir, filename, NULL);
		filename = freeme;
		g_free (curdir);
	}

	remove_all_markers (buffer);

	success = gtk_source_buffer_load_file (buffer, filename, NULL);

	if (!success)
		goto out;

	language = get_language (GTK_TEXT_BUFFER (buffer), filename);

	if (language == NULL)
		g_print ("No language found for file `%s'\n", filename);

	gtk_source_buffer_set_language (buffer, language);
	g_object_set_data_full (G_OBJECT (buffer),
				"filename", g_strdup (filename),
				(GDestroyNotify) g_free);

	if (language != NULL)
	{
		gchar **styles;

		styles = gtk_source_language_get_style_ids (language);

		if (styles == NULL)
			g_print ("No styles in language '%s'\n", gtk_source_language_get_name (language));
		else
		{
			gchar **ids;
			g_print ("Styles in in language '%s':\n", gtk_source_language_get_name (language));

			ids = styles;

			while (*ids != NULL)
			{
				const gchar *name;

				name = gtk_source_language_get_style_name (language, *ids);

				g_print ("- %s (name: '%s')\n", *ids, name);

				++ids;
			}

			g_strfreev (styles);
		}

		g_print("\n");
	}
out:
	g_free (freeme);
	return success;
}


/* View action callbacks -------------------------------------------------------- */

static void
numbers_toggled_cb (GtkAction *action, gpointer user_data)
{
	g_return_if_fail (GTK_IS_TOGGLE_ACTION (action) && GTK_IS_SOURCE_VIEW (user_data));
	gtk_source_view_set_show_line_numbers (
		GTK_SOURCE_VIEW (user_data),
		gtk_toggle_action_get_active (GTK_TOGGLE_ACTION (action)));
}

static void
markers_toggled_cb (GtkAction *action, gpointer user_data)
{
	g_return_if_fail (GTK_IS_TOGGLE_ACTION (action) && GTK_IS_SOURCE_VIEW (user_data));
	gtk_source_view_set_show_line_markers (
		GTK_SOURCE_VIEW (user_data),
		gtk_toggle_action_get_active (GTK_TOGGLE_ACTION (action)));
}

static void
margin_toggled_cb (GtkAction *action, gpointer user_data)
{
	g_return_if_fail (GTK_IS_TOGGLE_ACTION (action) && GTK_IS_SOURCE_VIEW (user_data));
	gtk_source_view_set_show_right_margin (
		GTK_SOURCE_VIEW (user_data),
		gtk_toggle_action_get_active (GTK_TOGGLE_ACTION (action)));
}

static void
hl_line_toggled_cb (GtkAction *action, gpointer user_data)
{
	g_return_if_fail (GTK_IS_TOGGLE_ACTION (action) && GTK_IS_SOURCE_VIEW (user_data));
	gtk_source_view_set_highlight_current_line (
		GTK_SOURCE_VIEW (user_data),
		gtk_toggle_action_get_active (GTK_TOGGLE_ACTION (action)));
}

static void
wrap_lines_toggled_cb (GtkAction *action, gpointer user_data)
{
	g_return_if_fail (GTK_IS_TOGGLE_ACTION (action) && GTK_IS_SOURCE_VIEW (user_data));
	gtk_text_view_set_wrap_mode (user_data,
				     gtk_toggle_action_get_active (GTK_TOGGLE_ACTION (action)) ?
					GTK_WRAP_WORD : GTK_WRAP_NONE);
}

static void
auto_indent_toggled_cb (GtkAction *action,
			gpointer   user_data)
{
	g_return_if_fail (GTK_IS_TOGGLE_ACTION (action) && GTK_IS_SOURCE_VIEW (user_data));
	gtk_source_view_set_auto_indent (
		GTK_SOURCE_VIEW (user_data),
		gtk_toggle_action_get_active (GTK_TOGGLE_ACTION (action)));
}

static void
insert_spaces_toggled_cb (GtkAction *action,
			  gpointer   user_data)
{
	g_return_if_fail (GTK_IS_TOGGLE_ACTION (action) && GTK_IS_SOURCE_VIEW (user_data));
	gtk_source_view_set_insert_spaces_instead_of_tabs (
		GTK_SOURCE_VIEW (user_data),
		gtk_toggle_action_get_active (GTK_TOGGLE_ACTION (action)));
}

static void
tabs_toggled_cb (GtkAction *action,
		 GtkAction *current,
		 gpointer user_data)
{
	g_return_if_fail (GTK_IS_RADIO_ACTION (action) && GTK_IS_SOURCE_VIEW (user_data));
	gtk_source_view_set_tab_width (
		GTK_SOURCE_VIEW (user_data),
		gtk_radio_action_get_current_value (GTK_RADIO_ACTION (action)));
}

static void
indent_toggled_cb (GtkAction *action,
		   GtkAction *current,
		   gpointer user_data)
{
	g_return_if_fail (GTK_IS_RADIO_ACTION (action) && GTK_IS_SOURCE_VIEW (user_data));
	gtk_source_view_set_indent_width (
		GTK_SOURCE_VIEW (user_data),
		gtk_radio_action_get_current_value (GTK_RADIO_ACTION (action)));
}

static void
smart_home_end_toggled_cb (GtkAction *action,
			   GtkAction *current,
			   gpointer user_data)
{
	g_return_if_fail (GTK_IS_RADIO_ACTION (action) && GTK_IS_SOURCE_VIEW (user_data));
	gtk_source_view_set_smart_home_end (
		GTK_SOURCE_VIEW (user_data),
		gtk_radio_action_get_current_value (GTK_RADIO_ACTION (action)));
}

static void
new_view_cb (GtkAction *action, gpointer user_data)
{
	GtkSourceBuffer *buffer;
	GtkSourceView *view;
	GtkWidget *window;

	g_return_if_fail (GTK_IS_SOURCE_VIEW (user_data));

	view = GTK_SOURCE_VIEW (user_data);
	buffer = GTK_SOURCE_BUFFER (gtk_text_view_get_buffer (GTK_TEXT_VIEW (view)));

	window = create_view_window (buffer, view);
	gtk_window_set_default_size (GTK_WINDOW (window), 400, 400);
	gtk_widget_show (window);
}


/* Buffer action callbacks ------------------------------------------------------------ */

static gboolean
replace_dialog (GtkWidget *widget,
		char     **what_p,
		char     **replacement_p)
{
	GtkWidget *dialog;
	GtkEntry *entry1, *entry2;
	static char *what, *replacement;

	if (!what)
		what = g_strdup ("gtk");
	if (!replacement)
		replacement = g_strdup ("boo");

	dialog = gtk_dialog_new_with_buttons ("Replace",
					      GTK_WINDOW (gtk_widget_get_toplevel (widget)),
					      GTK_DIALOG_MODAL,
					      GTK_STOCK_CANCEL,
					      GTK_RESPONSE_CANCEL,
					      GTK_STOCK_OK,
					      GTK_RESPONSE_OK,
					      NULL);
	gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);

	entry1 = g_object_new (GTK_TYPE_ENTRY,
			       "visible", TRUE,
			       "text", what ? what : "",
			       "activates-default", TRUE,
			       NULL);
	gtk_box_pack_start_defaults (GTK_BOX (GTK_DIALOG (dialog)->vbox),
				     GTK_WIDGET (entry1));
	entry2 = g_object_new (GTK_TYPE_ENTRY,
			       "visible", TRUE,
			       "text", replacement ? replacement : "",
			       "activates-default", TRUE,
			       NULL);
	gtk_box_pack_start_defaults (GTK_BOX (GTK_DIALOG (dialog)->vbox),
				     GTK_WIDGET (entry2));

	while (TRUE)
	{
		if (gtk_dialog_run (GTK_DIALOG (dialog)) != GTK_RESPONSE_OK)
		{
			gtk_widget_destroy (dialog);
			return FALSE;
		}

		if (*gtk_entry_get_text (entry1))
			break;
	}

	g_free (what);
	*what_p = what = g_strdup (gtk_entry_get_text (entry1));
	g_free (replacement);
	*replacement_p = replacement = g_strdup (gtk_entry_get_text (entry2));

	gtk_widget_destroy (dialog);
	return TRUE;
}

static void
debug_thing_3_cb (GtkAction *action,
		  gpointer   user_data)
{
	GtkTextView *view = user_data;
	GtkTextBuffer *buffer = gtk_text_view_get_buffer (view);
	GtkTextIter iter;
	char *what, *replacement;

	if (!replace_dialog (GTK_WIDGET (view), &what, &replacement))
		return;

	gtk_text_buffer_get_iter_at_offset (buffer, &iter, 0);

	while (TRUE)
	{
		GtkTextIter match_start, match_end;

		if (!gtk_text_iter_forward_search (&iter, what, 0,
						   &match_start,
						   &match_end,
						   NULL))
		{
			break;
		}

		gtk_text_buffer_delete (buffer, &match_start, &match_end);
		gtk_text_buffer_insert (buffer, &match_start, replacement, -1);
		iter = match_start;
	}
}

static void
open_file_cb (GtkAction *action, gpointer user_data)
{
	GtkWidget *chooser;
	gint response;
	static gchar *last_dir;

	g_return_if_fail (GTK_IS_SOURCE_BUFFER (user_data));

	chooser = gtk_file_chooser_dialog_new ("Open file...",
					       NULL,
					       GTK_FILE_CHOOSER_ACTION_OPEN,
					       GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
					       GTK_STOCK_OPEN, GTK_RESPONSE_OK,
					       NULL);

	if (last_dir == NULL)
		last_dir = g_strdup (TOP_SRCDIR "/gtksourceview");

	if (last_dir != NULL && g_path_is_absolute (last_dir))
		gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (chooser),
						     last_dir);

	response = gtk_dialog_run (GTK_DIALOG (chooser));

	if (response == GTK_RESPONSE_OK)
	{
		gchar *filename;

		filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (chooser));

		if (filename != NULL)
		{
			g_free (last_dir);
			last_dir = gtk_file_chooser_get_current_folder (GTK_FILE_CHOOSER (chooser));
			open_file (GTK_SOURCE_BUFFER (user_data), filename);
			g_free (filename);
		}
	}

	gtk_widget_destroy (chooser);
}

static void
begin_print (GtkPrintOperation        *operation, 
	     GtkPrintContext          *context,
	     GtkSourcePrintCompositor *compositor)
{
	gint n_pages;

	g_debug ("begin_print");

	gtk_source_print_compositor_paginate (compositor, context);

	n_pages = gtk_source_print_compositor_get_n_pages (compositor);
	gtk_print_operation_set_n_pages (operation, n_pages);
}

static void
draw_page (GtkPrintOperation        *operation,
	   GtkPrintContext          *context,
	   gint                      page_nr,
	   GtkSourcePrintCompositor *compositor)
{
	g_debug ("draw_page %d", page_nr);

	gtk_source_print_compositor_draw_page (compositor, context, page_nr);
}

static void
end_print (GtkPrintOperation        *operation, 
	   GtkPrintContext          *context,
	   GtkSourcePrintCompositor *compositor)
{
	g_object_unref (compositor);
}

#define LINE_NUMBERS_FONT_NAME   "Monospace 6"
#define HEADER_FONT_NAME   "Serif Italic 12"
#define FOOTER_FONT_NAME   "SansSerif Bold 8"

static void
print_file_cb (GtkAction *action, gpointer user_data)
{
	GtkSourceView *view;
	GtkSourceBuffer *buffer;
	GtkSourcePrintCompositor *compositor;
	GtkPrintOperation *operation;
	const gchar *filename;

	g_return_if_fail (GTK_IS_SOURCE_VIEW (user_data));

	g_debug ("print_file_cb");

	view = GTK_SOURCE_VIEW (user_data);
	buffer = GTK_SOURCE_BUFFER (gtk_text_view_get_buffer (GTK_TEXT_VIEW (view))); 

	compositor = gtk_source_print_compositor_new (buffer);

	gtk_source_print_compositor_set_tab_width (compositor,
						   gtk_source_view_get_tab_width (view));

	gtk_source_print_compositor_set_print_line_numbers (compositor, 5);

	/* To test line numbers font != text font */
	gtk_source_print_compositor_set_line_numbers_font_name (compositor,
								LINE_NUMBERS_FONT_NAME);

	gtk_source_print_compositor_set_header_format (compositor,
						       TRUE,
						       "Printed on %A",
						       "test-widget",
						       "%F");

	filename = g_object_get_data (G_OBJECT (user_data), "filename");
	
	gtk_source_print_compositor_set_footer_format (compositor,
						       TRUE,
						       "%T",
						       filename,
						       "Page %N/%Q");

	gtk_source_print_compositor_set_print_header (compositor, TRUE);
	gtk_source_print_compositor_set_print_footer (compositor, TRUE);

	gtk_source_print_compositor_set_header_font_name (compositor,
							  HEADER_FONT_NAME);

	gtk_source_print_compositor_set_footer_font_name (compositor,
							  FOOTER_FONT_NAME);
	
	operation = gtk_print_operation_new ();
	
  	g_signal_connect (G_OBJECT (operation), "begin-print", 
			  G_CALLBACK (begin_print), compositor);		          
	g_signal_connect (G_OBJECT (operation), "draw-page", 
			  G_CALLBACK (draw_page), compositor);
	g_signal_connect (G_OBJECT (operation), "end-print", 
			  G_CALLBACK (end_print), compositor);

	gtk_print_operation_run (operation, 
				 GTK_PRINT_OPERATION_ACTION_PRINT_DIALOG,
				 NULL, NULL);

	g_object_unref (operation);
}

/* View UI callbacks ------------------------------------------------------------------ */

static void
update_cursor_position (GtkTextBuffer *buffer, gpointer user_data)
{
	gchar *msg;
	gint row, col, chars, tabwidth;
	GtkTextIter iter, start;
	GtkSourceView *view;
	GtkLabel *pos_label;

	g_return_if_fail (GTK_IS_SOURCE_VIEW (user_data));

	view = GTK_SOURCE_VIEW (user_data);
	tabwidth = gtk_source_view_get_tab_width (view);
	pos_label = GTK_LABEL (g_object_get_data (G_OBJECT (view), "pos_label"));

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
			col += (tabwidth - (col % tabwidth));
		}
		else
			++col;

		gtk_text_iter_forward_char (&start);
	}

	msg = g_strdup_printf ("char: %d, line: %d, column: %d", chars, row, col);
	gtk_label_set_text (pos_label, msg);
      	g_free (msg);
}

static void
move_cursor_cb (GtkTextBuffer *buffer,
		GtkTextIter   *cursoriter,
		GtkTextMark   *mark,
		gpointer       user_data)
{
	if (mark != gtk_text_buffer_get_insert (buffer))
		return;

	update_cursor_position (buffer, user_data);
}

static gboolean
window_deleted_cb (GtkWidget *widget, GdkEvent *ev, gpointer user_data)
{
	g_return_val_if_fail (GTK_IS_SOURCE_VIEW (user_data), TRUE);

	if (g_list_nth_data (windows, 0) == widget)
	{
		/* Main (first in the list) window was closed, so exit
		 * the application */
		gtk_main_quit ();
	}
	else
	{
		GtkSourceView *view = GTK_SOURCE_VIEW (user_data);
		GtkSourceBuffer *buffer = GTK_SOURCE_BUFFER (
			gtk_text_view_get_buffer (GTK_TEXT_VIEW (view)));

		windows = g_list_remove (windows, widget);

		/* deinstall buffer motion signal handlers */
		g_signal_handlers_disconnect_matched (buffer,
						      G_SIGNAL_MATCH_DATA,
						      0, /* signal_id */
						      0, /* detail */
						      NULL, /* closure */
						      NULL, /* func */
						      user_data);

		/* we return FALSE since we want the window destroyed */
		return FALSE;
	}

	return TRUE;
}

static gboolean
button_press_cb (GtkWidget *widget, GdkEventButton *ev, gpointer user_data)
{
	GtkSourceView *view;
	GtkSourceBuffer *buffer;

	g_return_val_if_fail (GTK_IS_SOURCE_VIEW (widget), FALSE);

	view = GTK_SOURCE_VIEW (widget);
	buffer = GTK_SOURCE_BUFFER (gtk_text_view_get_buffer (GTK_TEXT_VIEW (view)));

	if (!gtk_source_view_get_show_line_markers (view))
		return FALSE;

	/* check that the click was on the left gutter */
	if (ev->window == gtk_text_view_get_window (GTK_TEXT_VIEW (view),
						    GTK_TEXT_WINDOW_LEFT))
	{
		gint y_buf;
		GtkTextIter line_start, line_end;
		GSList *marker_list, *list_iter;
		GtkSourceMarker *marker;
		const gchar *marker_type;

		if (ev->button == 1)
			marker_type = MARKER_TYPE_1;
		else
			marker_type = MARKER_TYPE_2;

		gtk_text_view_window_to_buffer_coords (GTK_TEXT_VIEW (view),
						       GTK_TEXT_WINDOW_LEFT,
						       ev->x, ev->y,
						       NULL, &y_buf);

		/* get line bounds */
		gtk_text_view_get_line_at_y (GTK_TEXT_VIEW (view),
					     &line_start,
					     y_buf,
					     NULL);

		line_end = line_start;
		gtk_text_iter_forward_to_line_end (&line_end);

		/* get the markers already in the line */
		marker_list = gtk_source_buffer_get_markers_in_region (buffer,
								       &line_start,
								       &line_end);

		/* search for the marker corresponding to the button pressed */
		marker = NULL;
		for (list_iter = marker_list;
		     list_iter && !marker;
		     list_iter = g_slist_next (list_iter))
		{
			GtkSourceMarker *tmp = list_iter->data;
			gchar *tmp_type = gtk_source_marker_get_marker_type (tmp);

			if (tmp_type && !strcmp (tmp_type, marker_type))
			{
				marker = tmp;
			}
			g_free (tmp_type);
		}
		g_slist_free (marker_list);

		if (marker)
		{
			/* a marker was found, so delete it */
			gtk_source_buffer_delete_marker (buffer, marker);
		}
		else
		{
			/* no marker found -> create one */
			marker = gtk_source_buffer_create_marker (buffer, NULL,
								  marker_type, &line_start);
		}
	}

	return FALSE;
}


/* Window creation functions -------------------------------------------------------- */

static GtkWidget *
create_view_window (GtkSourceBuffer *buffer, GtkSourceView *from)
{
	GtkWidget *window, *sw, *view, *vbox, *pos_label, *menu;
	PangoFontDescription *font_desc = NULL;
	GdkPixbuf *pixbuf;
	GtkAccelGroup *accel_group;
	GtkActionGroup *action_group;
	GtkUIManager *ui_manager;
	GError *error;

	g_return_val_if_fail (GTK_IS_SOURCE_BUFFER (buffer), NULL);
	g_return_val_if_fail (from == NULL || GTK_IS_SOURCE_VIEW (from), NULL);

	/* window */
	window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_container_set_border_width (GTK_CONTAINER (window), 0);
	gtk_window_set_title (GTK_WINDOW (window), "GtkSourceView Demo");
	windows = g_list_append (windows, window);

	/* view */
	view = gtk_source_view_new_with_buffer (buffer);

	if (style_scheme)
		gtk_source_buffer_set_style_scheme (buffer, style_scheme);

	g_signal_connect (buffer, "mark_set", G_CALLBACK (move_cursor_cb), view);
	g_signal_connect (buffer, "changed", G_CALLBACK (update_cursor_position), view);
	g_signal_connect (view, "button-press-event", G_CALLBACK (button_press_cb), NULL);
	g_signal_connect (window, "delete-event", (GCallback) window_deleted_cb, view);

	/* action group and UI manager */
	action_group = gtk_action_group_new ("ViewActions");
	gtk_action_group_add_actions (action_group, view_action_entries,
				      G_N_ELEMENTS (view_action_entries), view);
	gtk_action_group_add_toggle_actions (action_group, toggle_entries,
					     G_N_ELEMENTS (toggle_entries), view);
	gtk_action_group_add_radio_actions (action_group, tabs_radio_entries,
					    G_N_ELEMENTS (tabs_radio_entries),
					    -1, G_CALLBACK (tabs_toggled_cb), view);
	gtk_action_group_add_radio_actions (action_group, indent_radio_entries,
					    G_N_ELEMENTS (indent_radio_entries),
					    -1, G_CALLBACK (indent_toggled_cb), view);
	gtk_action_group_add_radio_actions (action_group, smart_home_end_entries,
					    G_N_ELEMENTS (smart_home_end_entries),
					    -1, G_CALLBACK (smart_home_end_toggled_cb), view);

	ui_manager = gtk_ui_manager_new ();
	gtk_ui_manager_insert_action_group (ui_manager, action_group, 0);
	g_object_unref (action_group);

	/* save a reference to the ui manager in the window for later use */
	g_object_set_data_full (G_OBJECT (window), "ui_manager",
				ui_manager, (GDestroyNotify) g_object_unref);

	accel_group = gtk_ui_manager_get_accel_group (ui_manager);
	gtk_window_add_accel_group (GTK_WINDOW (window), accel_group);

	error = NULL;
	if (!gtk_ui_manager_add_ui_from_string (ui_manager, view_ui_description, -1, &error))
	{
		g_message ("building view ui failed: %s", error->message);
		g_error_free (error);
		exit (1);
	}

	/* misc widgets */
	vbox = gtk_vbox_new (0, FALSE);
	sw = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (sw),
                                             GTK_SHADOW_IN);
	pos_label = gtk_label_new ("Position");
	g_object_set_data (G_OBJECT (view), "pos_label", pos_label);
	menu = gtk_ui_manager_get_widget (ui_manager, "/MainMenu");

	/* layout widgets */
	gtk_container_add (GTK_CONTAINER (window), vbox);
	gtk_box_pack_start (GTK_BOX (vbox), menu, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (vbox), sw, TRUE, TRUE, 0);
	gtk_container_add (GTK_CONTAINER (sw), view);
	gtk_box_pack_start (GTK_BOX (vbox), pos_label, FALSE, FALSE, 0);

	/* setup view */
	font_desc = pango_font_description_from_string ("monospace");
	if (font_desc != NULL)
	{
		gtk_widget_modify_font (view, font_desc);
		pango_font_description_free (font_desc);
	}

	/* change view attributes to match those of from */
	if (from)
	{
		gchar *tmp;
		gint i;
		GtkAction *action;

		action = gtk_action_group_get_action (action_group, "ShowNumbers");
		gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action),
					      gtk_source_view_get_show_line_numbers (from));

		action = gtk_action_group_get_action (action_group, "ShowMarkers");
		gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action),
					      gtk_source_view_get_show_line_markers (from));

		action = gtk_action_group_get_action (action_group, "ShowMargin");
		gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action),
					      gtk_source_view_get_show_right_margin (from));

		action = gtk_action_group_get_action (action_group, "HlLine");
		gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action),
					      gtk_source_view_get_highlight_current_line (from));

		action = gtk_action_group_get_action (action_group, "WrapLines");
		gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action),
					      gtk_text_view_get_wrap_mode (GTK_TEXT_VIEW (from)) != GTK_WRAP_NONE);

		action = gtk_action_group_get_action (action_group, "AutoIndent");
		gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action),
					      gtk_source_view_get_auto_indent (from));

		action = gtk_action_group_get_action (action_group, "InsertSpaces");
		gtk_toggle_action_set_active (
			GTK_TOGGLE_ACTION (action),
			gtk_source_view_get_insert_spaces_instead_of_tabs (from));

		tmp = g_strdup_printf ("TabWidth%d", gtk_source_view_get_tab_width (from));
		action = gtk_action_group_get_action (action_group, tmp);
		if (action)
			gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action), TRUE);
		g_free (tmp);

		i = gtk_source_view_get_indent_width (from);
		tmp = i < 0 ? g_strdup ("IndentWidthUnset") : g_strdup_printf ("IndentWidth%d", i);
		action = gtk_action_group_get_action (action_group, tmp);
		if (action)
			gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action), TRUE);
		g_free (tmp);
	}

	/* add marker pixbufs */
	error = NULL;
	if ((pixbuf = gdk_pixbuf_new_from_file (DATADIR "/pixmaps/apple-green.png", &error)))
	{
		gtk_source_view_set_marker_pixbuf (GTK_SOURCE_VIEW (view), MARKER_TYPE_1, pixbuf);
		g_object_unref (pixbuf);
	}
	else
	{
		g_message ("could not load marker 1 image.  Spurious messages might get triggered: %s",
		error->message);
		g_error_free (error);
	}

	error = NULL;
	if ((pixbuf = gdk_pixbuf_new_from_file (DATADIR "/pixmaps/apple-red.png", &error)))
	{
		gtk_source_view_set_marker_pixbuf (GTK_SOURCE_VIEW (view), MARKER_TYPE_2, pixbuf);
		g_object_unref (pixbuf);
	}
	else
	{
		g_message ("could not load marker 2 image.  Spurious messages might get triggered: %s",
		error->message);
		g_error_free (error);
	}

	gtk_widget_show_all (vbox);

	return window;
}

static GtkWidget *
create_main_window (GtkSourceBuffer *buffer)
{
	GtkWidget *window;
	GtkAction *action;
	GtkUIManager *ui_manager;
	GtkActionGroup *action_group;
	GList *groups;
	GError *error;

	window = create_view_window (buffer, NULL);
	ui_manager = g_object_get_data (G_OBJECT (window), "ui_manager");

	/* buffer action group */
	action_group = gtk_action_group_new ("BufferActions");
	gtk_action_group_add_actions (action_group, buffer_action_entries,
				      G_N_ELEMENTS (buffer_action_entries), buffer);
	gtk_ui_manager_insert_action_group (ui_manager, action_group, 1);
	g_object_unref (action_group);

	/* merge buffer ui */
	error = NULL;
	if (!gtk_ui_manager_add_ui_from_string (ui_manager, buffer_ui_description, -1, &error))
	{
		g_message ("building buffer ui failed: %s", error->message);
		g_error_free (error);
		exit (1);
	}

	/* preselect menu checkitems */
	groups = gtk_ui_manager_get_action_groups (ui_manager);
	/* retrieve the view action group at position 0 in the list */
	action_group = g_list_nth_data (groups, 0);

	action = gtk_action_group_get_action (action_group, "ShowNumbers");
	gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action), TRUE);

	action = gtk_action_group_get_action (action_group, "ShowMarkers");
	gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action), TRUE);

	action = gtk_action_group_get_action (action_group, "ShowMargin");
	gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action), FALSE);

	action = gtk_action_group_get_action (action_group, "AutoIndent");
	gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action), TRUE);

	action = gtk_action_group_get_action (action_group, "InsertSpaces");
	gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action), FALSE);

	action = gtk_action_group_get_action (action_group, "TabWidth8");
	gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action), TRUE);

	action = gtk_action_group_get_action (action_group, "IndentWidthUnset");
	gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action), TRUE);

	return window;
}


/* XML memory management and verification functions ----------------------------- */

#ifdef TEST_XML_MEM

#define ALIGN 8

/* my_free(malloc(n)) and free(my_malloc(n)) are invalid and
 * abort on glibc */

static gpointer
my_malloc (gsize n_bytes)
{
	char *mem = malloc (n_bytes + ALIGN);
	return mem ? mem + ALIGN : NULL;
}

static gpointer
my_realloc (gpointer mem,
	    gsize    n_bytes)
{
	if (mem)
	{
		char *new_mem = realloc ((char*) mem - ALIGN, n_bytes + ALIGN);
		return new_mem ? new_mem + ALIGN : NULL;
	}
	else
	{
		return my_malloc (n_bytes);
	}
}

static char *
my_strdup (const char *s)
{
	if (s)
	{
		char *new_s = my_malloc (strlen (s) + 1);
		strcpy (new_s, s);
		return new_s;
	}
	else
	{
		return NULL;
	}
}

static void
my_free (gpointer mem)
{
	if (mem)
		free ((char*) mem - ALIGN);
}

static void
init_mem_stuff (void)
{
	if (1)
	{
		if (xmlMemSetup (my_free, my_malloc, my_realloc, my_strdup) != 0)
			g_warning ("xmlMemSetup() failed");
	}
	else
	{
		GMemVTable mem_table = {
			my_malloc,
			my_realloc,
			my_free,
			NULL, NULL, NULL
		};

		g_mem_set_vtable (&mem_table);
		g_slice_set_config (G_SLICE_CONFIG_ALWAYS_MALLOC, TRUE);
	}
}

#endif /* TEST_XML_MEM */


/* Program entry point ------------------------------------------------------------ */

int
main (int argc, char *argv[])
{
	GtkWidget *window;
	GtkSourceLanguageManager *lm;
	GtkSourceStyleSchemeManager *sm;
	GtkSourceBuffer *buffer;

	gchar *builtin_lang_dirs[] = {TOP_SRCDIR "/gtksourceview/language-specs", NULL};
	gchar *builtin_sm_dirs[] = {TOP_SRCDIR "/gtksourceview/language-specs", NULL};
	gchar **lang_dirs;
	const gchar * const * schemes;
	gboolean use_default_paths = FALSE;

	gchar *style_scheme_id = NULL;
	GOptionContext *context;

	GOptionEntry entries[] = {
	  { "style-scheme", 's', 0, G_OPTION_ARG_STRING, &style_scheme_id, "Style scheme name to use", "SCHEME"},
	  { "default-paths", 'd', 0, G_OPTION_ARG_NONE, &use_default_paths, "Style scheme name to use", "SCHEME"},
	  { NULL }
	};

	g_thread_init (NULL);

#ifdef TEST_XML_MEM
	init_mem_stuff ();
#endif

	context = g_option_context_new ("- test GtkSourceView widget");
	g_option_context_add_main_entries (context, entries, NULL);
	g_option_context_add_group (context, gtk_get_option_group (TRUE));
	g_option_context_parse (context, &argc, &argv, NULL);

// 	gdk_window_set_debug_updates (TRUE);

#ifdef USE_GNOME_VFS
	gnome_vfs_init ();
#endif

	/* we do not use defaults so we don't need to install the library */
	lang_dirs = use_default_paths ? NULL : builtin_lang_dirs;
	lm = gtk_source_language_manager_get_default ();
	gtk_source_language_manager_set_search_path (lm, lang_dirs);

	lang_dirs = use_default_paths ? NULL : builtin_sm_dirs;

	sm = gtk_source_style_scheme_manager_get_default ();
	gtk_source_style_scheme_manager_set_search_path (sm, lang_dirs);

	if (!use_default_paths)
		gtk_source_style_scheme_manager_append_search_path (sm, TOP_SRCDIR "/tests/test-scheme.xml");

	schemes = gtk_source_style_scheme_manager_get_scheme_ids (sm);
	g_print ("Available style schemes:\n");
	while (*schemes != NULL)
	{
		const gchar* const *authors;
		gchar *authors_str = NULL;

		style_scheme = gtk_source_style_scheme_manager_get_scheme (sm, *schemes);

		authors = gtk_source_style_scheme_get_authors (style_scheme);
		if (authors != NULL)
			authors_str = g_strjoinv (", ", (gchar **)authors);

		g_print (" - [%s] %s: %s\n",
			 gtk_source_style_scheme_get_id (style_scheme),
			 gtk_source_style_scheme_get_name (style_scheme),
			 gtk_source_style_scheme_get_description (style_scheme) ?
				gtk_source_style_scheme_get_description (style_scheme) : "");

		if (authors_str) {
			g_print ("   by %s\n",  authors_str);
			g_free (authors_str);
		}

		++schemes;
	}
	g_print("\n");

	if (style_scheme_id != NULL)
		style_scheme = gtk_source_style_scheme_manager_get_scheme (sm, style_scheme_id);
	else
		style_scheme = NULL;

	/* create buffer */
	buffer = gtk_source_buffer_new (NULL);

	if (argc > 1)
		open_file (buffer, argv [1]);
	else
		open_file (buffer, TOP_SRCDIR "/gtksourceview/gtksourcebuffer.c");

	/* create first window */
	window = create_main_window (buffer);
	gtk_window_set_default_size (GTK_WINDOW (window), 500, 500);
	gtk_widget_show (window);

	/* ... and action! */
	gtk_main ();

	/* cleanup */
	g_list_foreach (windows, (GFunc) gtk_widget_destroy, NULL);
	g_list_free (windows);
	g_object_unref (buffer);

	g_free (style_scheme_id);

#ifdef USE_GNOME_VFS
	gnome_vfs_shutdown ();
#endif

	return 0;
}
