#include "evomail.h"
#include "evoapp.h"

#include <camel/camel.h>
#include <libedataserver/libedataserver.h>
#include <string.h>

extern gchar *format_date (gint64 date);

/* ========================================================================
 * Internal types
 * ======================================================================== */

struct _EvoFolder {
	CamelFolder *folder;
};

/* EvoMessage and EvoAttachment are defined in evomail.h (public API).
 * EvoAccount is defined below (internal).
 */

/* ========================================================================
 * Folder info tree traversal
 * ======================================================================== */

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
evo_find_special_folder (EvoStore *store, guint32 type, GError **error)
{
	CamelFolderInfo *info;
	gchar *name;

	info = camel_store_get_folder_info_sync (
		evo_store_get_camel_store (store),
		NULL, CAMEL_STORE_FOLDER_INFO_RECURSIVE, NULL, error);
	if (info == NULL)
		return NULL;

	name = folder_info_find_type (info, type);
	camel_folder_info_free (info);
	return name;
}

EvoFolder *
evo_folder_open (EvoStore *store, const gchar *name, GError **error)
{
	CamelStore *cs = evo_store_get_camel_store (store);
	CamelFolder *folder;
	GError *local_error = NULL;

	if (name == NULL || *name == '\0' ||
	    g_ascii_strcasecmp (name, "inbox") == 0) {
		folder = camel_store_get_inbox_folder_sync (cs, NULL, error);
		if (folder != NULL) {
			EvoFolder *ef = g_new0 (EvoFolder, 1);
			ef->folder = folder;
			return ef;
		}
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
				gchar *special = evo_find_special_folder (
					store, aliases[i].type, &local_error);
				if (special == NULL) {
					CamelFolderInfo *info =
						camel_store_get_folder_info_sync (
							cs, NULL,
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
						cs, special, 0, NULL, error);
					g_free (special);
					g_clear_error (&local_error);
					EvoFolder *ef = g_new0 (EvoFolder, 1);
					ef->folder = folder;
					return ef;
				}
				g_clear_error (&local_error);
				break;
			}
		}
	}

	folder = camel_store_get_folder_sync (cs, name, 0, NULL, NULL);
	if (folder != NULL) {
		EvoFolder *ef = g_new0 (EvoFolder, 1);
		ef->folder = folder;
		return ef;
	}

	{
		CamelFolderInfo *info;
		gchar *found;

		info = camel_store_get_folder_info_sync (
			cs, NULL, CAMEL_STORE_FOLDER_INFO_RECURSIVE,
			NULL, NULL);
		if (info == NULL) {
			g_set_error (error, CAMEL_STORE_ERROR,
				     CAMEL_STORE_ERROR_NO_FOLDER,
				     "Folder %s not found.", name);
			return NULL;
		}

		found = folder_info_find_name (info, name);
		camel_folder_info_free (info);
		if (found == NULL) {
			g_set_error (error, CAMEL_STORE_ERROR,
				     CAMEL_STORE_ERROR_NO_FOLDER,
				     "Folder %s not found.", name);
			return NULL;
		}

		folder = camel_store_get_folder_sync (cs, found, 0, NULL, error);
		g_free (found);
		EvoFolder *ef = g_new0 (EvoFolder, 1);
		ef->folder = folder;
		return ef;
	}
}

void
evo_folder_close (EvoFolder *folder)
{
	if (folder == NULL)
		return;
	g_object_unref (folder->folder);
	g_free (folder);
}

CamelFolder *
evo_folder_get_camel_folder (EvoFolder *folder)
{
	return folder->folder;
}

static gint
compare_messages (gconstpointer a, gconstpointer b)
{
	const EvoMessage *ma = *(EvoMessage * const *) a;
	const EvoMessage *mb = *(EvoMessage * const *) b;

	if (ma->date != mb->date)
		return ma->date < mb->date ? 1 : -1;
	return g_strcmp0 (mb->uid, ma->uid);
}

