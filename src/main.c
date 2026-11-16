#include "evomail.h"
#include "evoapp.h"

#include <locale.h>
#include <camel/camel.h>
#include <string.h>
#include <unistd.h>

extern void json_string (GString *out, const gchar *value);
extern void print_message_table (GPtrArray *messages);
extern void print_message_json (GPtrArray *messages);
extern void print_read_message (EvoMessage *msg);
extern void print_read_json (EvoMessage *msg);

static gchar *opt_account = NULL;
static gchar *opt_folder = NULL;
static gboolean opt_json = FALSE;
static gint opt_limit = 20;
static gboolean opt_refresh = TRUE;
static gboolean opt_unread = FALSE;
static gboolean opt_html = FALSE;
static gchar *opt_save_attachments = NULL;
static gchar *opt_output = NULL;
static gboolean opt_mark_seen = FALSE;
static gboolean opt_mark_read = FALSE;
static gboolean opt_flag = FALSE;
static gboolean opt_unflag = FALSE;
static gboolean opt_delete = FALSE;
static gboolean opt_undelete = FALSE;
static gboolean opt_expunge = FALSE;
static gchar *opt_to = NULL;
static gchar *opt_cc = NULL;
static gchar *opt_bcc = NULL;
static gchar *opt_subject = NULL;
static gchar *opt_body = NULL;
static gchar *opt_body_file = NULL;
static gchar **opt_attach = NULL;
static gboolean opt_dry_run = FALSE;
static gboolean opt_save_copy = TRUE;
static gboolean opt_version = FALSE;
static gboolean opt_reply = FALSE;
static gboolean opt_reply_all = FALSE;
static gboolean opt_forward = FALSE;
static gboolean opt_forward_as_attachment = FALSE;
static gchar *opt_move_to = NULL;
static gchar *opt_copy_to = NULL;
static gboolean opt_config = FALSE;
static gchar *opt_config_set_account = NULL;
static gchar *opt_config_set_folder = NULL;
static gboolean opt_config_wizard = FALSE;

static const GOptionEntry entries[] = {
	{ "version", 0, 0, G_OPTION_ARG_NONE, &opt_version,
	  "Print version and exit", NULL },
	{ "account", 'a', 0, G_OPTION_ARG_STRING, &opt_account,
	  "Account email, name, UID, or UID prefix", "ACCOUNT" },
	{ "folder", 'f', 0, G_OPTION_ARG_STRING, &opt_folder,
	  "Folder name or alias: inbox, sent, drafts, trash, junk, all", "FOLDER" },
	{ "json", 0, 0, G_OPTION_ARG_NONE, &opt_json,
	  "Print JSON instead of text", NULL },
	{ "limit", 'n', 0, G_OPTION_ARG_INT, &opt_limit,
	  "Maximum messages to show, 0 for all (default 20)", "N" },
	{ "no-refresh", 0, G_OPTION_FLAG_REVERSE, G_OPTION_ARG_NONE, &opt_refresh,
	  "Do not synchronize the folder before listing", NULL },
	{ "unread", 0, 0, G_OPTION_ARG_NONE, &opt_unread,
	  "List: only unread messages. mark: clear the Seen flag", NULL },
	{ "html", 0, 0, G_OPTION_ARG_NONE, &opt_html,
	  "Read: show the HTML part. send: treat the body as HTML", NULL },
	{ "save-attachments", 0, 0, G_OPTION_ARG_FILENAME, &opt_save_attachments,
	  "Save attachments to this directory when reading", "DIR" },
	{ "output", 'o', 0, G_OPTION_ARG_FILENAME, &opt_output,
	  "Export: save raw message to this file", "FILE" },
	{ "mark-seen", 0, 0, G_OPTION_ARG_NONE, &opt_mark_seen,
	  "Mark the message as seen after reading", NULL },
	{ "read", 0, 0, G_OPTION_ARG_NONE, &opt_mark_read,
	  "Mark: set the Seen flag", NULL },
	{ "flag", 0, 0, G_OPTION_ARG_NONE, &opt_flag,
	  "Mark: set the Flagged (starred) flag", NULL },
	{ "unflag", 0, 0, G_OPTION_ARG_NONE, &opt_unflag,
	  "Mark: clear the Flagged flag", NULL },
	{ "delete", 0, 0, G_OPTION_ARG_NONE, &opt_delete,
	  "Mark: set the Deleted flag", NULL },
	{ "undelete", 0, 0, G_OPTION_ARG_NONE, &opt_undelete,
	  "Mark: clear the Deleted flag", NULL },
	{ "expunge", 0, 0, G_OPTION_ARG_NONE, &opt_expunge,
	  "Mark: expunge deleted messages after flagging", NULL },
	{ "to", 't', 0, G_OPTION_ARG_STRING, &opt_to,
	  "Send: To recipients", "ADDRESSES" },
	{ "cc", 0, 0, G_OPTION_ARG_STRING, &opt_cc,
	  "Send: Cc recipients", "ADDRESSES" },
	{ "bcc", 0, 0, G_OPTION_ARG_STRING, &opt_bcc,
	  "Send: Bcc recipients", "ADDRESSES" },
	{ "subject", 's', 0, G_OPTION_ARG_STRING, &opt_subject,
	  "Send: subject", "TEXT" },
	{ "body", 'b', 0, G_OPTION_ARG_STRING, &opt_body,
	  "Send: message body", "TEXT" },
	{ "body-file", 0, 0, G_OPTION_ARG_FILENAME, &opt_body_file,
	  "Send: read the message body from a file", "FILE" },
	{ "attach", 0, 0, G_OPTION_ARG_FILENAME_ARRAY, &opt_attach,
	  "Send: attach a file (repeatable)", "FILE" },
	{ "dry-run", 0, 0, G_OPTION_ARG_NONE, &opt_dry_run,
	  "Send: build the message and print a summary without sending", NULL },
	{ "no-save-copy", 0, G_OPTION_FLAG_REVERSE, G_OPTION_ARG_NONE, &opt_save_copy,
	  "Send: do not append a copy to the Sent folder", NULL },
	{ "reply", 0, 0, G_OPTION_ARG_NONE, &opt_reply,
	  "Reply to a message", NULL },
	{ "reply-all", 0, 0, G_OPTION_ARG_NONE, &opt_reply_all,
	  "Reply to all recipients", NULL },
	{ "forward", 0, 0, G_OPTION_ARG_NONE, &opt_forward,
	  "Forward a message", NULL },
	{ "forward-as-attachment", 0, 0, G_OPTION_ARG_NONE, &opt_forward_as_attachment,
	  "Forward a message as an attachment", NULL },
	{ "move-to", 0, 0, G_OPTION_ARG_STRING, &opt_move_to,
	  "Move messages to a folder", "FOLDER" },
	{ "copy-to", 0, 0, G_OPTION_ARG_STRING, &opt_copy_to,
	  "Copy messages to a folder", "FOLDER" },
	{ "config", 0, 0, G_OPTION_ARG_NONE, &opt_config,
	  "Configure evomail settings", NULL },
	{ "config-set-account", 0, 0, G_OPTION_ARG_STRING, &opt_config_set_account,
	  "Config: set default account", "ACCOUNT" },
	{ "config-set-folder", 0, 0, G_OPTION_ARG_STRING, &opt_config_set_folder,
	  "Config: set default folder", "FOLDER" },
	{ "config-wizard", 0, 0, G_OPTION_ARG_NONE, &opt_config_wizard,
	  "Config: interactive setup wizard", NULL },
	{ NULL }
};

