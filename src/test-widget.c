/*  Copyright (C) 2001
 *  Mikael Hermansson<tyan@linux.se>
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
#include "gtktextsearch.h"
#include "gtksourceview.h"
#include "gtksourcelanguage.h"
#include "gtksourcelanguagesmanager.h"

static GtkTextBuffer *test_source (GtkSourceBuffer *buf);
static void cb_move_cursor (GtkTextBuffer *buf, GtkTextIter *cursoriter, GtkTextMark *mark,
			    gpointer data);

static gchar *
gtk_source_buffer_convert_to_html (GtkSourceBuffer * buffer,
				   const gchar * title)
{
	gchar txt[3];
	GtkTextIter iter;
	gboolean font = FALSE;
	gboolean bold = FALSE;
	gboolean italic = FALSE;
	gboolean underline = FALSE;
	GString *str = NULL;
	GSList *list = NULL;
	GtkTextTag *tag = NULL;
	GdkColor *col = NULL;
	GValue *value;
	gunichar c = 0;

	txt[1] = 0;

	g_return_val_if_fail (GTK_IS_SOURCE_BUFFER (buffer), NULL);

	gtk_text_buffer_get_iter_at_offset (GTK_TEXT_BUFFER (buffer),
					    &iter, 0);

	str = g_string_new ("<html>\n");
	g_string_append (str, "<head>\n");
	g_string_sprintfa (str, "<title>%s</title>\n",
			   title ? title : "GtkSourceView converter");
	g_string_append (str, "</head>\n");
	g_string_append (str, "<body bgcolor=white>\n");
	g_string_append (str, "<pre>");


	while (!gtk_text_iter_is_end (&iter)) {
		c = gtk_text_iter_get_char (&iter);
		if (!tag) {
			list =
			    gtk_text_iter_get_toggled_tags (&iter, TRUE);
			if (list && g_slist_last (list)->data) {
				tag =
				    GTK_TEXT_TAG (g_slist_last (list)->
						  data);
				g_slist_free (list);
			}
			if (tag && !gtk_text_iter_ends_tag (&iter, tag)) {
				GValue val1 = { 0, };
				GValue val2 = { 0, };
				GValue val3 = { 0, };

				value = &val1;
				g_value_init (value, GDK_TYPE_COLOR);
				g_object_get_property (G_OBJECT (tag),
						       "foreground_gdk",
						       value);
				col = g_value_get_boxed (value);
				if (col) {
					g_string_sprintfa (str,
							   "<font color=#%02X%02X%02X>",
							   col->red / 256,
							   col->green /
							   256,
							   col->blue /
							   256);
					font = TRUE;
				}
				value = &val2;
				g_value_init (value, G_TYPE_INT);
				g_object_get_property (G_OBJECT (tag),
						       "weight", value);
				if (g_value_get_int (value) ==
				    PANGO_WEIGHT_BOLD) {
					g_string_append (str, "<b>");
					bold = TRUE;
				}

				value = &val3;
				g_value_init (value, PANGO_TYPE_STYLE);
				g_object_get_property (G_OBJECT (tag),
						       "style", value);
				if (g_value_get_enum (value) ==
				    PANGO_STYLE_ITALIC) {
					g_string_append (str, "<i>");
					italic = TRUE;
				}

			}
		}

		if (c == '<')
			g_string_append (str, "&lt");
		else if (c == '>')
			g_string_append (str, "&gt");
		else {
			txt[0] = c;
			g_string_append (str, txt);
		}

		gtk_text_iter_forward_char (&iter);
		if (tag && gtk_text_iter_ends_tag (&iter, tag)) {
			if (bold)
				g_string_append (str, "</b>");
			if (italic)
				g_string_append (str, "</i>");
			if (underline)
				g_string_append (str, "</u>");
			if (font)
				g_string_append (str, "</font>");
			tag = NULL;
			bold = italic = underline = font = FALSE;
		}
	}
	g_string_append (str, "</pre>");
	g_string_append (str, "</body>");
	g_string_append (str, "</html>");

	return g_string_free (str, FALSE);
}

static gboolean
read_loop (GtkTextBuffer * buffer,
	   const char *filename, GIOChannel * io, GError ** error)
{
	GIOStatus status;
	GtkWidget *widget;
	GtkTextIter end;
	gchar *str = NULL;
	gint size = 0;
	*error = NULL;

	gtk_text_buffer_get_end_iter (GTK_TEXT_BUFFER (buffer), &end);
	if ((status =
	     g_io_channel_read_line (io, &str, &size, NULL,
				     error)) == G_IO_STATUS_NORMAL
	    && size) {
#ifdef DEBUG_SOURCEVIEW
		puts (str);
#endif
		gtk_text_buffer_insert (GTK_TEXT_BUFFER (buffer),
					&end, str, size);
		g_free (str);
		return TRUE;
	} else if (!*error
		   && (status =
		       g_io_channel_read_to_end (io, &str, &size,
						 error)) ==
		   G_IO_STATUS_NORMAL && size) {
#ifdef DEBUG_SOURCEVIEW
		puts (str);
#endif
		gtk_text_buffer_insert (GTK_TEXT_BUFFER (buffer),
					&end, str, size);
		g_free (str);

		return TRUE;
	}

	if (status == G_IO_STATUS_EOF && !*error)
		return FALSE;

	if (!*error)
		return FALSE;

	widget = gtk_message_dialog_new (NULL,
					 (GtkDialogFlags) 0,
					 GTK_MESSAGE_ERROR,
					 GTK_BUTTONS_OK,
					 "%s\nFile %s",
					 (*error)->message, filename);
	gtk_dialog_run (GTK_DIALOG (widget));
	gtk_widget_destroy (widget);

	/* because of error in input we clear already loaded text */
	gtk_text_buffer_set_text (buffer, "", 0);

	return FALSE;
}