static EvoMessage *
message_from_info (CamelFolder *cfolder, const gchar *uid)
{
	CamelMessageInfo *info = camel_folder_get_message_info (cfolder, uid);
	EvoMessage *msg;

	if (info == NULL)
		return NULL;

	msg = g_new0 (EvoMessage, 1);
	msg->uid = g_strdup (uid);
	msg->from = g_strdup (camel_message_info_get_from (info));
	msg->subject = g_strdup (camel_message_info_get_subject (info));
	msg->date = format_date (camel_message_info_get_date_sent (info));
	msg->flags = camel_message_info_get_flags (info);
	g_object_unref (info);

	return msg;
}

GPtrArray *
evo_folder_list_messages (EvoFolder *folder, gboolean refresh,
                          GError **error)
{
	CamelFolder *cfolder = evo_folder_get_camel_folder (folder);
	GPtrArray *uids = NULL;
	GPtrArray *messages;

	if (refresh && !camel_folder_refresh_info_sync (cfolder, NULL, error))
		return NULL;

	if (!camel_folder_search_sync (cfolder, "#t", &uids, NULL, error))
		return NULL;

	messages = g_ptr_array_new_with_free_func (
		(GDestroyNotify) evo_message_free);

	for (guint i = 0; i < uids->len; i++) {
		const gchar *uid = g_ptr_array_index (uids, i);
		EvoMessage *msg = message_from_info (cfolder, uid);

		if (msg != NULL)
			g_ptr_array_add (messages, msg);
	}

	g_ptr_array_unref (uids);
	g_ptr_array_sort (messages, compare_messages);
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
evo_folder_search_messages (EvoFolder *folder, const gchar *query,
                            gboolean refresh, GError **error)
{
	CamelFolder *cfolder = evo_folder_get_camel_folder (folder);
	GPtrArray *uids = NULL;
	GPtrArray *messages;
	gchar *expression;

	if (refresh && !camel_folder_refresh_info_sync (cfolder, NULL, error))
		return NULL;

	expression = build_search_expression (query);
	if (!camel_folder_search_sync (cfolder, expression, &uids, NULL, error)) {
		g_free (expression);
		return NULL;
	}
	g_free (expression);

	messages = g_ptr_array_new_with_free_func (
		(GDestroyNotify) evo_message_free);

	for (guint i = 0; i < uids->len; i++) {
		const gchar *uid = g_ptr_array_index (uids, i);
		EvoMessage *msg = message_from_info (cfolder, uid);

		if (msg != NULL)
			g_ptr_array_add (messages, msg);
	}

	g_ptr_array_unref (uids);
	g_ptr_array_sort (messages, compare_messages);
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
collect_part (CamelMimePart *part, EvoMessage *msg, const gchar *save_dir,
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
				      msg, save_dir, attachment_number, error);
	} else if (content != NULL) {
		gboolean is_text_plain = content_type != NULL &&
			g_str_has_prefix (content_type, "text/plain");
		gboolean is_text_html = content_type != NULL &&
			g_str_has_prefix (content_type, "text/html");
		gboolean is_attachment = filename != NULL ||
			(disposition != NULL &&
			 g_ascii_strcasecmp (disposition, "attachment") == 0);

		if (is_text_plain && msg->text == NULL) {
			GBytes *bytes = decode_content (content, error);
			if (bytes != NULL) {
				msg->text = bytes_to_utf8 (bytes, charset);
				g_bytes_unref (bytes);
			}
		} else if (is_text_html && msg->html == NULL) {
			GBytes *bytes = decode_content (content, error);
			if (bytes != NULL) {
				msg->html = bytes_to_utf8 (bytes, charset);
				g_bytes_unref (bytes);
			}
		} else if (is_attachment) {
			EvoAttachment *att = g_new0 (EvoAttachment, 1);
			gchar *fallback_name = NULL;

			att->content_type = g_strdup (
				content_type != NULL ? content_type :
				"application/octet-stream");

			if (filename != NULL) {
				att->filename = g_strdup (filename);
			} else {
				fallback_name = g_strdup_printf ("attachment-%u",
								 *attachment_number);
				att->filename = g_strdup (fallback_name);
			}

			if (save_dir != NULL && *save_dir != '\0') {
				GBytes *bytes = decode_content (content, error);
				if (bytes != NULL) {
					gchar *path = safe_attachment_path (
						save_dir, att->filename,
						*attachment_number);
					gsize size = 0;

					g_bytes_get_data (bytes, &size);
					att->size = size;
					if (!g_file_set_contents (path,
						g_bytes_get_data (bytes, NULL),
						size, error)) {
						g_free (path);
						g_free (fallback_name);
						evo_attachment_free (att);
						g_bytes_unref (bytes);
						return;
					}
					g_free (path);
					g_bytes_unref (bytes);
				}
			} else {
				att->size =
					camel_data_wrapper_calculate_decoded_size_sync (
						content, NULL, NULL);
			}

			g_ptr_array_add (msg->attachments, att);
			(*attachment_number)++;
			g_free (fallback_name);
		}
	}

	g_free (content_type);
}

