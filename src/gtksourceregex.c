/*  gtksourceregex.c
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

#include <string.h>

#include <glib.h>

#include "gtksourceregex.h"


gboolean
gtk_source_regex_compile (GtkSourceRegex *regex, const gchar *pattern)
{
	g_return_val_if_fail (pattern != NULL, FALSE);
	g_return_val_if_fail (regex != NULL, FALSE);

	memset (&regex->buf, 0, sizeof (regex->buf));
	
	regex->len = strlen (pattern);
	regex->buf.translate = NULL;
	regex->buf.fastmap = g_malloc (256);
	regex->buf.allocated = 0;
	regex->buf.buffer = NULL;
	regex->buf.can_be_null = 0;	/* so we wont allow that in patterns! */
	regex->buf.no_sub = 0;
	
	if (re_compile_pattern (pattern, strlen (pattern), &regex->buf) == 0) {
		/* success...now try to compile a fastmap */
		if (re_compile_fastmap (&regex->buf) != 0) {
			g_warning ("Regex failed to create a fastmap.");
			/* error...no fastmap */
			g_free (regex->buf.fastmap);
			regex->buf.fastmap = NULL;
		}

		return TRUE;
	} else {
		g_warning ("Regex failed to compile.");
		return FALSE;
	}
}

void
gtk_source_regex_destroy (GtkSourceRegex *regex)
{
	g_return_if_fail (regex != NULL);

	g_free (regex->buf.fastmap);
	regex->buf.fastmap = NULL;
	regfree (&regex->buf);
}

/*
 * pos: offset
 *
 * Returns: start pos as offset
 */
gint
gtk_source_regex_search (GtkSourceRegex       *regex,
			 const gchar          *text,
			 gint                  pos,
			 GtkSourceBufferMatch *match)
{
	gint len;
	gint res;

	g_return_val_if_fail (text != NULL, -2);
	g_return_val_if_fail (pos >= 0, -2);
	g_return_val_if_fail (regex != NULL, -2);

	/* Work around a re_search bug where it returns the number of bytes
	 * instead of the number of characters (unicode characters can be
	 * more than 1 byte) it matched. See redhat bugzilla #73644. */
	len = strlen (text);
	pos = g_utf8_offset_to_pointer (text, pos) - text;

	res = re_search (&regex->buf, text, len, pos, len - pos, &regex->reg);

	if (res < 0)
		return res;

	if (match != NULL) {
		match->startpos =
		    g_utf8_pointer_to_offset (text, text + res);
		match->endpos =
		    g_utf8_pointer_to_offset (text,
					      text + regex->reg.end[0]);

		return match->startpos;
	} else
		return g_utf8_pointer_to_offset (text, text + res);
}


/* regex_match -- tries to match regex at the 'pos' position in the text. It 
 * returns the number of chars matched, or -1 if no match. 
 * Warning: The number matched can be 0, if the regex matches the empty string.
 */

/*
 * pos: offset
 * 
 * Returns: number of bytes matched
 */
gint
gtk_source_regex_match (GtkSourceRegex *regex,
			const gchar    *text, 
			gint            len, 
			gint            pos)
{
	g_return_val_if_fail (regex != NULL, -1);
	g_return_val_if_fail (pos >= 0, -1);
	g_return_val_if_fail ((len == -1)
			      || ((len >= 0)
				  && (len <= strlen (text))), -1);

	if (len == -1)
		len = strlen (text);
	
	pos = g_utf8_offset_to_pointer (text, pos) - text;

	return re_match (&regex->buf, text, strlen (text), pos,
			 &regex->reg);
}