static void
print_folder_tree_text (CamelFolderInfo *info, guint depth)
{
	for (CamelFolderInfo *i = info; i != NULL; i = i->next) {
		g_print ("%*s%s", depth * 2, "", i->full_name);
		if (i->total >= 0)
			g_print ("  (%d messages, %d unread)",
				 i->total, i->unread);
		g_print ("\n");

		if (i->child != NULL)
			print_folder_tree_text (i->child, depth + 1);
	}
}

static void
print_folder_tree_json (CamelFolderInfo *info, GString *out, gboolean *first)
{
	for (CamelFolderInfo *i = info; i != NULL; i = i->next) {
		if (!*first)
			g_string_append_c (out, ',');
		*first = FALSE;
		g_string_append_c (out, '\n');
		g_string_append (out, "  {\"name\":");
		json_string (out, i->full_name);
		g_string_append_c (out, '}');
		if (i->child != NULL)
			print_folder_tree_json (i->child, out, first);
	}
}

static gboolean
cmd_folders (EvoApp *app, GError **error)
{
	EvoAccount *account = evo_app_find_account (app, opt_account, error);
	EvoStore *store;
	CamelFolderInfo *info;
	gboolean first = TRUE;

	if (account == NULL)
		return FALSE;

	store = evo_store_open (app, account, error);
	if (store == NULL)
		return FALSE;

	info = camel_store_get_folder_info_sync (
		evo_store_get_camel_store (store),
		NULL,
		CAMEL_STORE_FOLDER_INFO_RECURSIVE | CAMEL_STORE_FOLDER_INFO_REFRESH,
		NULL, error);
	if (info == NULL) {
		evo_store_close (store);
		return FALSE;
	}

	if (opt_json) {
		GString *out = g_string_new ("[\n");
		for (CamelFolderInfo *i = info; i != NULL; i = i->next) {
			if (!first)
				g_string_append_c (out, ',');
			first = FALSE;
			g_string_append_c (out, '\n');
			g_string_append (out, "  {\"name\":");
			json_string (out, i->full_name);
			g_string_append_c (out, '}');
			if (i->child != NULL)
				print_folder_tree_json (i->child, out, &first);
		}
		g_string_append_c (out, '\n');
		g_string_append_c (out, ']');
		g_print ("%s", out->str);
		g_string_free (out, TRUE);
	} else {
		print_folder_tree_text (info, 0);
	}

	camel_folder_info_free (info);
	evo_store_close (store);
	return TRUE;
}

static void
filter_unread (GPtrArray *messages)
{
	for (guint i = messages->len; i > 0; i--) {
		EvoMessage *msg = g_ptr_array_index (messages, i - 1);

		if (msg->flags & CAMEL_MESSAGE_SEEN)
			g_ptr_array_remove_index (messages, i - 1);
	}
}

static void
apply_limit (GPtrArray *messages)
{
	if (opt_limit > 0 && messages->len > (guint) opt_limit)
		g_ptr_array_remove_range (messages, opt_limit,
					  messages->len - opt_limit);
}

static gboolean
cmd_list (EvoApp *app, gboolean search, gchar **terms, GError **error)
{
	EvoAccount *account = evo_app_find_account (app, opt_account, error);
	EvoStore *store;
	EvoFolder *folder;
	GPtrArray *messages;
	gchar *query = NULL;

	if (account == NULL)
		return FALSE;

	store = evo_store_open (app, account, error);
	if (store == NULL)
		return FALSE;

	folder = evo_folder_open (store, opt_folder, error);
	if (folder == NULL) {
		evo_store_close (store);
		return FALSE;
	}

	if (search) {
		query = g_strjoinv (" ", terms);
		messages = evo_folder_search_messages (folder, query ? query : "",
						       opt_refresh, error);
		g_free (query);
	} else {
		messages = evo_folder_list_messages (folder, opt_refresh, error);
	}

	if (messages == NULL) {
		evo_folder_close (folder);
		evo_store_close (store);
		return FALSE;
	}

	if (opt_unread)
		filter_unread (messages);
	apply_limit (messages);

	if (opt_json)
		print_message_json (messages);
	else
		print_message_table (messages);

	g_ptr_array_unref (messages);
	evo_folder_close (folder);
	evo_store_close (store);
	return TRUE;
}