void
evo_attachment_free (EvoAttachment *att)
{
	if (att == NULL)
		return;
	g_free (att->filename);
	g_free (att->content_type);
	g_free (att);
}

static EvoMessage *
evo_message_new (void)
{
	EvoMessage *msg = g_new0 (EvoMessage, 1);
	msg->attachments = g_ptr_array_new_with_free_func (
		(GDestroyNotify) evo_attachment_free);
	return msg;
}

void
evo_message_free (EvoMessage *msg)
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

static gchar *
format_address (CamelInternetAddress *address)
{
	if (address == NULL)
		return NULL;
	return camel_address_format (CAMEL_ADDRESS (address));
}

EvoMessage *
evo_folder_read_message (EvoFolder *folder, const gchar *uid,
			 const gchar *save_dir, GError **error)
{
	CamelFolder *cfolder = evo_folder_get_camel_folder (folder);
	CamelMimeMessage *message;
	EvoMessage *msg;
	CamelInternetAddress *from;
	guint attachment_number = 1;

	message = camel_folder_get_message_sync (cfolder, uid, NULL, error);
	if (message == NULL)
		return NULL;

	msg = evo_message_new ();
	collect_part (CAMEL_MIME_PART (message), msg,
		      save_dir, &attachment_number, error);
	if (error != NULL && *error != NULL) {
		evo_message_free (msg);
		g_object_unref (message);
		return NULL;
	}

	msg->uid = g_strdup (uid);
	msg->subject = g_strdup (camel_mime_message_get_subject (message));
	from = camel_mime_message_get_from (message);
	msg->from = format_address (from);
	msg->to = format_address (camel_mime_message_get_recipients (
		message, CAMEL_RECIPIENT_TYPE_TO));
	msg->cc = format_address (camel_mime_message_get_recipients (
		message, CAMEL_RECIPIENT_TYPE_CC));
	msg->date = format_date (camel_mime_message_get_date (message, NULL));
	msg->message_id = g_strdup (camel_mime_message_get_message_id (message));

	{
		CamelMessageInfo *mi = camel_folder_get_message_info (cfolder, uid);
		if (mi != NULL) {
			msg->flags = camel_message_info_get_flags (mi);
			g_object_unref (mi);
		}
	}

	g_object_unref (message);
	return msg;
}