static gboolean
gtk_source_buffer_load_with_character_encoding (GtkSourceBuffer * buffer,
						const gchar * filename,
						const gchar * encoding,
						GError ** error)
{
	GIOChannel *io;
	GtkWidget *widget;
	gboolean highlight = FALSE;
	*error = NULL;

	g_return_val_if_fail (buffer != NULL, FALSE);
	g_return_val_if_fail (filename != NULL, FALSE);
	g_return_val_if_fail (GTK_IS_SOURCE_BUFFER (buffer), FALSE);

	highlight = gtk_source_buffer_get_highlight (buffer);

	io = g_io_channel_new_file (filename, "r", error);
	if (!io) {
		widget = gtk_message_dialog_new (NULL,
						 (GtkDialogFlags) 0,
						 GTK_MESSAGE_ERROR,
						 GTK_BUTTONS_OK,
						 "%s\nFile %s",
						 (*error)->message,
						 filename);
		gtk_dialog_run (GTK_DIALOG (widget));
		gtk_widget_destroy (widget);

		return FALSE;
	}

	if (g_io_channel_set_encoding (io, encoding, error) !=
	    G_IO_STATUS_NORMAL) {
		widget =
		    gtk_message_dialog_new (NULL, (GtkDialogFlags) 0,
					    GTK_MESSAGE_ERROR,
					    GTK_BUTTONS_OK,					 
					    "Failed to set encoding:\n%s\n%s",
					    filename, (*error)->message);

		gtk_dialog_run (GTK_DIALOG (widget));
		gtk_widget_destroy (widget);
		g_io_channel_unref (io);

		return FALSE;
	}

/* 	if (highlight) */
/* 		gtk_source_buffer_set_highlight (buffer, FALSE); */

	gtk_source_buffer_begin_not_undoable_action (buffer);

	while (!*error
	       && read_loop (GTK_TEXT_BUFFER (buffer), filename, io,
			     error));

	gtk_source_buffer_end_not_undoable_action (buffer);

	g_io_channel_unref (io);

	if (*error)
		return FALSE;

	gtk_text_buffer_set_modified (GTK_TEXT_BUFFER (buffer), FALSE);

/* 	if (highlight) */
/* 		gtk_source_buffer_set_highlight (buffer, TRUE); */

	return TRUE;
}

static gboolean
gtk_source_buffer_load (GtkSourceBuffer * buffer,
			const gchar * filename, GError ** error)
{
	g_return_val_if_fail (buffer != NULL, FALSE);
	g_return_val_if_fail (filename != NULL, FALSE);
	g_return_val_if_fail (GTK_IS_SOURCE_BUFFER (buffer), FALSE);

	return gtk_source_buffer_load_with_character_encoding (buffer,
							       filename,
							       NULL,
							       error);
}