static gboolean
cmd_read (EvoApp *app, gchar *uid, GError **error)
{
	EvoAccount *account = evo_app_find_account (app, opt_account, error);
	EvoStore *store;
	EvoFolder *folder;
	EvoMessage *message;

	if (account == NULL)
		return FALSE;

	store = evo_store_open (app, account, error);
	if (store == NULL)
		return FALSE;

	folder = evo_folder_open (store, opt_folder, error);
	if (folder == NULL) {
		evo_store_close (store);
		return FALSE;
	}

	message = evo_folder_read_message (folder, uid, opt_save_attachments,
					   error);
	if (message == NULL) {
		evo_folder_close (folder);
		evo_store_close (store);
		return FALSE;
	}

	if (opt_html && message->html != NULL) {
		g_free (message->text);
		message->text = NULL;
	}

	if (opt_json)
		print_read_json (message);
	else
		print_read_message (message);

	if (opt_mark_seen) {
		const gchar * const mark_uids[] = { uid };
		if (!evo_folder_mark_messages (folder, mark_uids, 1,
					       EVO_MESSAGE_FLAG_SEEN,
					       EVO_FLAG_OP_SET, NULL)) {
			g_printerr ("evomail: could not mark message as seen\n");
		}
	}

	evo_message_free (message);
	evo_folder_close (folder);
	evo_store_close (store);
	return TRUE;
}

static gboolean
cmd_mark (EvoApp *app, gchar **uids, gint n_uids, GError **error)
{
	EvoAccount *account = evo_app_find_account (app, opt_account, error);
	EvoStore *store;
	EvoFolder *folder;
	EvoMessageFlag mask = 0;
	EvoFlagOp op = EVO_FLAG_OP_SET;
	gboolean success;

	if (account == NULL)
		return FALSE;

	if (n_uids == 0) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
				     "No message UIDs given.");
		return FALSE;
	}

	if (opt_mark_read) {
		mask |= EVO_MESSAGE_FLAG_SEEN;
		op = EVO_FLAG_OP_SET;
	}
	if (opt_unread) {
		mask |= EVO_MESSAGE_FLAG_SEEN;
		op = EVO_FLAG_OP_CLEAR;
	}
	if (opt_flag) {
		mask |= EVO_MESSAGE_FLAG_FLAGGED;
		op = EVO_FLAG_OP_SET;
	}
	if (opt_unflag) {
		mask |= EVO_MESSAGE_FLAG_FLAGGED;
		op = EVO_FLAG_OP_CLEAR;
	}
	if (opt_delete) {
		mask |= EVO_MESSAGE_FLAG_DELETED;
		op = EVO_FLAG_OP_SET;
	}
	if (opt_undelete) {
		mask |= EVO_MESSAGE_FLAG_DELETED;
		op = EVO_FLAG_OP_CLEAR;
	}

	if (mask == 0) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
				     "No flag change given. Use --read, --unread, "
				     "--flag, --unflag, --delete, or --undelete.");
		return FALSE;
	}

	store = evo_store_open (app, account, error);
	if (store == NULL)
		return FALSE;

	folder = evo_folder_open (store, opt_folder, error);
	if (folder == NULL) {
		evo_store_close (store);
		return FALSE;
	}

	success = evo_folder_mark_messages (folder, (const gchar * const *) uids,
					    n_uids, mask, op, error);
	if (success && opt_expunge) {
		CamelFolder *cfolder = evo_folder_get_camel_folder (folder);
		success = camel_folder_expunge_sync (cfolder, NULL, error);
	}

	if (success) {
		if (opt_json)
			g_print ("{\"updated\":%d}\n", n_uids);
		else
			g_print ("Updated %d message%s.\n", n_uids,
				 n_uids == 1 ? "" : "s");
	}

	evo_folder_close (folder);
	evo_store_close (store);
	return success;
}

static gboolean
cmd_send (EvoApp *app, GError **error)
{
	EvoAccount *account = evo_app_find_account (app, opt_account, error);
	GPtrArray *attach_paths = NULL;
	gchar *body = NULL;
	gboolean success;

	if (account == NULL)
		return FALSE;

	if (opt_body != NULL && opt_body_file != NULL) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
				     "Use either --body or --body-file, not both.");
		return FALSE;
	}

	if (opt_body != NULL) {
		body = g_strdup (opt_body);
	} else if (opt_body_file != NULL) {
		if (!g_file_get_contents (opt_body_file, &body, NULL, error))
			return FALSE;
	} else if (!isatty (STDIN_FILENO)) {
		GString *input = g_string_new (NULL);
		gchar buffer[4096];
		gsize count;

		while ((count = fread (buffer, 1, sizeof (buffer), stdin)) > 0)
			g_string_append_len (input, buffer, count);
		if (ferror (stdin)) {
			g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED,
					     "Could not read the message body from stdin.");
			g_string_free (input, TRUE);
			return FALSE;
		}
		body = g_string_free (input, FALSE);
	} else {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
				     "No message body given. Use --body, --body-file, "
				     "or pipe the body on stdin.");
		return FALSE;
	}

	if (opt_attach != NULL) {
		attach_paths = g_ptr_array_new ();

		for (gint i = 0; opt_attach[i] != NULL; i++) {
			if (!g_file_test (opt_attach[i], G_FILE_TEST_IS_REGULAR)) {
				g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
					     "Attachment “%s” is not a regular file.",
					     opt_attach[i]);
				g_ptr_array_unref (attach_paths);
				g_free (body);
				return FALSE;
			}
			g_ptr_array_add (attach_paths, opt_attach[i]);
		}
	}

	success = evo_send (app, account, opt_to, opt_cc, opt_bcc,
			    opt_subject, body, opt_html, attach_paths,
			    opt_dry_run, opt_save_copy, opt_json, error);

	if (attach_paths != NULL)
		g_ptr_array_unref (attach_paths);
	g_free (body);
	return success;
}

