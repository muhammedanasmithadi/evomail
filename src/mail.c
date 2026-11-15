#include "evomail.h"
#include <string.h>

static gchar *
folder_info_find_name (CamelFolderInfo *info, const gchar *name)
{
	for (CamelFolderInfo *i = info; i != NULL; i = i->next) {
		if (g_ascii_strcasecmp (i->full_name, name) == 0 ||
		    (i->display_name != NULL &&
		     g_ascii_strcasecmp (i->display_name, name) == 0))
			return g_strdup (i->full_name);

		if (i->child != NULL) {
			gchar *found = folder_info_find_name (i->child, name);
			if (found != NULL)
				return found;
		}
	}
	return NULL;
}

static gchar *
folder_info_find_type (CamelFolderInfo *info, guint32 type)
{
	for (CamelFolderInfo *i = info; i != NULL; i = i->next) {
		if ((i->flags & CAMEL_FOLDER_TYPE_MASK) == type)
			return g_strdup (i->full_name);

		if (i->child != NULL) {
			gchar *found = folder_info_find_type (i->child, type);
			if (found != NULL)
				return found;
		}
	}
	return NULL;
}

static gchar *
folder_info_find_name_or_type (CamelFolderInfo *info, const gchar *alias,
                               guint32 type)
{
	for (CamelFolderInfo *i = info; i != NULL; i = i->next) {
		/* Try name-based match: alias is a prefix of the last path
		 * component, e.g. "sent" matches "[Gmail]/Sent Mail". */
		if (i->full_name != NULL) {
			const gchar *suffix = strrchr (i->full_name, '/');
			if (suffix != NULL)
				suffix++;
			else
				suffix = i->full_name;
			if (g_ascii_strncasecmp (suffix, alias, strlen (alias)) == 0 &&
			    (suffix[strlen (alias)] == '\0' ||
			     suffix[strlen (alias)] == ' '))
				return g_strdup (i->full_name);
			if (g_ascii_strcasecmp (i->full_name, alias) == 0)
				return g_strdup (i->full_name);
		}

		/* Then try type-based match. */
		if ((i->flags & CAMEL_FOLDER_TYPE_MASK) == type)
			return g_strdup (i->full_name);

		if (i->child != NULL) {
			gchar *found = folder_info_find_name_or_type (
				i->child, alias, type);
			if (found != NULL)
				return found;
		}
	}
	return NULL;
}

gchar *
evo_find_special_folder (CamelStore *store, guint32 type, GError **error)
{
	CamelFolderInfo *info;
	gchar *name;

	info = camel_store_get_folder_info_sync (
		store, NULL, CAMEL_STORE_FOLDER_INFO_RECURSIVE, NULL, error);
	if (info == NULL)
		return NULL;

	name = folder_info_find_type (info, type);
	camel_folder_info_free (info);
	return name;
}