GtkTextBuffer *
test_source (GtkSourceBuffer *buffer)
{
	/*
	GtkTextTag *tag;
	*/
	GtkTextTagTable *table;
	GSList *list = NULL;
	GError *err = NULL;

	GtkSourceLanguage *language;
	/*
	GSList *keywords = NULL;
	*/
	if (!buffer)
		buffer = GTK_SOURCE_BUFFER (gtk_source_buffer_new (NULL));

	table = GTK_TEXT_BUFFER (buffer)->tag_table;
#if 0
	tag = gtk_pattern_tag_new ("gnu_typedef", "\\b\\(Gtk\\|Gdk\\|Gnome\\)[a-zA-Z0-9_]+");
	g_object_set (G_OBJECT (tag), "foreground", "blue", NULL);
	list = g_list_append (list, (gpointer) tag);

	tag = gtk_pattern_tag_new ("numbers", "\\b[0-9]+\\.?\\b");
	g_object_set (G_OBJECT (tag), "weight", PANGO_WEIGHT_BOLD, NULL);
	list = g_list_append (list, (gpointer) tag);

	keywords = g_slist_append (keywords, "int");
	keywords = g_slist_append (keywords, "float");
	keywords = g_slist_append (keywords, "enum");
	keywords = g_slist_append (keywords, "bool");
	keywords = g_slist_append (keywords, "char");
	keywords = g_slist_append (keywords, "void");
	keywords = g_slist_append (keywords, "sizeof");
	keywords = g_slist_append (keywords, "static");
	keywords = g_slist_append (keywords, "const");
	keywords = g_slist_append (keywords, "typedef");
	keywords = g_slist_append (keywords, "struct");
	keywords = g_slist_append (keywords, "class");

	tag = gtk_keyword_list_tag_new ("types", keywords, TRUE, TRUE, TRUE, NULL, NULL);
	/*
	tag = gtk_pattern_tag_new ("types",
				   "\\b\\(int\\|float\\|enum\\|bool\\|char\\|void\\|gint\\|"
				   "gchar\\|gpointer\\|guint\\|guchar\\|static\\|const\\|"
				   "typedef\\|struct\\|class\\|gboolean\\|sizeof\\)\\b");
	*/
	g_slist_free (keywords);
	
	g_object_set (G_OBJECT (tag), "foreground", "navy", /*"weight", PANGO_WEIGHT_BOLD,*/ NULL);
	list = g_list_append (list, (gpointer) tag);

	tag = gtk_pattern_tag_new ("gtk_functions", "\\b\\(gtk\\|gdk\\|g\\|gnome\\)_[a-zA-Z0-9_]+");
	g_object_set (G_OBJECT (tag), "foreground", "brown", NULL);
	list = g_list_append (list, (gpointer) tag);

	tag = gtk_pattern_tag_new ("functions", "^[a-zA-Z_]*\\:");
	g_object_set (G_OBJECT (tag), "foreground", "navy", NULL);
	list = g_list_append (list, (gpointer) tag);
/*
	tag = gtk_pattern_tag_new ("macro", "\\b[A-Z_][A-Z0-9_\\-]+\\b");
	g_object_set (G_OBJECT (tag), "foreground", "red", NULL);
	list = g_list_append (list, (gpointer) tag);
*/

	tag = gtk_pattern_tag_new ("operators",
				   "\\(\\*\\|\\*\\*\\|->\\|::\\|<<\\|>>\\|>\\|<\\|=\\|==\\|!=\\|<=\\|>=\\|++\\|--\\|%\\|+\\|-\\|||\\|&&\\|!\\|+=\\|-=\\|\\*=\\|/=\\|%=\\)");
	g_object_set (G_OBJECT (tag), "foreground", "green", NULL);
	list = g_list_append (list, (gpointer) tag);

	tag = gtk_pattern_tag_new ("char_string", "'\\?[a-zA-Z0-9_\\()#@!$%^&*-=+\"{}<)]'");
	g_object_set (G_OBJECT (tag), "foreground", "orange", NULL);
	list = g_list_append (list, (gpointer) tag);

	keywords = NULL;
	keywords = g_slist_append (keywords, "include");
	keywords = g_slist_append (keywords, "if");
	keywords = g_slist_append (keywords, "ifdef");
	keywords = g_slist_append (keywords, "ifndef");
	keywords = g_slist_append (keywords, "else");
	keywords = g_slist_append (keywords, "elif");
	keywords = g_slist_append (keywords, "define");
	keywords = g_slist_append (keywords, "endif");
	keywords = g_slist_append (keywords, "pragma");
	keywords = g_slist_append (keywords, "undef");

	tag = gtk_keyword_list_tag_new ("defs", keywords, TRUE, FALSE, TRUE, "^[ \t]*#[ \t]*", NULL);
	g_object_set (G_OBJECT (tag), "foreground", "tomato3", NULL);
	list = g_list_append (list, (gpointer) tag);
	g_slist_free (keywords);

	tag = gtk_pattern_tag_new ("keywords",
				   "\\b\\(do\\|while\\|for\\|if\\|else\\|switch\\|case\\|"
				   "return\\|public\\|protected\\|private\\|false\\|"
				   "true\\|break\\|extern\\|inline\\|this\\|dynamic_cast\\|"
				   "static_cast\\|template\\|cin\\|cout\\)\\b");
	g_object_set (G_OBJECT (tag), "foreground", "blue", "weight", PANGO_WEIGHT_BOLD, NULL);
	list = g_list_append (list, (gpointer) tag);

	
	tag = gtk_line_comment_tag_new ("comment", "//");
	g_object_set (G_OBJECT (tag), "foreground", "gray", "style", PANGO_STYLE_ITALIC, NULL);
	list = g_list_append (list, (gpointer) tag);

	tag = gtk_block_comment_tag_new ("comment_multiline", "/\\*", "\\*/");
	g_object_set (G_OBJECT (tag), "foreground", "gray", "style", PANGO_STYLE_ITALIC, NULL);
	list = g_list_append (list, (gpointer) tag);

	tag = gtk_string_tag_new ("string", "\"", "\"", TRUE);
	g_object_set (G_OBJECT (tag), "foreground", "forest green", NULL);
	list = g_list_append (list, (gpointer) tag);
#endif
	language = gtk_source_language_get_from_mime_type ("text/x-c");
		
	if (language != NULL)
	{
		g_print ("Name: %s\n", gtk_source_language_get_name (language));
		
		list = gtk_source_language_get_tags (language);
		
		gtk_source_buffer_install_regex_tags (buffer, list);
		g_slist_foreach (list, (GFunc) g_object_unref, NULL);
		g_slist_free (list);

		g_object_unref (language);
		
	}
	else
		g_print ("No language found.");	


#if 0
	gtk_source_buffer_load (buffer, "test-widget.c", &err);
	gtk_source_buffer_load (buffer, "/home/gustavo/cvs/gnome/gnome-python/pygtk/gtk/gtk.c", &err);
#else
	gtk_source_buffer_load (buffer, "gtksourcebuffer.c", &err);	
#endif
	
#ifdef OLD
	if (g_file_get_contents ("gtksourcebuffer.c", &txt, &len, &error)) {
		gtk_text_buffer_set_text (GTK_TEXT_BUFFER (buffer), txt, len);
	} else {
		GtkWidget *w;

		w = gtk_message_dialog_new (NULL, 0, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
					    error->message);
		gtk_dialog_run (GTK_DIALOG (w));
		gtk_widget_destroy (w);
		g_error_free (error);
	}
#endif

	return GTK_TEXT_BUFFER (buffer);
}
static gboolean
cb_func(GtkTextIter *iter1, GtkTextIter *iter2, gpointer data)
{
  gtk_text_buffer_delete(GTK_TEXT_BUFFER(gtk_text_iter_get_buffer(iter1)), iter1, iter2);

  gtk_text_buffer_insert (GTK_TEXT_BUFFER(gtk_text_iter_get_buffer(iter1)), iter1, "FUCK", 4);



  return FALSE;
}

