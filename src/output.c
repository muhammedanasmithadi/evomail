#include "evomail.h"

static void
json_append_indent (GString *out, guint depth)
{
	for (guint i = 0; i < depth; i++)
		g_string_append (out, "  ");
}

void
json_string (GString *out, const gchar *value)
{
	if (value == NULL) {
		g_string_append (out, "null");
		return;
	}

	g_string_append_c (out, '"');
	for (const guchar *p = (const guchar *) value; *p != '\0'; p++) {
		switch (*p) {
		case '"':
			g_string_append (out, "\\\"");
			break;
		case '\\':
			g_string_append (out, "\\\\");
			break;
		case '\b':
			g_string_append (out, "\\b");
			break;
		case '\f':
			g_string_append (out, "\\f");
			break;
		case '\n':
			g_string_append (out, "\\n");
			break;
		case '\r':
			g_string_append (out, "\\r");
			break;
		case '\t':
			g_string_append (out, "\\t");
			break;
		default:
			if (*p < 0x20)
				g_string_append_printf (out, "\\u%04x", *p);
			else
				g_string_append_c (out, *p);
			break;
		}
	}
	g_string_append_c (out, '"');
}

gchar *
format_date (gint64 date)
{
	GDateTime *dt;
	gchar *result;

	if (date <= 0)
		return g_strdup ("");

	dt = g_date_time_new_from_unix_local (date);
	if (dt == NULL)
		return g_strdup ("");

	result = g_date_time_format (dt, "%Y-%m-%d %H:%M");
	g_date_time_unref (dt);
	return result;
}

static gchar *
format_date_iso (gint64 date)
{
	GDateTime *dt;
	gchar *result;

	if (date <= 0)
		return g_strdup ("");

	dt = g_date_time_new_from_unix_utc (date);
	if (dt == NULL)
		return g_strdup ("");

	result = g_date_time_format (dt, "%Y-%m-%dT%H:%M:%SZ");
	g_date_time_unref (dt);
	return result;
}

gchar *
format_flags (guint32 flags)
{
	GString *out = g_string_new (NULL);

	if ((flags & CAMEL_MESSAGE_SEEN) == 0)
		g_string_append_c (out, 'U');
	if (flags & CAMEL_MESSAGE_FLAGGED)
		g_string_append_c (out, 'F');
	if (flags & CAMEL_MESSAGE_ANSWERED)
		g_string_append_c (out, 'A');
	if (flags & CAMEL_MESSAGE_DELETED)
		g_string_append_c (out, 'D');
	if (flags & CAMEL_MESSAGE_ATTACHMENTS)
		g_string_append_c (out, 'T');
	if (out->len == 0)
		g_string_append_c (out, '-');

	return g_string_free (out, FALSE);
}

static gchar *
truncate_utf8 (const gchar *text, glong max_chars)
{
	glong length;

	if (text == NULL || *text == '\0')
		return g_strdup ("");

	length = g_utf8_strlen (text, -1);
	if (length <= max_chars)
		return g_strdup (text);

	return g_strndup (text, g_utf8_offset_to_pointer (text, max_chars) - text);
}

void
print_message_table (GPtrArray *messages)
{
	g_print ("%-10s %-16s %-5s %-28s %s\n",
		 "UID", "DATE", "FLAGS", "FROM", "SUBJECT");

	for (guint i = 0; i < messages->len; i++) {
		MsgSummary *msg = g_ptr_array_index (messages, i);
		gchar *date = format_date (msg->date);
		gchar *flags = format_flags (msg->flags);
		gchar *from = truncate_utf8 (msg->from, 28);
		gchar *subject = truncate_utf8 (msg->subject, 80);

		g_print ("%-10s %-16s %-5s %-28s %s\n",
			 msg->uid, date, flags, from,
			 (subject != NULL && *subject != '\0') ? subject : "(no subject)");

		g_free (date);
		g_free (flags);
		g_free (from);
		g_free (subject);
	}
}

void
print_message_json (GPtrArray *messages)
{
	GString *out = g_string_new ("[\n");

	for (guint i = 0; i < messages->len; i++) {
		MsgSummary *msg = g_ptr_array_index (messages, i);
		gchar *iso = format_date_iso (msg->date);

		json_append_indent (out, 1);
		g_string_append (out, "{\"uid\":");
		json_string (out, msg->uid);
		g_string_append (out, ",\"from\":");
		json_string (out, msg->from);
		g_string_append (out, ",\"subject\":");
		json_string (out, msg->subject);
		g_string_append_printf (out, ",\"date\":%" G_GINT64_FORMAT,
					msg->date);
		g_string_append (out, ",\"date_iso\":");
		json_string (out, iso);
		g_string_append_printf (out, ",\"flags\":%u", msg->flags);
		g_string_append_printf (out, ",\"unread\":%s",
					(msg->flags & CAMEL_MESSAGE_SEEN) ? "false" : "true");
		g_string_append_printf (out, ",\"flagged\":%s",
					(msg->flags & CAMEL_MESSAGE_FLAGGED) ? "true" : "false");
		g_string_append_c (out, '}');
		if (i + 1 < messages->len)
			g_string_append_c (out, ',');
		g_string_append_c (out, '\n');

		g_free (iso);
	}

	g_string_append (out, "]\n");
	g_print ("%s", out->str);
	g_string_free (out, TRUE);
}