CamelFolder *
evo_open_folder (CamelStore *store, const gchar *name,
		 gboolean writable, GError **error)
{
	CamelFolder *folder;
	GError *local_error = NULL;

	(void) writable;

	if (name == NULL || *name == '\0' ||
	    g_ascii_strcasecmp (name, "inbox") == 0) {
		folder = camel_store_get_inbox_folder_sync (store, NULL, error);
		if (folder != NULL)
			return folder;
		if (name != NULL && *name != '\0')
			return NULL;
	}

	if (name != NULL && *name != '\0') {
		static const struct {
			const gchar *alias;
			guint32 type;
		} aliases[] = {
			{ "sent", CAMEL_FOLDER_TYPE_SENT },
			{ "drafts", CAMEL_FOLDER_TYPE_DRAFTS },
			{ "trash", CAMEL_FOLDER_TYPE_TRASH },
			{ "junk", CAMEL_FOLDER_TYPE_JUNK },
			{ "spam", CAMEL_FOLDER_TYPE_JUNK },
			{ "all", CAMEL_FOLDER_TYPE_ALL },
			{ "archive", CAMEL_FOLDER_TYPE_ARCHIVE },
		};

		for (guint i = 0; i < G_N_ELEMENTS (aliases); i++) {
			if (g_ascii_strcasecmp (name, aliases[i].alias) == 0) {
				/* First try type-based match, then name-based fallback. */
				gchar *special = evo_find_special_folder (
					store, aliases[i].type, &local_error);
				if (special == NULL) {
					CamelFolderInfo *info =
						camel_store_get_folder_info_sync (
							store, NULL,
							CAMEL_STORE_FOLDER_INFO_RECURSIVE,
							NULL, NULL);
					if (info != NULL) {
						special = folder_info_find_name_or_type (
							info, aliases[i].alias,
							aliases[i].type);
						camel_folder_info_free (info);
					}
				}
				if (special != NULL) {
					folder = camel_store_get_folder_sync (
						store, special, 0, NULL, error);
					g_free (special);
					g_clear_error (&local_error);
					return folder;
				}
				g_clear_error (&local_error);
				break;
			}
		}
	}

	folder = camel_store_get_folder_sync (store, name, 0, NULL, NULL);
	if (folder != NULL)
		return folder;

	{
		CamelFolderInfo *info;
		gchar *found;

		info = camel_store_get_folder_info_sync (
			store, NULL, CAMEL_STORE_FOLDER_INFO_RECURSIVE,
			NULL, NULL);
		if (info == NULL) {
			g_set_error (error, CAMEL_STORE_ERROR,
				     CAMEL_STORE_ERROR_NO_FOLDER,
				     "Folder “%s” not found.", name);
			return NULL;
		}

		found = folder_info_find_name (info, name);
		camel_folder_info_free (info);
		if (found == NULL) {
			g_set_error (error, CAMEL_STORE_ERROR,
				     CAMEL_STORE_ERROR_NO_FOLDER,
				     "Folder “%s” not found.", name);
			return NULL;
		}

		folder = camel_store_get_folder_sync (store, found, 0, NULL, error);
		g_free (found);
		return folder;
	}
}

static gint
compare_messages (gconstpointer a, gconstpointer b)
{
	const MsgSummary *ma = *(MsgSummary * const *) a;
	const MsgSummary *mb = *(MsgSummary * const *) b;

	if (ma->date != mb->date)
		return ma->date < mb->date ? 1 : -1;
	return g_strcmp0 (mb->uid, ma->uid);
}

static GPtrArray *
summaries_from_uids (CamelFolder *folder, GPtrArray *uids)
{
	GPtrArray *messages = g_ptr_array_new_with_free_func (
		(GDestroyNotify) msg_summary_free);

	for (guint i = 0; i < uids->len; i++) {
		const gchar *uid = g_ptr_array_index (uids, i);
		CamelMessageInfo *info = camel_folder_get_message_info (folder, uid);
		MsgSummary *summary;

		if (info == NULL)
			continue;

		summary = g_new0 (MsgSummary, 1);
		summary->uid = g_strdup (uid);
		summary->from = g_strdup (camel_message_info_get_from (info));
		summary->subject = g_strdup (camel_message_info_get_subject (info));
		summary->date = camel_message_info_get_date_sent (info);
		summary->flags = camel_message_info_get_flags (info);
		g_ptr_array_add (messages, summary);
	}

	g_ptr_array_sort (messages, compare_messages);
	return messages;
}

GPtrArray *
evo_list_messages (CamelFolder *folder, gboolean refresh, GError **error)
{
	GPtrArray *uids = NULL;
	GPtrArray *messages;

	if (refresh && !camel_folder_refresh_info_sync (folder, NULL, error))
		return NULL;

	if (!camel_folder_search_sync (folder, "#t", &uids, NULL, error))
		return NULL;

	messages = summaries_from_uids (folder, uids);
	g_ptr_array_unref (uids);
	return messages;
}