static void
cb_entry_activate (GtkWidget *widget, gpointer data)
{
  GtkTextSearch *search;
  char *txt;
  txt = gtk_editable_get_chars(GTK_EDITABLE(widget), 0, -1);
 

  search = gtk_text_search_new (GTK_TEXT_BUFFER(data), NULL, txt, GTK_ETEXT_SEARCH_TEXT_ONLY | GTK_ETEXT_SEARCH_CASE_INSENSITIVE, NULL);  
  gtk_text_search_forward_foreach(search, cb_func, NULL);
/*
  if (gtk_text_search_forward (search, &iter1, &iter2))  {
     gtk_text_buffer_place_cursor (GTK_TEXT_BUFFER(data), &iter1); 
     gtk_text_buffer_move_mark (GTK_TEXT_BUFFER(data), gtk_text_buffer_get_insert(GTK_TEXT_BUFFER(data)), &iter2); 
  }
*/

  g_free(txt);
}

static void
cb_convert (GtkWidget *widget, gpointer data)
{
	FILE *hf;
	gchar *txt;

	txt = gtk_source_buffer_convert_to_html (GTK_SOURCE_BUFFER (data), "This is a test");
	if (txt) {
		hf = fopen ("test.html", "w+");
		fwrite (txt, strlen (txt), 1, hf);
		fclose (hf);
	}
	g_free (txt);
}