static gboolean
cmd_reply (EvoApp *app, gchar *uid, GError **error)
{
	EvoAccount *account = evo_app_find_account (app, opt_account, error);
	EvoStore *store;
	EvoFolder *folder;
	EvoMessage *message;
	gboolean success;

	if (account == NULL)
		return FALSE;

	if (opt_body == NULL && opt_body_file == NULL && isatty (STDIN_FILENO)) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
				     "No message body given. Use --body, --body-file, "
				     "or pipe the body on stdin.");
		return FALSE;
	}

	if (opt_body != NULL && opt_body_file != NULL) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
				     "Use either --body or --body-file, not both.");
		return FALSE;
	}

	if (opt_body != NULL) {
		/* body already set */
	} else if (opt_body_file != NULL) {
		if (!g_file_get_contents (opt_body_file, &opt_body, NULL, error))
			return FALSE;
	} else {
		GString *input = g_string_new (NULL);
		gchar buffer[4096];
		gsize count;

		while ((count = fread (buffer, 1, sizeof (buffer), stdin)) > 0)
			g_string_append_len (input, buffer, count);
		if (ferror (stdin)) {
			g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED,
					     "Could not read the message body from stdin.");
			g_string_free (input, TRUE);
			return FALSE;
		}
		opt_body = g_string_free (input, FALSE);
	}

	store = evo_store_open (app, account, error);
	if (store == NULL)
		return FALSE;

	folder = evo_folder_open (store, opt_folder, error);
	if (folder == NULL) {
		evo_store_close (store);
		return FALSE;
	}

	message = evo_folder_read_message (folder, uid, NULL, error);
	if (message == NULL) {
		evo_folder_close (folder);
		evo_store_close (store);
		return FALSE;
	}

	GPtrArray *reply_attach_paths = NULL;
	if (opt_attach != NULL) {
		reply_attach_paths = g_ptr_array_new ();
		for (gint i = 0; opt_attach[i] != NULL; i++)
			g_ptr_array_add (reply_attach_paths, g_strdup (opt_attach[i]));
	}

	success = evo_send_reply (app, account, message,
				  opt_body, opt_html, reply_attach_paths,
				  opt_dry_run, opt_save_copy, opt_json,
				  opt_reply_all,
				  error);

	if (reply_attach_paths != NULL) {
		for (gint i = 0; i < reply_attach_paths->len; i++)
			g_free (g_ptr_array_index (reply_attach_paths, i));
		g_ptr_array_unref (reply_attach_paths);
	}
	evo_message_free (message);
	evo_folder_close (folder);
	evo_store_close (store);
	return success;
}

static gboolean
cmd_forward (EvoApp *app, gchar *uid, GError **error)
{
	EvoAccount *account = evo_app_find_account (app, opt_account, error);
	EvoStore *store;
	EvoFolder *folder;
	EvoMessage *message;
	gboolean success;

	if (account == NULL)
		return FALSE;

	if (opt_body == NULL && opt_body_file == NULL && isatty (STDIN_FILENO)) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
				     "No message body given. Use --body, --body-file, "
				     "or pipe the body on stdin.");
		return FALSE;
	}

	if (opt_body != NULL && opt_body_file != NULL) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
				     "Use either --body or --body-file, not both.");
		return FALSE;
	}

	if (opt_body != NULL) {
		/* body already set */
	} else if (opt_body_file != NULL) {
		if (!g_file_get_contents (opt_body_file, &opt_body, NULL, error))
			return FALSE;
	} else {
		GString *input = g_string_new (NULL);
		gchar buffer[4096];
		gsize count;

		while ((count = fread (buffer, 1, sizeof (buffer), stdin)) > 0)
			g_string_append_len (input, buffer, count);
		if (ferror (stdin)) {
			g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED,
					     "Could not read the message body from stdin.");
			g_string_free (input, TRUE);
			return FALSE;
		}
		opt_body = g_string_free (input, FALSE);
	}

	store = evo_store_open (app, account, error);
	if (store == NULL)
		return FALSE;

	folder = evo_folder_open (store, opt_folder, error);
	if (folder == NULL) {
		evo_store_close (store);
		return FALSE;
	}

	message = evo_folder_read_message (folder, uid, NULL, error);
	if (message == NULL) {
		evo_folder_close (folder);
		evo_store_close (store);
		return FALSE;
	}

	GPtrArray *fwd_attach_paths = NULL;
	if (opt_attach != NULL) {
		fwd_attach_paths = g_ptr_array_new ();
		for (gint i = 0; opt_attach[i] != NULL; i++)
			g_ptr_array_add (fwd_attach_paths, g_strdup (opt_attach[i]));
	}

	success = evo_send_forward (app, account, message,
				    opt_body, opt_html, fwd_attach_paths,
				    opt_dry_run, opt_save_copy, opt_json,
				    error);

	if (fwd_attach_paths != NULL)
		g_ptr_array_unref (fwd_attach_paths);
	evo_message_free (message);
	evo_folder_close (folder);
	evo_store_close (store);
	return success;
}