static void
append_expr_string (GString *out, const gchar *value)
{
	g_string_append_c (out, '"');
	for (const gchar *p = value; *p != '\0'; p++) {
		if (*p == '"' || *p == '\\')
			g_string_append_c (out, '\\');
		g_string_append_c (out, *p);
	}
	g_string_append_c (out, '"');
}

static gchar *
free_text_clause (const gchar *term)
{
	GString *out = g_string_new ("(or ");
	g_string_append (out, "(header-contains \"subject\" ");
	append_expr_string (out, term);
	g_string_append (out, ") (header-contains \"from\" ");
	append_expr_string (out, term);
	g_string_append (out, ") (body-contains ");
	append_expr_string (out, term);
	g_string_append (out, "))");
	return g_string_free (out, FALSE);
}

static gchar *
build_search_expression (const gchar *query)
{
	GPtrArray *terms;
	GPtrArray *clauses;
	GString *current;
	gboolean quoted = FALSE;
	GString *result;

	terms = g_ptr_array_new_with_free_func (g_free);
	current = g_string_new (NULL);

	for (const gchar *p = query; ; p++) {
		gchar c = *p;
		gboolean at_end = (c == '\0');
		gboolean split = at_end || (!quoted && g_ascii_isspace (c));

		if (!at_end && c == '"') {
			quoted = !quoted;
			continue;
		}

		if (split) {
			if (current->len > 0) {
				g_ptr_array_add (terms, g_strdup (current->str));
				g_string_truncate (current, 0);
			}
			if (at_end)
				break;
			continue;
		}

		g_string_append_c (current, c);
	}
	g_string_free (current, TRUE);

	clauses = g_ptr_array_new_with_free_func (g_free);
	for (guint i = 0; i < terms->len; i++) {
		const gchar *term = g_ptr_array_index (terms, i);
		const gchar *colon = strchr (term, ':');
		gchar *clause = NULL;

		if (colon != NULL && colon != term && colon[1] != '\0') {
			gchar *key = g_ascii_strdown (term, colon - term);
			const gchar *value = colon + 1;

			if (g_strcmp0 (key, "from") == 0 ||
			    g_strcmp0 (key, "to") == 0 ||
			    g_strcmp0 (key, "subject") == 0) {
				GString *tmp = g_string_new ("(header-contains \"");
				g_string_append (tmp, key);
				g_string_append (tmp, "\" ");
				append_expr_string (tmp, value);
				g_string_append_c (tmp, ')');
				clause = g_string_free (tmp, FALSE);
			} else if (g_strcmp0 (key, "body") == 0) {
				GString *tmp = g_string_new ("(body-contains ");
				append_expr_string (tmp, value);
				g_string_append_c (tmp, ')');
				clause = g_string_free (tmp, FALSE);
			} else if (g_strcmp0 (key, "is") == 0) {
				if (g_ascii_strcasecmp (value, "unread") == 0)
					clause = g_strdup ("(not (system-flag \"seen\"))");
				else if (g_ascii_strcasecmp (value, "read") == 0)
					clause = g_strdup ("(system-flag \"seen\")");
				else if (g_ascii_strcasecmp (value, "flagged") == 0 ||
					 g_ascii_strcasecmp (value, "starred") == 0)
					clause = g_strdup ("(system-flag \"flagged\")");
				else if (g_ascii_strcasecmp (value, "unflagged") == 0)
					clause = g_strdup ("(not (system-flag \"flagged\"))");
			}
			g_free (key);
		}

		if (clause == NULL)
			clause = free_text_clause (term);
		g_ptr_array_add (clauses, clause);
	}

	if (clauses->len == 0) {
		result = g_string_new ("#t");
	} else if (clauses->len == 1) {
		result = g_string_new (g_ptr_array_index (clauses, 0));
	} else {
		result = g_string_new ("(and ");
		for (guint i = 0; i < clauses->len; i++) {
			if (i > 0)
				g_string_append_c (result, ' ');
			g_string_append (result, g_ptr_array_index (clauses, i));
		}
		g_string_append_c (result, ')');
	}

	g_ptr_array_unref (clauses);
	g_ptr_array_unref (terms);
	return g_string_free (result, FALSE);
}