gboolean
evo_folder_export_message (EvoFolder *folder, const gchar *uid,
			   const gchar *path, GError **error)
{
	CamelFolder *cfolder = evo_folder_get_camel_folder (folder);
	CamelMimeMessage *message;
	gboolean success;

	message = camel_folder_get_message_sync (cfolder, uid, NULL, error);
	if (message == NULL)
		return FALSE;

	/* Remove existing file if any, then create */
	GFile *file = g_file_new_for_path (path);
	g_file_delete (file, NULL, NULL);
	GFileOutputStream *stream = g_file_create (file, G_FILE_CREATE_NONE, NULL, error);
	g_object_unref (file);
	if (stream == NULL) {
		g_object_unref (message);
		return FALSE;
	}

	success = camel_data_wrapper_write_to_output_stream_sync (
		CAMEL_DATA_WRAPPER (message), G_OUTPUT_STREAM (stream), NULL, error);
	g_output_stream_close (G_OUTPUT_STREAM (stream), NULL, NULL);
	g_object_unref (stream);
	g_object_unref (message);

	return success;
}

gboolean
evo_folder_mark_messages (EvoFolder *folder, const gchar * const *uids,
                          guint n_uids, EvoMessageFlag mask, EvoFlagOp op,
                          GError **error)
{
	CamelFolder *cfolder = evo_folder_get_camel_folder (folder);
	guint32 camel_mask = 0;
	guint32 camel_set = 0;

	if ((mask & EVO_MESSAGE_FLAG_SEEN) != 0)
		camel_mask |= CAMEL_MESSAGE_SEEN;
	if ((mask & EVO_MESSAGE_FLAG_FLAGGED) != 0)
		camel_mask |= CAMEL_MESSAGE_FLAGGED;
	if ((mask & EVO_MESSAGE_FLAG_DELETED) != 0)
		camel_mask |= CAMEL_MESSAGE_DELETED;

	if (op == EVO_FLAG_OP_SET || op == EVO_FLAG_OP_TOGGLE) {
		if ((mask & EVO_MESSAGE_FLAG_SEEN) != 0)
			camel_set |= CAMEL_MESSAGE_SEEN;
		if ((mask & EVO_MESSAGE_FLAG_FLAGGED) != 0)
			camel_set |= CAMEL_MESSAGE_FLAGGED;
		if ((mask & EVO_MESSAGE_FLAG_DELETED) != 0)
			camel_set |= CAMEL_MESSAGE_DELETED;
	}

	gboolean any_changed = FALSE;

	for (guint i = 0; i < n_uids; i++) {
		const gchar *uid = uids[i];
		CamelMessageInfo *info = camel_folder_get_message_info (cfolder, uid);
		guint32 before = info ? camel_message_info_get_flags (info) : 0;

		if (info != NULL &&
		    (before & camel_mask) == (camel_set & camel_mask)) {
			g_clear_object (&info);
			continue;
		}
		g_clear_object (&info);

		if (!camel_folder_set_message_flags (cfolder, uid,
						     camel_mask, camel_set)) {
			g_set_error (error, CAMEL_FOLDER_ERROR,
				     CAMEL_FOLDER_ERROR_INVALID,
				     "Could not update flags for message %s.",
				     uid);
			return FALSE;
		}
		any_changed = TRUE;
	}

	if (any_changed) {
		if (!camel_folder_synchronize_sync (cfolder, FALSE, NULL, error))
			return FALSE;
	}

	return TRUE;
}