static gchar *
default_filename (const gchar *filename, guint number)
{
	if (filename != NULL && *filename != '\0')
		return g_strdup (filename);
	return g_strdup_printf ("attachment-%u", number);
}

void
print_read_message (ReadMessage *msg)
{
	g_print ("From: %s\n", msg->from != NULL ? msg->from : "");
	if (msg->to != NULL && *msg->to != '\0')
		g_print ("To: %s\n", msg->to);
	if (msg->cc != NULL && *msg->cc != '\0')
		g_print ("Cc: %s\n", msg->cc);
	g_print ("Date: %s\n", msg->date != NULL ? msg->date : "");
	g_print ("Subject: %s\n", msg->subject != NULL ? msg->subject : "");
	if (msg->message_id != NULL && *msg->message_id != '\0')
		g_print ("Message-ID: %s\n", msg->message_id);

	if (msg->attachments != NULL && msg->attachments->len > 0) {
		g_print ("Attachments:\n");
		for (guint i = 0; i < msg->attachments->len; i++) {
			Attachment *attachment = g_ptr_array_index (msg->attachments, i);
			g_print ("  - %s (%s, %" G_GSIZE_FORMAT " bytes)\n",
				 attachment->filename, attachment->content_type,
				 attachment->size);
		}
	}

	g_print ("\n");

	if (msg->text != NULL) {
		g_print ("%s", msg->text);
		if (!g_str_has_suffix (msg->text, "\n"))
			g_print ("\n");
	} else if (msg->html != NULL) {
		g_print ("(no text/plain part; HTML body follows)\n\n");
		g_print ("%s", msg->html);
		if (!g_str_has_suffix (msg->html, "\n"))
			g_print ("\n");
	} else {
		g_print ("(empty message body)\n");
	}
}

void
print_read_json (ReadMessage *msg)
{
	GString *out = g_string_new ("{\n");

	g_string_append (out, "  \"uid\":");
	json_string (out, msg->uid);
	g_string_append (out, ",\n  \"from\":");
	json_string (out, msg->from);
	g_string_append (out, ",\n  \"to\":");
	json_string (out, msg->to);
	g_string_append (out, ",\n  \"cc\":");
	json_string (out, msg->cc);
	g_string_append (out, ",\n  \"date\":");
	json_string (out, msg->date);
	g_string_append (out, ",\n  \"subject\":");
	json_string (out, msg->subject);
	g_string_append (out, ",\n  \"message_id\":");
	json_string (out, msg->message_id);
	g_string_append (out, ",\n  \"flags\":");
	g_string_append_printf (out, "%u", msg->flags);
	g_string_append (out, ",\n  \"unread\":");
	g_string_append (out, (msg->flags & CAMEL_MESSAGE_SEEN) ? "false" : "true");
	g_string_append (out, ",\n  \"flagged\":");
	g_string_append (out, (msg->flags & CAMEL_MESSAGE_FLAGGED) ? "true" : "false");
	g_string_append (out, ",\n  \"text\":");
	json_string (out, msg->text);
	g_string_append (out, ",\n  \"html\":");
	json_string (out, msg->html);
	g_string_append (out, ",\n  \"attachments\":[");

	if (msg->attachments != NULL) {
		for (guint i = 0; i < msg->attachments->len; i++) {
			Attachment *attachment = g_ptr_array_index (msg->attachments, i);
			gchar *filename = default_filename (attachment->filename,
							    i + 1);

			if (i > 0)
				g_string_append_c (out, ',');
			g_string_append (out, "\n    {\"filename\":");
			json_string (out, filename);
			g_string_append (out, ",\"content_type\":");
			json_string (out, attachment->content_type);
			g_string_append_printf (out, ",\"size\":%" G_GSIZE_FORMAT,
						attachment->size);
			g_string_append_c (out, '}');
			g_free (filename);
		}
		if (msg->attachments->len > 0)
			g_string_append (out, "\n  ");
	}

	g_string_append (out, "]\n}\n");
	g_print ("%s", out->str);
	g_string_free (out, TRUE);
}
