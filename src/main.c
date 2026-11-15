#include "evomail.h"
#include <string.h>
#include <unistd.h>

static gchar *opt_account = NULL;
static gchar *opt_folder = NULL;
static gboolean opt_json = FALSE;
static gint opt_limit = 20;
static gboolean opt_refresh = TRUE;
static gboolean opt_unread = FALSE;
static gboolean opt_html = FALSE;
static gchar *opt_save_attachments = NULL;
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
	{ NULL }
};

static void
app_clear (App *app)
{
	if (app->accounts != NULL)
		g_ptr_array_unref (app->accounts);
	if (app->session != NULL)
		g_object_unref (app->session);
	if (app->registry != NULL)
		g_object_unref (app->registry);
	memset (app, 0, sizeof (*app));
}

static gboolean
app_init (App *app, GError **error)
{
	memset (app, 0, sizeof (*app));

	app->registry = e_source_registry_new_sync (NULL, error);
	if (app->registry == NULL)
		return FALSE;

	app->session = evo_session_new (app->registry);
	return evo_load_accounts (app, error);
}

static void
print_accounts (App *app)
{
	if (opt_json) {
		GString *out = g_string_new ("[\n");

		for (guint i = 0; i < app->accounts->len; i++) {
			Account *account = g_ptr_array_index (app->accounts, i);

			g_string_append (out, "  {\"uid\":");
			json_string (out, account->uid);
			g_string_append (out, ",\"name\":");
			json_string (out, account->name);
			g_string_append (out, ",\"email\":");
			json_string (out, account->email);
			g_string_append (out, ",\"backend\":");
			json_string (out, account->backend);
			g_string_append (out, ",\"host\":");
			json_string (out, account->host);
			g_string_append_printf (out, ",\"port\":%d", account->port);
			g_string_append_printf (out, ",\"local\":%s",
						account->local ? "true" : "false");
			g_string_append (out, ",\"smtp_uid\":");
			json_string (out, account->smtp_uid);
			g_string_append_c (out, '}');
			if (i + 1 < app->accounts->len)
				g_string_append_c (out, ',');
			g_string_append_c (out, '\n');
		}

		g_string_append (out, "]\n");
		g_print ("%s", out->str);
		g_string_free (out, TRUE);
		return;
	}

	if (app->accounts->len == 0) {
		g_print ("No Evolution mail accounts found.\n");
		return;
	}

	for (guint i = 0; i < app->accounts->len; i++) {
		Account *account = g_ptr_array_index (app->accounts, i);

		g_print ("* %s", account->email != NULL ?
			 account->email : account->uid);
		if (account->name != NULL &&
		    g_strcmp0 (account->name, account->email) != 0)
			g_print (" (%s)", account->name);
		g_print ("\n");
		g_print ("  store: %s", account->backend);
		if (account->host != NULL && *account->host != '\0')
			g_print (" %s:%d", account->host, account->port);
		g_print (" [%s]\n", account->uid);
		if (account->smtp_uid != NULL)
			g_print ("  transport: smtp [%s]\n", account->smtp_uid);
		else if (!account->local)
			g_print ("  transport: none configured\n");
	}
}

static void
print_folder_tree (CamelFolderInfo *info, guint depth, gboolean *first)
{
	for (CamelFolderInfo *i = info; i != NULL; i = i->next) {
		if (opt_json) {
			GString *out = g_string_new (NULL);

			if (!*first)
				g_print (",\n");
			*first = FALSE;
			json_string (out, i->full_name);
			g_print ("  {\"name\":%s}", out->str);
			g_string_free (out, TRUE);
		} else {
			g_print ("%*s%s", depth * 2, "", i->full_name);
			if (i->total >= 0)
				g_print ("  (%d messages, %d unread)",
					 i->total, i->unread);
			g_print ("\n");
		}

		if (i->child != NULL)
			print_folder_tree (i->child, depth + 1, first);
	}
}

static gboolean
cmd_folders (App *app, GError **error)
{
	Account *account = evo_find_account (app, opt_account, error);
	CamelStore *store;
	CamelFolderInfo *info;
	gboolean first = TRUE;

	if (account == NULL)
		return FALSE;

	store = evo_open_store (app, account, error);
	if (store == NULL)
		return FALSE;

	info = camel_store_get_folder_info_sync (
		store, NULL,
		CAMEL_STORE_FOLDER_INFO_RECURSIVE | CAMEL_STORE_FOLDER_INFO_REFRESH,
		NULL, error);
	if (info == NULL) {
		g_object_unref (store);
		return FALSE;
	}

	if (opt_json) {
		g_print ("[\n");
		print_folder_tree (info, 0, &first);
		g_print ("\n]\n");
	} else {
		print_folder_tree (info, 0, &first);
	}

	camel_folder_info_free (info);
	g_object_unref (store);
	return TRUE;
}