static gboolean
cmd_forward_as_attachment (EvoApp *app, gchar *uid, GError **error)
{
	EvoAccount *account = evo_app_find_account (app, opt_account, error);
	EvoStore *store;
	EvoFolder *folder;
	EvoMessage *message;
	gboolean success;

	if (account == NULL)
		return FALSE;

	if (opt_body == NULL && opt_body_file == NULL && isatty (STDIN_FILENO)) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
				     "No message body given. Use --body, --body-file, "
				     "or pipe the body on stdin.");
		return FALSE;
	}

	if (opt_body != NULL && opt_body_file != NULL) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
				     "Use either --body or --body-file, not both.");
		return FALSE;
	}

	if (opt_body != NULL) {
		/* body already set */
	} else if (opt_body_file != NULL) {
		if (!g_file_get_contents (opt_body_file, &opt_body, NULL, error))
			return FALSE;
	} else {
		GString *input = g_string_new (NULL);
		gchar buffer[4096];
		gsize count;

		while ((count = fread (buffer, 1, sizeof (buffer), stdin)) > 0)
			g_string_append_len (input, buffer, count);
		if (ferror (stdin)) {
			g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED,
					     "Could not read the message body from stdin.");
			g_string_free (input, TRUE);
			return FALSE;
		}
		opt_body = g_string_free (input, FALSE);
	}

	store = evo_store_open (app, account, error);
	if (store == NULL)
		return FALSE;

	folder = evo_folder_open (store, opt_folder, error);
	if (folder == NULL) {
		evo_store_close (store);
		return FALSE;
	}

	message = evo_folder_read_message (folder, uid, NULL, error);
	if (message == NULL) {
		evo_folder_close (folder);
		evo_store_close (store);
		return FALSE;
	}

	GPtrArray *fwd_attach_paths = NULL;
	if (opt_attach != NULL) {
		fwd_attach_paths = g_ptr_array_new ();
		for (gint i = 0; opt_attach[i] != NULL; i++)
			g_ptr_array_add (fwd_attach_paths, g_strdup (opt_attach[i]));
	}

	success = evo_send_forward_as_attachment (app, account, message,
						  opt_body, opt_html, fwd_attach_paths,
						  opt_dry_run, opt_save_copy, opt_json,
						  error);

	if (fwd_attach_paths != NULL) {
		for (gint i = 0; i < fwd_attach_paths->len; i++)
			g_free (g_ptr_array_index (fwd_attach_paths, i));
		g_ptr_array_unref (fwd_attach_paths);
	}
	evo_message_free (message);
	evo_folder_close (folder);
	evo_store_close (store);
	return success;
}

static gboolean
cmd_export (EvoApp *app, gchar *uid, GError **error)
{
	EvoAccount *account = evo_app_find_account (app, opt_account, error);
	EvoStore *store;
	EvoFolder *folder;
	gboolean success;

	if (account == NULL)
		return FALSE;

	if (opt_output == NULL) {
		g_set_error_literal (error, G_IO_ERROR,
				     G_IO_ERROR_INVALID_ARGUMENT,
				     "Export needs an output file. Use -o, --output FILE.");
		return FALSE;
	}

	store = evo_store_open (app, account, error);
	if (store == NULL)
		return FALSE;

	folder = evo_folder_open (store, opt_folder, error);
	if (folder == NULL) {
		evo_store_close (store);
		return FALSE;
	}

	success = evo_folder_export_message (folder, uid, opt_output, error);

	evo_folder_close (folder);
	evo_store_close (store);

	if (success) {
		if (opt_json)
			g_print ("{\"exported\":true,\"path\":\"%s\"}\n", opt_output);
		else
			g_print ("Exported message %s to %s\n", uid, opt_output);
	}

	return success;
}

static gboolean
cmd_delete (EvoApp *app, gchar **uids, gint n_uids, GError **error)
{
	EvoAccount *account = evo_app_find_account (app, opt_account, error);
	EvoStore *store;
	EvoFolder *folder;
	gboolean success;

	if (account == NULL)
		return FALSE;

	if (n_uids == 0) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
				     "No message UIDs given.");
		return FALSE;
	}

	store = evo_store_open (app, account, error);
	if (store == NULL)
		return FALSE;

	folder = evo_folder_open (store, opt_folder, error);
	if (folder == NULL) {
		evo_store_close (store);
		return FALSE;
	}

	/* Mark messages as deleted */
	success = evo_folder_mark_messages (folder, (const gchar * const *) uids,
					    n_uids, EVO_MESSAGE_FLAG_DELETED,
					    EVO_FLAG_OP_SET, error);
	if (success) {
		/* Expunge deleted messages */
		CamelFolder *cfolder = evo_folder_get_camel_folder (folder);
		success = camel_folder_expunge_sync (cfolder, NULL, error);
	}

	if (success) {
		if (opt_json)
			g_print ("{\"deleted\":%d}\n", n_uids);
		else
			g_print ("Deleted and expunged %d message%s.\n", n_uids,
				 n_uids == 1 ? "" : "s");
	}

	evo_folder_close (folder);
	evo_store_close (store);
	return success;
}

static gboolean
cmd_move (EvoApp *app, gchar **uids, gint n_uids, GError **error)
{
	EvoAccount *account = evo_app_find_account (app, opt_account, error);
	EvoStore *store;
	EvoFolder *folder;
	EvoFolder *dest_folder;
	gboolean success;

	if (account == NULL)
		return FALSE;

	if (n_uids == 0) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
				     "No message UIDs given.");
		return FALSE;
	}

	if (opt_move_to == NULL) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
				     "No destination folder given. Use --move-to FOLDER.");
		return FALSE;
	}

	store = evo_store_open (app, account, error);
	if (store == NULL)
		return FALSE;

	folder = evo_folder_open (store, opt_folder, error);
	if (folder == NULL) {
		evo_store_close (store);
		return FALSE;
	}

	dest_folder = evo_folder_open (store, opt_move_to, error);
	if (dest_folder == NULL) {
		evo_folder_close (folder);
		evo_store_close (store);
		return FALSE;
	}

	success = evo_folder_move_messages (folder, (const gchar * const *) uids,
					    n_uids, dest_folder, error);

	if (!success) {
		g_printerr ("evomail: move failed: %s\n",
			    error != NULL && *error != NULL ? (*error)->message : "unknown");
	} else if (opt_json) {
		g_print ("{\"moved\":%d}\n", n_uids);
	} else {
		g_print ("Moved %d message%s.\n", n_uids,
			 n_uids == 1 ? "" : "s");
	}

	evo_folder_close (dest_folder);
	evo_folder_close (folder);
	evo_store_close (store);
	return success;
}