gboolean
evo_folder_move_messages (EvoFolder *folder, const gchar * const *uids,
                          guint n_uids, EvoFolder *dest_folder,
                          GError **error)
{
	CamelFolder *cfolder = evo_folder_get_camel_folder (folder);
	CamelFolder *dest = evo_folder_get_camel_folder (dest_folder);
	gboolean success = TRUE;

	for (guint i = 0; i < n_uids && success; i++) {
		const gchar *uid = uids[i];

		/* Read the message to get its content */
		EvoMessage *msg = evo_folder_read_message (folder, uid, NULL, error);
		if (msg == NULL)
			return FALSE;

		/* Build a new MIME message */
		CamelMimeMessage *mime_msg = camel_mime_message_new ();
		CamelInternetAddress *from = camel_internet_address_new ();
		CamelInternetAddress *to = camel_internet_address_new ();
		CamelMimePart *body_part;
		CamelMultipart *multipart;
		CamelMimePart *text_part;

		if (msg->from)
			camel_internet_address_add (from, "", msg->from);
		if (msg->to)
			camel_internet_address_add (to, "", msg->to);
		camel_mime_message_set_from (mime_msg, from);
		camel_mime_message_set_recipients (mime_msg, CAMEL_RECIPIENT_TYPE_TO, to);
		g_object_unref (from);
		g_object_unref (to);

		camel_mime_message_set_subject (mime_msg, msg->subject ? msg->subject : "");
		if (msg->message_id)
			camel_mime_message_set_message_id (mime_msg, msg->message_id);

		multipart = camel_multipart_new ();
		camel_data_wrapper_set_mime_type (CAMEL_DATA_WRAPPER (multipart), "multipart/mixed");
		camel_multipart_set_boundary (multipart, g_strdup_printf ("----=_MovePart_%u.%u",
						       (guint) time (NULL), g_random_int ()));

		text_part = camel_mime_part_new ();
		gchar *type = g_strdup_printf ("text/plain; charset=UTF-8");
		camel_mime_part_set_content (text_part, msg->text ? msg->text : "", -1, type);
		camel_mime_part_set_encoding (text_part, CAMEL_TRANSFER_ENCODING_QUOTEDPRINTABLE);
		g_free (type);
		camel_multipart_add_part (multipart, text_part);
		g_object_unref (text_part);

		body_part = camel_mime_part_new ();
		camel_medium_set_content (CAMEL_MEDIUM (body_part), CAMEL_DATA_WRAPPER (multipart));
		camel_mime_part_set_disposition (body_part, "inline");
		camel_medium_set_content (CAMEL_MEDIUM (mime_msg), CAMEL_DATA_WRAPPER (body_part));
		g_object_unref (body_part);
		g_object_unref (multipart);

		/* Append to destination folder */
		if (!camel_folder_append_message_sync (dest, mime_msg, NULL, NULL, NULL, error)) {
			g_object_unref (mime_msg);
			evo_message_free (msg);
			return FALSE;
		}
		g_object_unref (mime_msg);
		evo_message_free (msg);

		/* Mark original as deleted */
		if (!camel_folder_set_message_flags (cfolder, uid,
						     CAMEL_MESSAGE_DELETED, CAMEL_MESSAGE_DELETED)) {
			g_set_error (error, CAMEL_FOLDER_ERROR,
				     CAMEL_FOLDER_ERROR_INVALID,
				     "Could not update flags for message %s.", uid);
			return FALSE;
		}
	}

	if (success)
		success = camel_folder_synchronize_sync (cfolder, FALSE, NULL, error);

	return success;
}

gboolean
evo_folder_copy_messages (EvoFolder *folder, const gchar * const *uids,
                          guint n_uids, EvoFolder *dest_folder,
                          GError **error)
{
	CamelFolder *cfolder = evo_folder_get_camel_folder (folder);
	CamelFolder *dest = evo_folder_get_camel_folder (dest_folder);
	GPtrArray *uid_array = g_ptr_array_new_with_free_func (g_free);
	gboolean success;

	for (guint i = 0; i < n_uids; i++)
		g_ptr_array_add (uid_array, g_strdup (uids[i]));

	success = camel_folder_transfer_messages_to_sync (cfolder,
			uid_array, dest, FALSE, NULL, NULL, error);
	if (!success) {
		g_printerr ("evomail: transfer_messages_to_sync failed: %s\n",
			    error != NULL && *error != NULL ? (*error)->message : "unknown");
	}
	if (success)
		success = camel_folder_synchronize_sync (cfolder, FALSE, NULL, error);

	g_ptr_array_unref (uid_array);
	return success;
}