static void
filter_unread (GPtrArray *messages)
{
	for (guint i = messages->len; i > 0; i--) {
		MsgSummary *msg = g_ptr_array_index (messages, i - 1);

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
cmd_list (App *app, gboolean search, gchar **terms, GError **error)
{
	Account *account = evo_find_account (app, opt_account, error);
	CamelStore *store;
	CamelFolder *folder;
	GPtrArray *messages;
	gchar *query = NULL;

	if (account == NULL)
		return FALSE;

	store = evo_open_store (app, account, error);
	if (store == NULL)
		return FALSE;

	folder = evo_open_folder (store, opt_folder, FALSE, error);
	if (folder == NULL) {
		g_object_unref (store);
		return FALSE;
	}

	if (search) {
		query = g_strjoinv (" ", terms);
		messages = evo_search_messages (folder, query ? query : "",
						opt_refresh, error);
		g_free (query);
	} else {
		messages = evo_list_messages (folder, opt_refresh, error);
	}

	if (messages == NULL) {
		g_object_unref (folder);
		g_object_unref (store);
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
	g_object_unref (folder);
	g_object_unref (store);
	return TRUE;
}

static gboolean
cmd_read (App *app, gchar *uid, GError **error)
{
	Account *account = evo_find_account (app, opt_account, error);
	CamelStore *store;
	CamelFolder *folder;
	ReadMessage *message;
	GError *local_error = NULL;

	if (account == NULL)
		return FALSE;

	store = evo_open_store (app, account, error);
	if (store == NULL)
		return FALSE;

	folder = evo_open_folder (store, opt_folder, FALSE, error);
	if (folder == NULL) {
		g_object_unref (store);
		return FALSE;
	}

	if (!camel_folder_refresh_info_sync (folder, NULL, &local_error)) {
		g_printerr ("evomail: warning: folder refresh failed: %s\n",
			    local_error->message);
		g_clear_error (&local_error);
	}

	message = evo_read_message (folder, uid, opt_save_attachments, error);
	if (message == NULL) {
		g_object_unref (folder);
		g_object_unref (store);
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
		gchar *mark_uids[] = { uid };
		if (!evo_mark_messages (folder, mark_uids, 1,
				     CAMEL_MESSAGE_SEEN, CAMEL_MESSAGE_SEEN,
				     NULL)) {
			g_printerr ("evomail: could not mark message as seen\n");
		}
	}

	read_message_free (message);
	g_object_unref (folder);
	g_object_unref (store);
	return TRUE;
}

static gboolean
cmd_mark (App *app, gchar **uids, gint n_uids, GError **error)
{
	Account *account = evo_find_account (app, opt_account, error);
	CamelStore *store;
	CamelFolder *folder;
	guint32 mask = 0;
	guint32 set = 0;
	gboolean success;
	GError *local_error = NULL;

	if (account == NULL)
		return FALSE;

	if (n_uids == 0) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
				     "No message UIDs given.");
		return FALSE;
	}

	if (opt_mark_read) {
		mask |= CAMEL_MESSAGE_SEEN;
		set |= CAMEL_MESSAGE_SEEN;
	}
	if (opt_unread) {
		mask |= CAMEL_MESSAGE_SEEN;
		set &= ~CAMEL_MESSAGE_SEEN;
	}
	if (opt_flag) {
		mask |= CAMEL_MESSAGE_FLAGGED;
		set |= CAMEL_MESSAGE_FLAGGED;
	}
	if (opt_unflag) {
		mask |= CAMEL_MESSAGE_FLAGGED;
		set &= ~CAMEL_MESSAGE_FLAGGED;
	}
	if (opt_delete) {
		mask |= CAMEL_MESSAGE_DELETED;
		set |= CAMEL_MESSAGE_DELETED;
	}
	if (opt_undelete) {
		mask |= CAMEL_MESSAGE_DELETED;
		set &= ~CAMEL_MESSAGE_DELETED;
	}

	if (mask == 0) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
				     "No flag change given. Use --read, --unread, "
				     "--flag, --unflag, --delete, or --undelete.");
		return FALSE;
	}

	store = evo_open_store (app, account, error);
	if (store == NULL)
		return FALSE;

	folder = evo_open_folder (store, opt_folder, TRUE, error);
	if (folder == NULL) {
		g_object_unref (store);
		return FALSE;
	}

	/* Ensure the folder is connected and synced with the server
	 * before making flag changes. */
	if (!camel_folder_refresh_info_sync (folder, NULL, &local_error)) {
		g_printerr ("evomail: warning: folder refresh failed: %s\n",
			    local_error->message);
		g_clear_error (&local_error);
	}

	if (!camel_offline_store_get_online (CAMEL_OFFLINE_STORE (store))) {
		if (!camel_offline_store_set_online_sync (
			CAMEL_OFFLINE_STORE (store), TRUE,
			NULL, error)) {
			g_object_unref (folder);
			g_object_unref (store);
			return FALSE;
		}
	}

	success = evo_mark_messages (folder, uids, n_uids, mask, set, error);
	if (success && opt_expunge)
		success = camel_folder_expunge_sync (folder, NULL, error);

	if (success) {
		if (opt_json)
			g_print ("{\"updated\":%d}\n", n_uids);
		else
			g_print ("Updated %d message%s.\n", n_uids,
				 n_uids == 1 ? "" : "s");
	}

	g_object_unref (folder);
	g_object_unref (store);
	return success;
}