static void
cb_toggle (GtkWidget *widget, gpointer data)
{
	gtk_source_view_set_show_line_pixmaps (GTK_SOURCE_VIEW (data),
					       !GTK_SOURCE_VIEW (data)->show_line_pixmaps);
}

static void
cb_line_numbers_toggle (GtkWidget *widget, gpointer data)
{
	gtk_source_view_set_show_line_numbers (GTK_SOURCE_VIEW (data),
					       !GTK_SOURCE_VIEW (data)->show_line_numbers);
}

static void
cb_move_cursor (GtkTextBuffer *b, GtkTextIter *cursoriter, GtkTextMark *mark, gpointer data)
{
	char buf[64];

	if (mark != gtk_text_buffer_get_insert (gtk_text_iter_get_buffer (cursoriter)))
		return;
	g_snprintf (buf, 64, "char pos %d line %d column %d",
		    gtk_text_iter_get_offset (cursoriter),
		    gtk_text_iter_get_line (cursoriter),
		    gtk_text_iter_get_line_offset (cursoriter));
	gtk_label_set_text (GTK_LABEL (data), buf);
}


int
main (int argc, char *argv[])
{
	GtkWidget *window;
	GtkWidget *scrolled;
	GtkWidget *vbox;
	GtkWidget *entry;
	GtkWidget *label;
	GtkWidget *button;
	GtkTextBuffer *buf;
	GtkWidget *tw;
	GdkPixbuf *pixbuf;
	GSList *dirs = NULL;

	/*
	int i;
	*/
	PangoFontDescription *font_desc = NULL;
	
	gtk_init (&argc, &argv);
	
	if (!gtk_source_languages_manager_init ())
			return -1;       
		
	if (!gtk_source_tags_style_manager_init())
		return -1;

	dirs = g_slist_prepend (dirs, ".");

	gtk_source_languages_manager_set_specs_dirs (dirs);		
	g_slist_free (dirs);

	window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_signal_connect (GTK_OBJECT (window), "destroy", GTK_SIGNAL_FUNC (gtk_main_quit), NULL);

	buf = test_source (NULL);

	vbox = gtk_vbox_new (0, FALSE);
	gtk_container_add (GTK_CONTAINER (window), vbox);

	entry = gtk_entry_new ();
	gtk_box_pack_start (GTK_BOX (vbox), entry, FALSE, FALSE, 2);

	button = gtk_button_new_with_label ("convert to html (example is saved as test.html)");
	gtk_box_pack_start (GTK_BOX (vbox), button, FALSE, FALSE, 0);
	gtk_signal_connect (GTK_OBJECT (button), "clicked", GTK_SIGNAL_FUNC (cb_convert), buf);

	scrolled = gtk_scrolled_window_new (NULL, NULL);
	gtk_box_pack_start (GTK_BOX (vbox), scrolled, TRUE, TRUE, 0);

	label = gtk_label_new ("label");
	gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, FALSE, 0);

	g_signal_connect_closure (G_OBJECT (entry), "activate",
				  g_cclosure_new ((GCallback) cb_entry_activate, buf, NULL), TRUE);
	g_signal_connect_closure (G_OBJECT (buf), "mark_set",
				  g_cclosure_new ((GCallback) cb_move_cursor, label, NULL), TRUE);
	tw = gtk_source_view_new_with_buffer (GTK_SOURCE_BUFFER (buf));
	g_object_unref (buf);
	
	gtk_source_view_set_show_line_numbers (GTK_SOURCE_VIEW (tw), TRUE);
	gtk_source_view_set_show_line_pixmaps (GTK_SOURCE_VIEW (tw), TRUE);

	font_desc = pango_font_description_from_string ("monospace 10");
	if (font_desc != NULL) {
		gtk_widget_modify_font (tw, font_desc);
		pango_font_description_free (font_desc);
	}

	gtk_source_view_set_tab_stop (GTK_SOURCE_VIEW (tw), 8);

	gtk_container_add (GTK_CONTAINER (scrolled), tw);

	button = gtk_button_new_with_label ("Toggle line pixmaps");
	gtk_box_pack_start (GTK_BOX (vbox), button, FALSE, FALSE, 0);
	gtk_signal_connect (GTK_OBJECT (button), "clicked", GTK_SIGNAL_FUNC (cb_toggle), tw);

	button = gtk_button_new_with_label ("Toggle line numbers");
	gtk_box_pack_start (GTK_BOX (vbox), button, FALSE, FALSE, 0);
	gtk_signal_connect (GTK_OBJECT (button), "clicked", GTK_SIGNAL_FUNC (cb_line_numbers_toggle), tw);

	pixbuf = gdk_pixbuf_new_from_file ("/usr/share/pixmaps/apple-green.png", NULL);
	gtk_source_view_add_pixbuf (GTK_SOURCE_VIEW (tw), "one", pixbuf, FALSE);
	g_object_unref (pixbuf);
	
	pixbuf = gdk_pixbuf_new_from_file ("/usr/share/pixmaps/no.xpm", NULL);
	gtk_source_view_add_pixbuf (GTK_SOURCE_VIEW (tw), "two", pixbuf, FALSE);
	g_object_unref (pixbuf);
	
	pixbuf = gdk_pixbuf_new_from_file ("/usr/share/pixmaps/yes.xpm", NULL);
	gtk_source_view_add_pixbuf (GTK_SOURCE_VIEW (tw), "three", pixbuf, FALSE);
	g_object_unref (pixbuf);
	
	/*
	for (i = 1; i < 200; i += 20) {
		gtk_source_buffer_line_set_marker (GTK_SOURCE_BUFFER (GTK_TEXT_VIEW (tw)->buffer),
						   i, "one");
	}
	for (i = 1; i < 200; i += 40) {
		gtk_source_buffer_line_add_marker (GTK_SOURCE_BUFFER (GTK_TEXT_VIEW (tw)->buffer),
						   i, "two");
	}
	for (i = 1; i < 200; i += 80) {
		gtk_source_buffer_line_add_marker (GTK_SOURCE_BUFFER (GTK_TEXT_VIEW (tw)->buffer),
						   i, "three");
	}
	*/

	