GPtrArray *
evo_search_messages (CamelFolder *folder, const gchar *query,
		     gboolean refresh, GError **error)
{
	GPtrArray *uids = NULL;
	GPtrArray *messages;
	gchar *expression;

	if (refresh && !camel_folder_refresh_info_sync (folder, NULL, error))
		return NULL;

	expression = build_search_expression (query);
	if (!camel_folder_search_sync (folder, expression, &uids, NULL, error)) {
		g_free (expression);
		return NULL;
	}
	g_free (expression);

	messages = summaries_from_uids (folder, uids);
	g_ptr_array_unref (uids);
	return messages;
}

static GBytes *
decode_content (CamelDataWrapper *content, GError **error)
{
	GOutputStream *stream;
	gssize written;
	gsize size;
	gpointer data;

	stream = g_memory_output_stream_new_resizable ();
	written = camel_data_wrapper_decode_to_output_stream_sync (
		content, stream, NULL, error);
	if (written < 0) {
		g_object_unref (stream);
		return NULL;
	}

	g_output_stream_close (stream, NULL, NULL);
	size = g_memory_output_stream_get_data_size (G_MEMORY_OUTPUT_STREAM (stream));
	data = g_memdup2 (g_memory_output_stream_get_data (
		G_MEMORY_OUTPUT_STREAM (stream)), size);
	g_object_unref (stream);

	return g_bytes_new_take (data, size);
}

static gchar *
bytes_to_utf8 (GBytes *bytes, const gchar *charset)
{
	gsize len;
	const gchar *data = g_bytes_get_data (bytes, &len);

	if (charset != NULL && *charset != '\0' &&
	    g_ascii_strcasecmp (charset, "utf-8") != 0 &&
	    g_ascii_strcasecmp (charset, "us-ascii") != 0) {
		gsize written = 0;
		gchar *converted = g_convert (data, len, "UTF-8", charset,
					      NULL, &written, NULL);
		if (converted != NULL)
			return converted;
	}

	return g_strndup (data, len);
}

static gchar *
safe_attachment_path (const gchar *dir, const gchar *filename, guint number)
{
	gchar *base;
	gchar *path;
	guint suffix = 0;

	base = g_path_get_basename (filename);
	if (base == NULL || *base == '\0' || g_strcmp0 (base, ".") == 0 ||
	    g_strcmp0 (base, "/") == 0) {
		g_free (base);
		base = g_strdup_printf ("attachment-%u", number);
	}

	path = g_build_filename (dir, base, NULL);
	while (g_file_test (path, G_FILE_TEST_EXISTS)) {
		gchar *stem;
		gchar *ext;
		gchar *candidate;

		suffix++;
		stem = g_strdup (base);
		ext = g_strrstr (stem, ".");
		if (ext != NULL && ext != stem)
			*ext++ = '\0';
		else
			ext = NULL;
		if (ext != NULL)
			candidate = g_strdup_printf ("%s-%u.%s", stem, suffix, ext);
		else
			candidate = g_strdup_printf ("%s-%u", stem, suffix);
		g_free (stem);
		g_free (path);
		path = g_build_filename (dir, candidate, NULL);
		g_free (candidate);
	}

	g_free (base);
	return path;
}