static gboolean
cmd_send (App *app, GError **error)
{
	Account *account = evo_find_account (app, opt_account, error);
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

int
main (int argc, char **argv)
{
	GOptionContext *context;
	GError *error = NULL;
	App app;
	const gchar *command;
	gint result = 0;

	context = g_option_context_new ("COMMAND [ARGS] — a CLI for Evolution mail");
	g_option_context_add_main_entries (context, entries, NULL);
	g_option_context_set_summary (context,
		"Commands:\n"
		"  accounts                         List Evolution mail accounts\n"
		"  folders                          List folders for an account\n"
		"  list                             List messages (default: newest 20)\n"
		"  read UID                         Print one message\n"
		"  search QUERY                     Search a folder\n"
		"  mark UID...                      Change message flags\n"
		"  send                             Compose and send a message\n"
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

	if (argc < 2) {
		gchar *help = g_option_context_get_help (context, TRUE, NULL);

		g_print ("%s", help);
		g_free (help);
		result = 1;
		goto cleanup;
	}

	command = argv[1];

	if (!app_init (&app, &error)) {
		g_printerr ("evomail: %s\n", error->message);
		g_clear_error (&error);
		result = 1;
		goto cleanup;
	}

	if (g_strcmp0 (command, "version") == 0) {
		g_print ("evomail 0.1.0\n");
	} else if (g_strcmp0 (command, "accounts") == 0) {
		print_accounts (&app);
	} else if (g_strcmp0 (command, "folders") == 0) {
		if (!cmd_folders (&app, &error))
			result = 1;
	} else if (g_strcmp0 (command, "list") == 0) {
		if (!cmd_list (&app, FALSE, NULL, &error))
			result = 1;
	} else if (g_strcmp0 (command, "search") == 0) {
		if (argc < 3) {
			g_set_error_literal (&error, G_IO_ERROR,
					     G_IO_ERROR_INVALID_ARGUMENT,
					     "Search needs a query.");
			result = 1;
		} else if (!cmd_list (&app, TRUE, &argv[2], &error)) {
			result = 1;
		}
	} else if (g_strcmp0 (command, "read") == 0) {
		if (argc < 3) {
			g_set_error_literal (&error, G_IO_ERROR,
					     G_IO_ERROR_INVALID_ARGUMENT,
					     "Read needs a message UID.");
			result = 1;
		} else if (!cmd_read (&app, argv[2], &error)) {
			result = 1;
		}
	} else if (g_strcmp0 (command, "mark") == 0) {
		if (!cmd_mark (&app, &argv[2], argc - 2, &error))
			result = 1;
	} else if (g_strcmp0 (command, "send") == 0) {
		if (!cmd_send (&app, &error))
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

	app_clear (&app);

cleanup:
	g_option_context_free (context);
	g_strfreev (opt_attach);
	opt_attach = NULL;
	return result;
}