#if 0
	{
		const GSList *langs;
		GtkSourceLanguage *language;
		GSList *types = NULL;
		
		if (!gtk_source_languages_manager_init ())
			return -1;       
		
		if (!gtk_source_tags_style_manager_init())
			return -1;
		
		langs = gtk_source_languages_manager_get_available_languages ();

		while (langs != NULL)
		{
			const GSList *mime_types;
			const GSList *tags;
			GtkSourceLanguage *l = GTK_SOURCE_LANGUAGE (langs->data);

			g_print ("Name: %s\n", gtk_source_language_get_name (l));
			g_print ("Section: %s\n", gtk_source_language_get_section (l));

			mime_types = gtk_source_language_get_mime_types (l);
			g_print ("Mime types: ");
			while (mime_types != NULL)
			{
				g_print ("%s ", (const gchar*)mime_types->data);

				mime_types = g_slist_next (mime_types);
			}

			g_print ("\n\n");

			g_print ("Tags:\n");
			tags = gtk_source_language_get_tags (l);

			while (tags != NULL)
			{
				if (GTK_IS_SOURCE_TAG (tags->data))
				{
					g_print ("  Tag name: %s\n", GTK_TEXT_TAG (tags->data)->name);
				}
				else
					g_print ("  Error!!!\n");

				tags = g_slist_next (tags);
			}
							
			
			langs = g_slist_next (langs);
		}

		types = g_slist_prepend (types, "text/test");
		gtk_source_language_set_mime_types (
				GTK_SOURCE_LANGUAGE (
					gtk_source_languages_manager_get_available_languages ()->data),
				types);
		g_slist_free (types);

		g_print ("Get language from mime type: text/test\n\n");
		
		language = gtk_source_language_get_from_mime_type ("text/test");
		
		if (language != NULL)
		{
			g_print ("Name: %s\n", gtk_source_language_get_name (language));
			g_object_unref (language);
		}
		else
			g_print ("No language found.");	

		
		gtk_source_language_set_mime_types (
				GTK_SOURCE_LANGUAGE (
					gtk_source_languages_manager_get_available_languages ()->data),
				NULL);			
	}
#endif

	gtk_widget_set_usize (window, 400, 500);
	gtk_widget_show_all (window);
	gtk_main ();

	g_print ("Shutting down languages manager...\n");	

	gtk_source_languages_manager_shutdown ();
	gtk_source_tags_style_manager_shutdown ();

	g_print ("Done.\n");

	return (0);
}