static gboolean
cmd_copy (EvoApp *app, gchar **uids, gint n_uids, GError **error)
{
	EvoAccount *account = evo_app_find_account (app, opt_account, error);
	EvoStore *store;
	EvoFolder *folder;
	EvoFolder *dest_folder;
	gboolean success;

	if (account == NULL)
		return FALSE;

	if (n_uids == 0) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
				     "No message UIDs given.");
		return FALSE;
	}

	if (opt_copy_to == NULL) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
				     "No destination folder given. Use --copy-to FOLDER.");
		return FALSE;
	}

	store = evo_store_open (app, account, error);
	if (store == NULL)
		return FALSE;

	folder = evo_folder_open (store, opt_folder, error);
	if (folder == NULL) {
		evo_store_close (store);
		return FALSE;
	}

	dest_folder = evo_folder_open (store, opt_copy_to, error);
	if (dest_folder == NULL) {
		evo_folder_close (folder);
		evo_store_close (store);
		return FALSE;
	}

	success = evo_folder_copy_messages (folder, (const gchar * const *) uids,
					    n_uids, dest_folder, error);

	if (!success) {
		g_printerr ("evomail: copy failed: %s\n",
			    error != NULL && *error != NULL ? (*error)->message : "unknown");
	} else if (opt_json) {
		g_print ("{\"copied\":%d}\n", n_uids);
	} else {
		g_print ("Copied %d message%s.\n", n_uids,
			 n_uids == 1 ? "" : "s");
	}

	evo_folder_close (dest_folder);
	evo_folder_close (folder);
	evo_store_close (store);
	return success;
}

static gboolean
cmd_config (EvoApp *app, GError **error)
{
	gboolean changed = FALSE;

	if (opt_config_set_account != NULL) {
		if (!evo_config_set_default_account (opt_config_set_account)) {
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
				     "Failed to set default account to %s",
				     opt_config_set_account);
			return FALSE;
		}
		g_print ("Default account set to %s\n", opt_config_set_account);
		changed = TRUE;
	}

	if (opt_config_set_folder != NULL) {
		if (!evo_config_set_default_folder (opt_config_set_folder)) {
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
				     "Failed to set default folder to %s",
				     opt_config_set_folder);
			return FALSE;
		}
		g_print ("Default folder set to %s\n", opt_config_set_folder);
		changed = TRUE;
	}

	if (!changed) {
		/* Print current config */
		const gchar *def_account = evo_config_get_default_account ();
		const gchar *def_folder = evo_config_get_default_folder ();

		g_print ("Current configuration:\n");
		if (def_account != NULL)
			g_print ("  default account: %s\n", def_account);
		else
			g_print ("  default account: (none)\n");
		if (def_folder != NULL)
			g_print ("  default folder: %s\n", def_folder);
		else
			g_print ("  default folder: (none)\n");
	}

	return TRUE;
}

static gboolean
cmd_config_wizard (EvoApp *app, GError **error)
{
	GPtrArray *accounts;
	gint choice = -1;
	const gchar *def_account = evo_config_get_default_account ();

	g_print ("=== evomail Configuration Wizard ===\n\n");

	/* List available accounts */
	accounts = evo_app_get_accounts (app);
	if (accounts == NULL || accounts->len == 0) {
		g_print ("No mail accounts found in Evolution Data Server.\n");
		g_print ("Please configure an account in Evolution first.\n");
		return TRUE;
	}

	g_print ("Available accounts:\n");
	for (guint i = 0; i < accounts->len; i++) {
		EvoAccount *acct = g_ptr_array_index (accounts, i);
		const gchar *uid = evo_account_get_uid (acct);
		const gchar *email = evo_account_get_email (acct);
		const gchar *name = evo_account_get_name (acct);
		g_print ("  %u) %s <%s> [%s]\n",
			 i + 1,
			 name != NULL ? name : email,
			 email != NULL ? email : uid,
			 uid);
		if (def_account != NULL && g_strcmp0 (uid, def_account) == 0)
			g_print ("      (current default)\n");
	}

	g_print ("\nSelect default account (1-%u): ", accounts->len);

	{
		gchar buf[256];
		if (fgets (buf, sizeof (buf), stdin) == NULL) {
			g_print ("\nWizard cancelled.\n");
			return TRUE;
		}
		choice = g_ascii_strtod (buf, NULL) - 1;
		if (choice < 0 || choice >= (gint) accounts->len) {
			g_print ("Invalid selection.\n");
			return TRUE;
		}
	}

	EvoAccount *selected = g_ptr_array_index (accounts, choice);
	const gchar *selected_uid = evo_account_get_uid (selected);
	const gchar *selected_email = evo_account_get_email (selected);

	if (!evo_config_set_default_account (selected_uid)) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
			     "Failed to set default account to %s", selected_uid);
		return FALSE;
	}
	g_print ("Default account set to %s\n\n", selected_email != NULL ? selected_email : selected_uid);

	/* Prompt for default folder */
	g_print ("Enter default folder name (e.g. inbox, sent, drafts): ");
	{
		gchar buf[256];
		if (fgets (buf, sizeof (buf), stdin) == NULL) {
			g_print ("\nWizard cancelled.\n");
			return TRUE;
		}
		/* Strip newline */
		gsize len = strlen (buf);
		if (len > 0 && buf[len - 1] == '\n')
			buf[len - 1] = '\0';

		if (buf[0] != '\0') {
			if (!evo_config_set_default_folder (buf)) {
				g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
					     "Failed to set default folder to %s", buf);
				return FALSE;
			}
			g_print ("Default folder set to %s\n", buf);
		}
	}

	g_print ("\nWizard complete. Config saved to ~/.config/evomail/config.ini\n");

	return TRUE;
}