static void
collect_part (CamelMimePart *part, MessageBody *body, const gchar *save_dir,
	      guint *attachment_number, GError **error)
{
	CamelContentType *ctype;
	gchar *content_type;
	const gchar *charset = NULL;
	const gchar *filename;
	const gchar *disposition;
	CamelDataWrapper *content;

	ctype = camel_mime_part_get_content_type (part);
	content_type = ctype != NULL ? camel_content_type_simple (ctype) : NULL;
	if (ctype != NULL)
		charset = camel_content_type_param (ctype, "charset");

	filename = camel_mime_part_get_filename (part);
	disposition = camel_mime_part_get_disposition (part);
	content = camel_medium_get_content (CAMEL_MEDIUM (part));

	if (content != NULL && CAMEL_IS_MULTIPART (content)) {
		CamelMultipart *multipart = CAMEL_MULTIPART (content);
		guint count = camel_multipart_get_number (multipart);

		for (guint i = 0; i < count; i++)
			collect_part (camel_multipart_get_part (multipart, i),
				      body, save_dir, attachment_number, error);
	} else if (content != NULL) {
		gboolean is_text_plain = content_type != NULL &&
			g_str_has_prefix (content_type, "text/plain");
		gboolean is_text_html = content_type != NULL &&
			g_str_has_prefix (content_type, "text/html");
		gboolean is_attachment = filename != NULL ||
			(disposition != NULL &&
			 g_ascii_strcasecmp (disposition, "attachment") == 0);

		if (is_text_plain && body->text == NULL) {
			GBytes *bytes = decode_content (content, error);
			if (bytes != NULL) {
				body->text = bytes_to_utf8 (bytes, charset);
				g_free (body->charset);
				body->charset = g_strdup (charset);
				g_bytes_unref (bytes);
			}
		} else if (is_text_html && body->html == NULL) {
			GBytes *bytes = decode_content (content, error);
			if (bytes != NULL) {
				body->html = bytes_to_utf8 (bytes, charset);
				g_bytes_unref (bytes);
			}
		} else if (is_attachment) {
			Attachment *attachment = g_new0 (Attachment, 1);
			gchar *fallback_name = NULL;

			attachment->content_type = g_strdup (
				content_type != NULL ? content_type :
				"application/octet-stream");

			if (filename != NULL) {
				attachment->filename = g_strdup (filename);
			} else {
				fallback_name = g_strdup_printf ("attachment-%u",
								 *attachment_number);
				attachment->filename = g_strdup (fallback_name);
			}

			if (save_dir != NULL && *save_dir != '\0') {
				GBytes *bytes = decode_content (content, error);
				if (bytes != NULL) {
					gchar *path = safe_attachment_path (
						save_dir, attachment->filename,
						*attachment_number);
					gsize size = 0;

					g_bytes_get_data (bytes, &size);
					attachment->size = size;
					if (!g_file_set_contents (path,
						g_bytes_get_data (bytes, NULL),
						size, error)) {
						g_free (path);
						g_free (fallback_name);
						attachment_free (attachment);
						g_bytes_unref (bytes);
						g_free (content_type);
						return;
					}
					g_free (path);
					g_bytes_unref (bytes);
				}
			} else {
				attachment->size =
					camel_data_wrapper_calculate_decoded_size_sync (
						content, NULL, NULL);
			}

			g_ptr_array_add (body->attachments, attachment);
			(*attachment_number)++;
			g_free (fallback_name);
		}
	}

	g_free (content_type);
}

static MessageBody *
message_body_new (void)
{
	MessageBody *body = g_new0 (MessageBody, 1);
	body->attachments = g_ptr_array_new_with_free_func (
		(GDestroyNotify) attachment_free);
	return body;
}

void
message_body_free (MessageBody *body)
{
	if (body == NULL)
		return;
	g_free (body->text);
	g_free (body->html);
	g_free (body->charset);
	if (body->attachments != NULL)
		g_ptr_array_unref (body->attachments);
	g_free (body);
}

void
attachment_free (Attachment *attachment)
{
	if (attachment == NULL)
		return;
	g_free (attachment->filename);
	g_free (attachment->content_type);
	g_free (attachment);
}

static gchar *
format_address (CamelInternetAddress *address)
{
	if (address == NULL)
		return NULL;
	return camel_address_format (CAMEL_ADDRESS (address));
}

