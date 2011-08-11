/*
** Copyright (C) 2011  Dirk-Jan C. Binnema <djcb@djcbsoftware.nl>
**
** This program is free software; you can redistribute it and/or modify it
** under the terms of the GNU General Public License as published by the
** Free Software Foundation; either version 3, or (at your option) any
** later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software Foundation,
** Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
**
*/
#include <string.h>

#include "mu-str.h"
#include "mu-msg.h"
#include "mu-msg-part.h"
#include "mu-maildir.h"

static void
append_sexp_attr_list (GString *gstr, const char* elm, const GSList *lst)
{
	const GSList *cur;
	
	if (!lst)
		return; /* empty list, don't include */
	
	g_string_append_printf (gstr, "\t:%s ( ", elm);

	for (cur = lst; cur; cur = g_slist_next(cur)) {
		char *str;
		str = mu_str_escape_c_literal
			((const gchar*)cur->data, TRUE);
		g_string_append_printf (gstr, "%s ", str);
		g_free (str);
	}

	g_string_append (gstr, ")\n");	
}


static void
append_sexp_attr (GString *gstr, const char* elm, const char *str)
{
	gchar *esc;
	
	if (!str || strlen(str) == 0)
		return; /* empty: don't include */

	esc = mu_str_escape_c_literal (str, TRUE);
	
	g_string_append_printf (gstr, "\t:%s %s\n", elm, esc);
	g_free (esc);
}


struct _ContactData {
	gboolean from, to, cc, bcc;
	GString *gstr;
	MuMsgContactType prev_ctype;
}; 
typedef struct _ContactData ContactData;

static gchar*
get_name_addr_pair (MuMsgContact *c)
{
	gchar *name, *addr, *pair;

	name = (char*)mu_msg_contact_name(c);
	addr = (char*)mu_msg_contact_address(c);

	name = name ? mu_str_escape_c_literal (name, TRUE) : NULL;
	addr = addr ? mu_str_escape_c_literal (addr, TRUE) : NULL;

	pair = g_strdup_printf ("(%s . %s)",
				name ? name : "nil",
				addr ? addr : "nil");
	g_free (name);
	g_free (addr);

	return pair;
}



static gboolean
each_contact (MuMsgContact *c, ContactData *cdata)
{
	char *pair;
	MuMsgContactType ctype;

	ctype = mu_msg_contact_type (c);	

	if (cdata->prev_ctype != ctype && cdata->prev_ctype != (unsigned)-1)
		g_string_append (cdata->gstr, ")\n");

	switch (ctype) {
	
	case MU_MSG_CONTACT_TYPE_FROM:
		if (!cdata->from)
			g_string_append (cdata->gstr, "\t:from (");
		cdata->from = TRUE;
		break;
		
	case MU_MSG_CONTACT_TYPE_TO:
		if (!cdata->to)
			g_string_append_printf 	(cdata->gstr,"\t:to (");
		cdata->to = TRUE;
		break;

	case MU_MSG_CONTACT_TYPE_CC:
		if (!cdata->cc)
			g_string_append_printf (cdata->gstr,"\t:cc (");
		cdata->cc = TRUE;
		break;

	case MU_MSG_CONTACT_TYPE_BCC:
		if (!cdata->bcc)
			g_string_append_printf (cdata->gstr, "\t:bcc (");
		cdata->bcc = TRUE;
		break;
		
	default: g_return_val_if_reached (FALSE);
	}

	
	cdata->prev_ctype = ctype;
	
	pair = get_name_addr_pair (c);
	g_string_append (cdata->gstr, pair);
	g_free (pair);
		
	return TRUE;
}


static void
append_sexp_contacts (GString *gstr, MuMsg *msg)
{
	ContactData cdata = { FALSE, FALSE, FALSE, FALSE, gstr,
			      (unsigned)-1};
	
	mu_msg_contact_foreach (msg, (MuMsgContactForeachFunc)each_contact,
				&cdata);
	
	if (cdata.from || cdata.to || cdata.cc || cdata.bcc)
		gstr = g_string_append (gstr, ")\n");
}