int
main (int argc, char **argv)
{
	GOptionContext *context;
	GError *error = NULL;
	EvoApp *app;
	const gchar *command;
	gint result = 0;

	setlocale (LC_ALL, "");

	context = g_option_context_new ("COMMAND [ARGS] - a CLI for Evolution mail");
	g_option_context_add_main_entries (context, entries, NULL);
	g_option_context_set_summary (context,
		"Commands:\n"
		"  accounts                         List Evolution mail accounts\n"
		"  folders                          List folders for an account\n"
		"  list                             List messages (default: newest 20)\n"
		"  read UID                         Print one message\n"
		"  search QUERY                     Search a folder\n"
		"  mark UID...                      Change message flags\n"
		"  export UID -o FILE               Export a raw message to a file\n"
		"  delete UID...                    Delete and expunge messages\n"
		"  send                             Compose and send a message\n"
		"  reply UID                        Reply to a message\n"
		"  forward UID                      Forward a message\n"
		"  forward-as-attachment UID        Forward a message as .eml attachment\n"
		"  move UID... --to FOLDER          Move messages to a folder\n"
		"  copy UID... --to FOLDER          Copy messages to a folder\n"
		"\n"
		"Search keys: from:, to:, subject:, body:, is:unread, is:read,\n"
		"is:flagged, is:unflagged. Other words match subject, sender, and body.\n"
		"\n"
		"Examples:\n"
		"  evomail accounts\n"
		"  evomail list --unread\n"
		"  evomail read 1234 --save-attachments ~/Downloads\n"
		"  evomail search 'from:github is:unread'\n"
		"  evomail mark 1234 1235 --read\n"
		"  evomail send --to you@example.com --subject Hi --body 'Hello'\n"
		"  evomail export 1234 -o ~/message.eml\n"
		"\n"
		"Environment: EVOMAIL_ACCOUNT selects the default account.");

	if (!g_option_context_parse (context, &argc, &argv, &error)) {
		g_printerr ("evomail: %s\n", error->message);
		g_clear_error (&error);
		result = 1;
		goto cleanup;
	}

	if (opt_version) {
		g_print ("evomail 0.1.0\n");
		result = 0;
		goto cleanup;
	}

	if (opt_config_set_account != NULL) {
		if (!evo_config_set_default_account (opt_config_set_account)) {
			g_printerr ("evomail: failed to set default account to %s\n",
				    opt_config_set_account);
			result = 1;
			goto cleanup;
		}
		g_print ("Default account set to %s\n", opt_config_set_account);
		result = 0;
		goto cleanup;
	}

	if (opt_config_set_folder != NULL) {
		if (!evo_config_set_default_folder (opt_config_set_folder)) {
			g_printerr ("evomail: failed to set default folder to %s\n",
				    opt_config_set_folder);
			result = 1;
			goto cleanup;
		}
		g_print ("Default folder set to %s\n", opt_config_set_folder);
		result = 0;
		goto cleanup;
	}

	if (opt_config_wizard) {
		app = evo_app_new (&error);
		if (app == NULL) {
			g_printerr ("evomail: %s\n", error->message);
			g_clear_error (&error);
			result = 1;
			goto cleanup;
		}
		if (!evo_config_load ()) {
			/* Config file not found or invalid - continue without defaults */
		}
		if (!evo_load_accounts (app, &error)) {
			g_printerr ("evomail: %s\n", error->message);
			g_clear_error (&error);
			result = 1;
			goto cleanup;
		}
		if (!cmd_config_wizard (app, &error)) {
			g_printerr ("evomail: %s\n",
				    error != NULL ? error->message : "unknown");
			result = 1;
			goto cleanup;
		}
		evo_app_free (app);
		app = NULL;
		result = 0;
		goto cleanup;
	}

	if (argc < 2) {
		gchar *help = g_option_context_get_help (context, TRUE, NULL);

		g_print ("%s", help);
		g_free (help);
		result = 1;
		goto cleanup;
	}

	command = argv[1];

	app = evo_app_new (&error);
	if (app == NULL) {
		g_printerr ("evomail: %s\n", error->message);
		g_clear_error (&error);
		result = 1;
		goto cleanup;
	}

	if (!evo_config_load ()) {
		/* Config file not found or invalid - continue without defaults */
	}

	if (!evo_load_accounts (app, &error)) {
		g_printerr ("evomail: %s\n", error->message);
		g_clear_error (&error);
		result = 1;
		goto cleanup;
	}

	if (opt_folder == NULL) {
		opt_folder = (gchar *) evo_config_get_default_folder ();
	}

	if (g_strcmp0 (command, "version") == 0) {
		g_print ("evomail 0.1.0\n");
	} else if (g_strcmp0 (command, "accounts") == 0) {
		GPtrArray *accounts = evo_app_get_accounts (app);

		if (opt_json) {
			GString *out = g_string_new ("[\n");
			for (guint i = 0; i < accounts->len; i++) {
				EvoAccount *acct = g_ptr_array_index (accounts, i);
				g_string_append (out, "  {\"uid\":");
				json_string (out, evo_account_get_uid (acct));
				g_string_append (out, ",\"name\":");
				json_string (out, evo_account_get_name (acct));
				g_string_append (out, ",\"email\":");
				json_string (out, evo_account_get_email (acct));
				g_string_append (out, ",\"backend\":");
				json_string (out, evo_account_get_backend (acct));
				g_string_append (out, ",\"host\":");
				json_string (out, evo_account_get_host (acct));
				g_string_append_printf (out, ",\"port\":%d",
							evo_account_get_port (acct));
				g_string_append_printf (out, ",\"local\":%s",
							evo_account_is_local (acct) ?
							"true" : "false");
				g_string_append (out, ",\"smtp_uid\":");
				json_string (out, evo_account_get_smtp_uid (acct));
				g_string_append_c (out, '}');
				if (i + 1 < accounts->len)
					g_string_append_c (out, ',');
				g_string_append_c (out, '\n');
			}
			g_string_append (out, "]\n");
			g_print ("%s", out->str);
			g_string_free (out, TRUE);
		} else {
			for (guint i = 0; i < accounts->len; i++) {
				EvoAccount *acct = g_ptr_array_index (accounts, i);
				g_print ("* %s",
					 evo_account_get_email (acct) != NULL ?
					 evo_account_get_email (acct) :
					 evo_account_get_uid (acct));
				if (evo_account_get_name (acct) != NULL &&
				    g_strcmp0 (evo_account_get_name (acct),
					       evo_account_get_email (acct)) != 0)
					g_print (" (%s)", evo_account_get_name (acct));
				g_print ("\n");
				g_print ("  store: %s",
					 evo_account_get_backend (acct));
				if (evo_account_get_host (acct) != NULL &&
				    *evo_account_get_host (acct) != '\0')
					g_print (" %s:%d",
						 evo_account_get_host (acct),
						 evo_account_get_port (acct));
				g_print (" [%s]\n", evo_account_get_uid (acct));
				if (evo_account_get_smtp_uid (acct) != NULL)
					g_print ("  transport: smtp [%s]\n",
						 evo_account_get_smtp_uid (acct));
				else if (!evo_account_is_local (acct))
					g_print ("  transport: none configured\n");
			}
		}
	} else if (g_strcmp0 (command, "folders") == 0) {
		if (!cmd_folders (app, &error))
			result = 1;
	} else if (g_strcmp0 (command, "list") == 0) {
		if (!cmd_list (app, FALSE, NULL, &error))
			result = 1;
	} else if (g_strcmp0 (command, "search") == 0) {
		if (argc < 3) {
			g_set_error_literal (&error, G_IO_ERROR,
					     G_IO_ERROR_INVALID_ARGUMENT,
					     "Search needs a query.");
			result = 1;
		} else if (!cmd_list (app, TRUE, &argv[2], &error)) {
			result = 1;
		}
	} else if (g_strcmp0 (command, "read") == 0) {
		if (argc < 3) {
			g_set_error_literal (&error, G_IO_ERROR,
					     G_IO_ERROR_INVALID_ARGUMENT,
					     "Read needs a message UID.");
			result = 1;
		} else if (!cmd_read (app, argv[2], &error)) {
			result = 1;
		}
	} else if (g_strcmp0 (command, "mark") == 0) {
		if (!cmd_mark (app, &argv[2], argc - 2, &error))
			result = 1;
	} else if (g_strcmp0 (command, "send") == 0) {
		if (!cmd_send (app, &error))
			result = 1;
	} else if (g_strcmp0 (command, "reply") == 0) {
		if (argc < 3) {
			g_set_error_literal (&error, G_IO_ERROR,
					     G_IO_ERROR_INVALID_ARGUMENT,
					     "Reply needs a message UID.");
			result = 1;
		} else if (!cmd_reply (app, argv[2], &error)) {
			result = 1;
		}
	} else if (g_strcmp0 (command, "forward") == 0) {
		if (argc < 3) {
			g_set_error_literal (&error, G_IO_ERROR,
					     G_IO_ERROR_INVALID_ARGUMENT,
					     "Forward needs a message UID.");
			result = 1;
		} else if (!cmd_forward (app, argv[2], &error)) {
			result = 1;
		}
	} else if (g_strcmp0 (command, "forward-as-attachment") == 0) {
		if (argc < 3) {
			g_set_error_literal (&error, G_IO_ERROR,
					     G_IO_ERROR_INVALID_ARGUMENT,
					     "Forward-as-attachment needs a message UID.");
			result = 1;
		} else if (!cmd_forward_as_attachment (app, argv[2], &error)) {
			result = 1;
		}
	} else if (g_strcmp0 (command, "export") == 0) {
		if (argc < 3) {
			g_set_error_literal (&error, G_IO_ERROR,
					     G_IO_ERROR_INVALID_ARGUMENT,
					     "Export needs a message UID.");
			result = 1;
		} else if (!cmd_export (app, argv[2], &error)) {
			result = 1;
		}
	} else if (g_strcmp0 (command, "delete") == 0) {
		if (!cmd_delete (app, &argv[2], argc - 2, &error))
			result = 1;
	} else if (g_strcmp0 (command, "move") == 0) {
		if (!cmd_move (app, &argv[2], argc - 2, &error))
			result = 1;
	} else if (g_strcmp0 (command, "copy") == 0) {
		if (!cmd_copy (app, &argv[2], argc - 2, &error))
			result = 1;
	} else if (g_strcmp0 (command, "config") == 0) {
		if (!cmd_config (app, &error))
			result = 1;
	} else {
		g_printerr ("evomail: unknown command “%s”. Run with --help.\n",
			    command);
		result = 1;
	}

	if (error != NULL) {
		g_printerr ("evomail: %s\n", error->message);
		g_clear_error (&error);
	}

	evo_app_free (app);

cleanup:
	g_option_context_free (context);
	g_strfreev (opt_attach);
	opt_attach = NULL;
	return result;
}