ReadMessage *
evo_read_message (CamelFolder *folder, const gchar *uid,
		  const gchar *save_attachments, GError **error)
{
	CamelMimeMessage *message;
	ReadMessage *result;
	MessageBody *body;
	CamelInternetAddress *from;
	guint attachment_number = 1;

	message = camel_folder_get_message_sync (folder, uid, NULL, error);
	if (message == NULL)
		return NULL;

	body = message_body_new ();
	collect_part (CAMEL_MIME_PART (message), body,
		      save_attachments, &attachment_number, error);
	if (error != NULL && *error != NULL) {
		message_body_free (body);
		g_object_unref (message);
		return NULL;
	}

	result = g_new0 (ReadMessage, 1);
	result->uid = g_strdup (uid);
	result->subject = g_strdup (camel_mime_message_get_subject (message));
	from = camel_mime_message_get_from (message);
	result->from = format_address (from);
	result->to = format_address (camel_mime_message_get_recipients (
		message, CAMEL_RECIPIENT_TYPE_TO));
	result->cc = format_address (camel_mime_message_get_recipients (
		message, CAMEL_RECIPIENT_TYPE_CC));
	result->date = format_date (camel_mime_message_get_date (message, NULL));
	result->message_id = g_strdup (camel_mime_message_get_message_id (message));

	/* Get flags from message info if available. */
	{
		CamelMessageInfo *mi = camel_folder_get_message_info (folder, uid);
		if (mi != NULL) {
			result->flags = camel_message_info_get_flags (mi);
			g_object_unref (mi);
		}
	}

	result->text = body->text;
	result->html = body->html;
	result->attachments = body->attachments;

	body->text = NULL;
	body->html = NULL;
	body->attachments = NULL;
	message_body_free (body);
	g_object_unref (message);

	return result;
}

void
read_message_free (ReadMessage *msg)
{
	if (msg == NULL)
		return;
	g_free (msg->uid);
	g_free (msg->subject);
	g_free (msg->from);
	g_free (msg->to);
	g_free (msg->cc);
	g_free (msg->date);
	g_free (msg->message_id);
	g_free (msg->text);
	g_free (msg->html);
	if (msg->attachments != NULL)
		g_ptr_array_unref (msg->attachments);
	g_free (msg);
}

void
msg_summary_free (MsgSummary *msg)
{
	if (msg == NULL)
		return;
	g_free (msg->uid);
	g_free (msg->from);
	g_free (msg->subject);
	g_free (msg);
}

gboolean
evo_mark_messages (CamelFolder *folder, gchar **uids,
		   guint n_uids, guint32 mask, guint32 set,
		   GError **error)
{
	gboolean any_changed = FALSE;

	for (guint i = 0; i < n_uids; i++) {
		const gchar *uid = uids[i];
		CamelMessageInfo *info = camel_folder_get_message_info (folder, uid);
		guint32 before = info ? camel_message_info_get_flags (info) : 0;

		if (info != NULL &&
		    (before & mask) == (set & mask)) {
			/* Already in the desired state; nothing to do. */
			g_clear_object (&info);
			continue;
		}
		g_clear_object (&info);

		if (!camel_folder_set_message_flags (folder, uid, mask, set)) {
			g_set_error (error, CAMEL_FOLDER_ERROR,
				     CAMEL_FOLDER_ERROR_INVALID,
				     "Could not update flags for message %s.",
				     uid);
			return FALSE;
		}
		any_changed = TRUE;
	}

	if (any_changed) {
		CamelStore *store = camel_folder_get_parent_store (folder);
		gboolean sync_result;

		if (CAMEL_IS_OFFLINE_STORE (store)) {
			sync_result = camel_store_synchronize_sync (
				store, FALSE, NULL, error);
		} else {
			sync_result = camel_folder_synchronize_sync (
				folder, FALSE, NULL, error);
		}
		g_object_unref (store);

		if (!sync_result)
			return FALSE;
	}

	return TRUE;
}