struct _FlagData {
	char *flagstr;
	MuFlags msgflags;
};
typedef struct _FlagData FlagData;

static void
each_flag (MuFlags flag, FlagData *fdata)
{
	if (!(flag & fdata->msgflags))
		return;

	if (!fdata->flagstr)
		fdata->flagstr = g_strdup (mu_flag_name(flag));
	else {
		gchar *tmp;
		tmp = g_strconcat (fdata->flagstr, " ",
				   mu_flag_name(flag), NULL);
		g_free (fdata->flagstr);
		fdata->flagstr = tmp;
	}
}

static void
append_sexp_flags (GString *gstr, MuMsg *msg)
{
	FlagData fdata;
	
	fdata.msgflags = mu_msg_get_flags (msg);
	fdata.flagstr  = NULL;
	
	mu_flags_foreach ((MuFlagsForeachFunc)each_flag, &fdata);
	if (fdata.flagstr) 
		g_string_append_printf (gstr, "\t:flags (%s)\n",
					fdata.flagstr);
	g_free (fdata.flagstr);
}

static void
each_part (MuMsg *msg, MuMsgPart *part, gchar **parts)
{
	const char *fname;
	
	fname = mu_msg_part_file_name (part);
	if (fname) {
		char *esc;
		esc   = mu_str_escape_c_literal (fname, TRUE);
		*parts = g_strdup_printf
			("%s(%d %s \"%s/%s\")",
			 *parts ? *parts : "",
			 part->index,
			 esc,
			 part->type ? part->type : "application",
			 part->subtype ? part->subtype : "octet-stream");
	}
}


static void
append_sexp_attachments (GString *gstr, MuMsg *msg)
{
	char *parts;

	parts = NULL;
	mu_msg_part_foreach (msg, (MuMsgPartForeachFunc)each_part, &parts);

	if (parts)
		g_string_append_printf (gstr, "\t:attachments (%s)\n", parts);

	g_free (parts);
}


static void
append_sexp_message_file_attr (GString *gstr, MuMsg *msg)
{
	append_sexp_attachments (gstr, msg);
	
	append_sexp_attr (gstr, "reply-to",
				  mu_msg_get_header (msg, "Reply-To"));
	append_sexp_attr_list (gstr, "references", mu_msg_get_references (msg));
	append_sexp_attr (gstr, "in-reply-to",
			  mu_msg_get_header (msg, "In-Reply-To"));
	
	append_sexp_attr (gstr, "body-txt",
				  mu_msg_get_body_text(msg));
	append_sexp_attr (gstr, "body-html",
			  mu_msg_get_body_html(msg));
}

char*
mu_msg_to_sexp (MuMsg *msg, gboolean dbonly)
{
	GString *gstr;
	time_t t;
	
	gstr = g_string_sized_new (dbonly ? 1024 : 8192);	
	g_string_append (gstr, "(\n");

	append_sexp_contacts (gstr, msg);	
	append_sexp_attr (gstr,
			  "subject", mu_msg_get_subject (msg));
	
	t = mu_msg_get_date (msg);
	/* weird time format for emacs 29-bit ints...*/
	g_string_append_printf (gstr, 
				"\t:date (%u %u 0)\n", (unsigned)(t >> 16),
				(unsigned)(t & 0xffff));
	g_string_append_printf (gstr, "\t:size %u\n",
				(unsigned) mu_msg_get_size (msg));
	append_sexp_attr (gstr, "message-id", mu_msg_get_msgid (msg));
	append_sexp_attr (gstr, "path",	 mu_msg_get_path (msg));
	append_sexp_attr (gstr, "maildir", mu_msg_get_maildir (msg));
	
	g_string_append_printf (gstr, "\t:priority %s\n",
				mu_msg_prio_name(mu_msg_get_prio(msg)));
	
	append_sexp_flags (gstr, msg);
	
	/* file attr things can only be gotten from the file (ie., mu
	 * view), not from the database (mu find) */
	if (!dbonly)
		append_sexp_message_file_attr (gstr, msg);
	
	g_string_append (gstr, ")\n;;eom\n");	

	return g_string_free (gstr, FALSE);
}
 
