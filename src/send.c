#include "evomail.h"
#include "evoapp.h"

#include <camel/camel.h>
#include <libedataserver/libedataserver.h>
#include <string.h>

extern void json_string (GString *out, const gchar *value);

static CamelInternetAddress *
parse_address (const gchar *raw, const gchar *kind, GError **error)
{
	CamelInternetAddress *address = camel_internet_address_new ();

	if (raw != NULL && *raw != '\0') {
		if (camel_address_decode (CAMEL_ADDRESS (address), raw) < 0 ||
		    camel_address_length (CAMEL_ADDRESS (address)) == 0) {
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
				     "Invalid %s address: %s", kind, raw);
			g_object_unref (address);
			return NULL;
		}
	}

	return address;
}

static CamelMimePart *
make_text_part (const gchar *text, const gchar *mime_type)
{
	CamelMimePart *part = camel_mime_part_new ();
	gchar *type = g_strdup_printf ("%s; charset=UTF-8", mime_type);
	const gchar *content = (text != NULL && *text != '\0') ? text : "\n";

	camel_mime_part_set_content (part, content, strlen (content), type);
	camel_mime_part_set_encoding (part, CAMEL_TRANSFER_ENCODING_QUOTEDPRINTABLE);
	g_free (type);

	return part;
}

static gboolean
save_to_sent (EvoApp *app, EvoAccount *account, CamelMimeMessage *message,
	      GError **error)
{
	EvoStore *store;
	EvoFolder *sent;
	gchar *sent_name;
	gchar *appended_uid = NULL;
	gboolean success;

	store = evo_store_open (app, account, error);
	if (store == NULL)
		return FALSE;

	sent_name = evo_find_special_folder (store, CAMEL_FOLDER_TYPE_SENT, NULL);
	if (sent_name == NULL)
		sent_name = g_strdup ("Sent");

	sent = evo_folder_open (store, sent_name, error);
	g_free (sent_name);
	if (sent == NULL) {
		evo_store_close (store);
		return FALSE;
	}

	success = camel_folder_append_message_sync (
		evo_folder_get_camel_folder (sent), message, NULL,
		&appended_uid, NULL, error);
	if (success)
		camel_folder_synchronize_sync (
			evo_folder_get_camel_folder (sent),
			FALSE, NULL, NULL);

	g_free (appended_uid);
	evo_folder_close (sent);
	evo_store_close (store);
	return success;
}

gboolean
evo_save_to_drafts (EvoApp *app, EvoAccount *account, CamelMimeMessage *message,
	      GError **error)
{
	EvoStore *store;
	EvoFolder *drafts;
	gchar *drafts_name;
	gchar *appended_uid = NULL;
	gboolean success;

	store = evo_store_open (app, account, error);
	if (store == NULL)
		return FALSE;

	drafts_name = evo_find_special_folder (store, CAMEL_FOLDER_TYPE_DRAFTS, NULL);
	if (drafts_name == NULL)
		drafts_name = g_strdup ("Drafts");

	drafts = evo_folder_open (store, drafts_name, error);
	g_free (drafts_name);
	if (drafts == NULL) {
		evo_store_close (store);
		return FALSE;
	}

	success = camel_folder_append_message_sync (
		evo_folder_get_camel_folder (drafts), message, NULL,
		&appended_uid, NULL, error);
	if (success)
		camel_folder_synchronize_sync (
			evo_folder_get_camel_folder (drafts),
			FALSE, NULL, NULL);

	g_free (appended_uid);
	evo_folder_close (drafts);
	evo_store_close (store);
	return success;
}

static gboolean
evo_send_message_raw (EvoApp *app, EvoAccount *account,
		      CamelMimeMessage *message,
		      const gchar *to, const gchar *subject,
		      gboolean dry_run, gboolean save_copy,
		      gboolean json, GError **error)
{
	CamelTransport *transport = NULL;
	CamelInternetAddress *envelope_from;
	CamelInternetAddress *envelope_to;
	gboolean sent;
	gboolean saved = FALSE;
	gboolean copy_saved = FALSE;

	if (dry_run) {
		GOutputStream *stream = g_memory_output_stream_new_resizable ();
		gssize size;

		size = camel_data_wrapper_write_to_output_stream_sync (
			CAMEL_DATA_WRAPPER (message), stream, NULL, NULL);
		g_output_stream_close (stream, NULL, NULL);

		if (json) {
			GString *out = g_string_new ("{");

			g_string_append (out, "\"dry_run\":true,\"from\":");
			json_string (out, account->email);
			g_string_append (out, ",\"to\":");
			json_string (out, to);
			g_string_append (out, ",\"subject\":");
			json_string (out, subject);
			g_string_append_printf (out, ",\"size\":%" G_GSSIZE_FORMAT,
						size > 0 ? size : 0);
			g_string_append (out, "}\n");
			g_print ("%s", out->str);
			g_string_free (out, TRUE);
		} else {
			g_print ("Dry run: message not sent.\n");
			g_print ("From: %s <%s>\n",
				 account->name != NULL ? account->name : "",
				 account->email != NULL ? account->email : "");
			g_print ("To: %s\n", to != NULL ? to : "");
			g_print ("Subject: %s\n", subject != NULL ? subject : "");
			g_print ("Size: %" G_GSSIZE_FORMAT " bytes\n",
				 size > 0 ? size : 0);
		}

		g_object_unref (stream);
		g_object_unref (message);
		return TRUE;
	}

	/* Look up the SMTP EDS source and configure the transport service */
	ESource *smtp_source = e_source_registry_ref_source (
		evo_app_get_registry (app), account->smtp_uid);
	if (smtp_source == NULL) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
			     "No Evolution source for SMTP transport %s.",
			     account->smtp_uid);
		g_object_unref (message);
		return FALSE;
	}

	transport = CAMEL_TRANSPORT (camel_session_add_service (
		evo_app_get_session (app),
		account->smtp_uid,
		"smtp",
		CAMEL_PROVIDER_TRANSPORT,
		error));
	if (transport == NULL) {
		g_object_unref (smtp_source);
		g_object_unref (message);
		return FALSE;
	}

	e_source_camel_configure_service (smtp_source, CAMEL_SERVICE (transport));
	g_object_unref (smtp_source);

	if (!camel_service_connect_sync (CAMEL_SERVICE (transport), NULL, error)) {
		g_printerr ("evomail: transport connect failed: %s\n",
			    error != NULL && *error != NULL ? (*error)->message : "unknown");
		g_object_unref (transport);
		g_object_unref (message);
		return FALSE;
	}

	envelope_from = camel_internet_address_new ();
	camel_internet_address_add (envelope_from,
				    account->name != NULL ? account->name : "",
				    account->email);
	envelope_to = camel_internet_address_new ();
	{
		CamelInternetAddress *to_list = camel_mime_message_get_recipients (
			message, CAMEL_RECIPIENT_TYPE_TO);
		CamelInternetAddress *cc_list = camel_mime_message_get_recipients (
			message, CAMEL_RECIPIENT_TYPE_CC);
		CamelInternetAddress *bcc_list = camel_mime_message_get_recipients (
			message, CAMEL_RECIPIENT_TYPE_BCC);

		if (to_list != NULL)
			camel_address_cat (CAMEL_ADDRESS (envelope_to),
					   CAMEL_ADDRESS (to_list));
		if (cc_list != NULL)
			camel_address_cat (CAMEL_ADDRESS (envelope_to),
					   CAMEL_ADDRESS (cc_list));
		if (bcc_list != NULL)
			camel_address_cat (CAMEL_ADDRESS (envelope_to),
					   CAMEL_ADDRESS (bcc_list));
	}

	sent = camel_transport_send_to_sync (transport, message,
					     CAMEL_ADDRESS (envelope_from),
					     CAMEL_ADDRESS (envelope_to),
					     &saved, NULL, error);

	g_object_unref (envelope_to);
	g_object_unref (envelope_from);
	g_object_unref (transport);

	if (!sent) {
		g_object_unref (message);
		return FALSE;
	}

	if (save_copy && !saved) {
		GError *local_error = NULL;

		copy_saved = save_to_sent (app, account, message, &local_error);
		if (!copy_saved) {
			g_printerr ("evomail: message sent, but saving a copy failed: %s\n",
				    local_error != NULL ? local_error->message :
				    "unknown error");
			g_clear_error (&local_error);
		}
	} else {
		copy_saved = saved;
	}

	if (json) {
		GString *out = g_string_new ("{");

		g_string_append (out, "\"sent\":true,\"to\":");
		json_string (out, to);
		g_string_append (out, ",\"subject\":");
		json_string (out, subject);
		g_string_append_printf (out, ",\"saved_copy\":%s",
					copy_saved ? "true" : "false");
		g_string_append (out, "}\n");
		g_print ("%s", out->str);
		g_string_free (out, TRUE);
	} else {
		g_print ("Sent message to %s\n",
			 to != NULL ? to : "");
		if (copy_saved)
			g_print ("Copy saved to the Sent folder.\n");
	}

	g_object_unref (message);
	return TRUE;
}

static CamelMimeMessage *
build_reply_message (EvoAccount *account, EvoMessage *original,
		     const gchar *body, gboolean reply_all,
		     GError **error)
{
	CamelMimeMessage *message = camel_mime_message_new ();
	CamelInternetAddress *from;
	CamelInternetAddress *to;
	CamelMimePart *body_part;
	CamelMultipart *multipart;
	CamelMimePart *text_part;
	CamelMimePart *quoted_part;
	gchar *subject;
	gchar *quoted_body;

	message = camel_mime_message_new ();

	from = camel_internet_address_new ();
	camel_internet_address_add (from,
				    account->name != NULL ? account->name : "",
				    account->email != NULL ? account->email : "");
	camel_mime_message_set_from (message, from);
	g_object_unref (from);

	to = camel_internet_address_new ();
	camel_internet_address_add (to, "", original->from != NULL ? original->from : "");
	camel_mime_message_set_recipients (message, CAMEL_RECIPIENT_TYPE_TO, to);
	g_object_unref (to);

	if (reply_all && original->cc != NULL && *original->cc != '\0') {
		CamelInternetAddress *cc_addr = camel_internet_address_new ();
		camel_address_decode (CAMEL_ADDRESS (cc_addr), original->cc);
		camel_mime_message_set_recipients (message, CAMEL_RECIPIENT_TYPE_CC, cc_addr);
		g_object_unref (cc_addr);
	}

	subject = g_strdup_printf ("Re: %s",
				   original->subject != NULL && g_str_has_prefix (original->subject, "Re: ") ?
				   original->subject : original->subject);
	if (original->subject != NULL && !g_str_has_prefix (original->subject, "Re: "))
	{
		g_free (subject);
		subject = g_strdup_printf ("Re: %s", original->subject);
	}
	camel_mime_message_set_subject (message, subject);
	g_free (subject);

	multipart = camel_multipart_new ();
	camel_data_wrapper_set_mime_type (CAMEL_DATA_WRAPPER (multipart), "multipart/mixed");
	camel_multipart_set_boundary (multipart, g_strdup_printf ("----=_ReplyPart_%u.%u",
								 (guint) time (NULL), g_random_int ()));

	text_part = make_text_part (body, "text/plain");
	camel_multipart_add_part (multipart, text_part);
	g_object_unref (text_part);

	quoted_body = g_strdup_printf ("\n\n---- Original Message ----\nFrom: %s\nDate: %s\nSubject: %s\n\n%s",
				       original->from != NULL ? original->from : "",
				       original->date != NULL ? original->date : "",
				       original->subject != NULL ? original->subject : "",
				       original->text != NULL ? original->text :
				       (original->html != NULL ? original->html : ""));

	quoted_part = make_text_part (quoted_body, "text/plain");
	camel_multipart_add_part (multipart, quoted_part);
	g_object_unref (quoted_part);

	body_part = camel_mime_part_new ();
	camel_medium_set_content (CAMEL_MEDIUM (body_part), CAMEL_DATA_WRAPPER (multipart));
	camel_mime_part_set_disposition (body_part, "inline");
	camel_medium_set_content (CAMEL_MEDIUM (message), CAMEL_DATA_WRAPPER (body_part));
	g_object_unref (body_part);
	g_object_unref (multipart);
	g_free (quoted_body);

	return message;
}

static CamelMimePart *make_attachment_part (const gchar *path, GError **error);

static CamelMimeMessage *
build_forward_message (EvoAccount *account, EvoMessage *original,
		       const gchar *to, const gchar *cc, const gchar *bcc,
		       const gchar *body, GPtrArray *attach_paths,
		       GError **error)
{
	CamelMimeMessage *message = camel_mime_message_new ();
	CamelInternetAddress *from;
	CamelInternetAddress *to_addr;
	CamelInternetAddress *cc_addr;
	CamelInternetAddress *bcc_addr;
	CamelMimePart *body_part;
	CamelMultipart *multipart;
	CamelMimePart *text_part;
	CamelMimePart *forward_part;
	gchar *subject;
	gchar *forward_body;
	gchar *original_text;

	message = camel_mime_message_new ();

	from = camel_internet_address_new ();
	camel_internet_address_add (from,
				    account->name != NULL ? account->name : "",
				    account->email != NULL ? account->email : "");
	camel_mime_message_set_from (message, from);
	g_object_unref (from);

	to_addr = parse_address (to, "To", error);
	if (to_addr == NULL)
		return NULL;
	cc_addr = parse_address (cc, "Cc", error);
	if (cc_addr == NULL) {
		g_object_unref (to_addr);
		return NULL;
	}
	bcc_addr = parse_address (bcc, "Bcc", error);
	if (bcc_addr == NULL) {
		g_object_unref (to_addr);
		g_object_unref (cc_addr);
		return NULL;
	}

	if (camel_address_length (CAMEL_ADDRESS (to_addr)) == 0 &&
	    camel_address_length (CAMEL_ADDRESS (cc_addr)) == 0 &&
	    camel_address_length (CAMEL_ADDRESS (bcc_addr)) == 0) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
				     "No recipients given. Use --to, --cc, or --bcc.");
		g_object_unref (bcc_addr);
		g_object_unref (cc_addr);
		g_object_unref (to_addr);
		return NULL;
	}

	if (camel_address_length (CAMEL_ADDRESS (to_addr)) > 0)
		camel_mime_message_set_recipients (message, CAMEL_RECIPIENT_TYPE_TO, to_addr);
	if (camel_address_length (CAMEL_ADDRESS (cc_addr)) > 0)
		camel_mime_message_set_recipients (message, CAMEL_RECIPIENT_TYPE_CC, cc_addr);
	if (camel_address_length (CAMEL_ADDRESS (bcc_addr)) > 0)
		camel_mime_message_set_recipients (message, CAMEL_RECIPIENT_TYPE_BCC, bcc_addr);

	subject = g_strdup_printf ("Fwd: %s",
				   original->subject != NULL && g_str_has_prefix (original->subject, "Fw: ") ?
				   original->subject : original->subject);
	if (original->subject != NULL && !g_str_has_prefix (original->subject, "Fw: "))
	{
		g_free (subject);
		subject = g_strdup_printf ("Fw: %s", original->subject);
	}
	camel_mime_message_set_subject (message, subject);
	g_free (subject);

	/* Build forwarded message body: user's comment first, then quoted original */
	original_text = original->text != NULL ? original->text :
		(original->html != NULL ? original->html : "");
	forward_body = g_strdup_printf ("\n\n------- Forwarded Message -------\nFrom: %s\nDate: %s\nSubject: %s\nTo: %s\n\n%s",
				      original->from != NULL ? original->from : "",
				      original->date != NULL ? original->date : "",
				      original->subject != NULL ? original->subject : "",
				      original->to != NULL ? original->to : "",
				      original_text != NULL ? original_text : "");

	multipart = camel_multipart_new ();
	camel_data_wrapper_set_mime_type (CAMEL_DATA_WRAPPER (multipart), "multipart/mixed");
	camel_multipart_set_boundary (multipart, g_strdup_printf ("----=_ForwardPart_%u.%u",
								 (guint) time (NULL), g_random_int ()));

	/* User's comment first */
	text_part = make_text_part (body != NULL ? body : "", "text/plain");
	camel_multipart_add_part (multipart, text_part);
	g_object_unref (text_part);

	/* Then the quoted original message */
	forward_part = make_text_part (forward_body, "text/plain");
	camel_multipart_add_part (multipart, forward_part);
	g_object_unref (forward_part);

	/* Add any attachments */
	if (attach_paths != NULL) {
		for (guint i = 0; i < attach_paths->len; i++) {
			const gchar *path = g_ptr_array_index (attach_paths, i);
			CamelMimePart *attachment = make_attachment_part (path, error);

			if (attachment == NULL) {
				g_object_unref (bcc_addr);
				g_object_unref (cc_addr);
				g_object_unref (to_addr);
				return NULL;
			}
			camel_multipart_add_part (multipart, attachment);
			g_object_unref (attachment);
		}
	}

	body_part = camel_mime_part_new ();
	camel_medium_set_content (CAMEL_MEDIUM (body_part), CAMEL_DATA_WRAPPER (multipart));
	camel_mime_part_set_disposition (body_part, "inline");
	camel_medium_set_content (CAMEL_MEDIUM (message), CAMEL_DATA_WRAPPER (body_part));
	g_object_unref (body_part);
	g_object_unref (multipart);
	g_object_unref (bcc_addr);
	g_object_unref (cc_addr);
	g_object_unref (to_addr);
	g_free (forward_body);

	return message;
}

static CamelMimePart *
make_attachment_part (const gchar *path, GError **error)
{
	CamelMimePart *part;
	gchar *contents = NULL;
	gsize length = 0;
	gchar *content_type;
	gchar *mime_type;
	gchar *basename;
	gboolean uncertain = FALSE;

	if (!g_file_get_contents (path, &contents, &length, error))
		return NULL;

	basename = g_path_get_basename (path);
	content_type = g_content_type_guess (basename, (const guchar *) contents,
					     length, &uncertain);
	mime_type = content_type != NULL ?
		g_content_type_get_mime_type (content_type) : NULL;

	part = camel_mime_part_new ();
	camel_mime_part_set_content (part, contents, length,
				     mime_type != NULL ? mime_type :
				     "application/octet-stream");
	camel_mime_part_set_filename (part, basename);
	camel_mime_part_set_disposition (part, "attachment");
	camel_mime_part_set_encoding (part, CAMEL_TRANSFER_ENCODING_BASE64);

	g_free (mime_type);
	g_free (content_type);
	g_free (basename);
	g_free (contents);

	return part;
}

static CamelMimeMessage *
build_message (EvoAccount *account, const gchar *to, const gchar *cc,
	       const gchar *bcc, const gchar *subject, const gchar *body,
	       gboolean html, GPtrArray *attach_paths, GError **error)
{
	CamelMimeMessage *message;
	CamelInternetAddress *from;
	CamelInternetAddress *to_addr;
	CamelInternetAddress *cc_addr;
	CamelInternetAddress *bcc_addr;
	CamelMimePart *body_part;
	CamelMultipart *multipart = NULL;
	gchar *message_id;

	message = camel_mime_message_new ();

	from = camel_internet_address_new ();
	camel_internet_address_add (from,
				    account->name != NULL ? account->name : "",
				    account->email != NULL ? account->email : "");
	camel_mime_message_set_from (message, from);
	g_object_unref (from);

	if (account->email == NULL || *account->email == '\0') {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
				     "Account has no email address.");
		g_object_unref (message);
		return NULL;
	}

	to_addr = parse_address (to, "To", error);
	if (to_addr == NULL) {
		g_object_unref (message);
		return NULL;
	}
	cc_addr = parse_address (cc, "Cc", error);
	if (cc_addr == NULL) {
		g_object_unref (to_addr);
		g_object_unref (message);
		return NULL;
	}
	bcc_addr = parse_address (bcc, "Bcc", error);
	if (bcc_addr == NULL) {
		g_object_unref (cc_addr);
		g_object_unref (to_addr);
		g_object_unref (message);
		return NULL;
	}

	if (camel_address_length (CAMEL_ADDRESS (to_addr)) == 0 &&
	    camel_address_length (CAMEL_ADDRESS (cc_addr)) == 0 &&
	    camel_address_length (CAMEL_ADDRESS (bcc_addr)) == 0) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
				     "No recipients given. Use --to, --cc, or --bcc.");
		g_object_unref (bcc_addr);
		g_object_unref (cc_addr);
		g_object_unref (to_addr);
		g_object_unref (message);
		return NULL;
	}

	if (camel_address_length (CAMEL_ADDRESS (to_addr)) > 0)
		camel_mime_message_set_recipients (message, CAMEL_RECIPIENT_TYPE_TO, to_addr);
	if (camel_address_length (CAMEL_ADDRESS (cc_addr)) > 0)
		camel_mime_message_set_recipients (message, CAMEL_RECIPIENT_TYPE_CC, cc_addr);
	if (camel_address_length (CAMEL_ADDRESS (bcc_addr)) > 0)
		camel_mime_message_set_recipients (message, CAMEL_RECIPIENT_TYPE_BCC, bcc_addr);

	camel_mime_message_set_subject (message, subject != NULL ? subject : "");
	camel_mime_message_set_date (message, time (NULL), 0);

	message_id = g_strdup_printf ("<%" G_GINT64_FORMAT ".%u.%s>",
				      (gint64) time (NULL), g_random_int (),
				      g_get_host_name ());
	camel_mime_message_set_message_id (message, message_id);
	g_free (message_id);

	if (html) {
		CamelMultipart *alternative = camel_multipart_new ();
		CamelMimePart *text_part = make_text_part (body, "text/plain");
		CamelMimePart *html_part = make_text_part (body, "text/html");
		gchar *alt_boundary = g_strdup_printf ("----=_AltPart_%u.%u",
						       (guint) time (NULL), g_random_int ());

		camel_data_wrapper_set_mime_type (CAMEL_DATA_WRAPPER (alternative),
						  "multipart/alternative");
		camel_multipart_set_boundary (alternative, alt_boundary);
		camel_multipart_add_part (alternative, text_part);
		camel_multipart_add_part (alternative, html_part);

		body_part = camel_mime_part_new ();
		camel_medium_set_content (CAMEL_MEDIUM (body_part),
					  CAMEL_DATA_WRAPPER (alternative));

		g_free (alt_boundary);
		g_object_unref (text_part);
		g_object_unref (html_part);
		g_object_unref (alternative);
	} else {
		body_part = make_text_part (body, "text/plain");
	}

	if (attach_paths != NULL && attach_paths->len > 0) {
		multipart = camel_multipart_new ();
		gchar *mix_boundary = g_strdup_printf ("----=_MixedPart_%u.%u",
						       (guint) time (NULL), g_random_int ());

		camel_data_wrapper_set_mime_type (CAMEL_DATA_WRAPPER (multipart),
						  "multipart/mixed");
		camel_multipart_set_boundary (multipart, mix_boundary);
		camel_multipart_add_part (multipart, body_part);

		for (guint i = 0; i < attach_paths->len; i++) {
			const gchar *path = g_ptr_array_index (attach_paths, i);
			CamelMimePart *attachment = make_attachment_part (path, error);

			if (attachment == NULL) {
				g_free (mix_boundary);
				g_object_unref (body_part);
				g_object_unref (multipart);
				g_object_unref (bcc_addr);
				g_object_unref (cc_addr);
				g_object_unref (to_addr);
				g_object_unref (message);
				return NULL;
			}
			camel_multipart_add_part (multipart, attachment);
			g_object_unref (attachment);
		}

		g_free (mix_boundary);
		camel_medium_set_content (CAMEL_MEDIUM (message),
					  CAMEL_DATA_WRAPPER (multipart));
		g_object_unref (multipart);
	} else {
		camel_medium_set_content (CAMEL_MEDIUM (message),
					  CAMEL_DATA_WRAPPER (body_part));
	}

	g_object_unref (body_part);
	g_object_unref (bcc_addr);
	g_object_unref (cc_addr);
	g_object_unref (to_addr);

	return message;
}

gboolean
evo_send (EvoApp *app, EvoAccount *account,
	  const gchar *to, const gchar *cc, const gchar *bcc,
	  const gchar *subject, const gchar *body, gboolean html,
	  GPtrArray *attach_paths,
	  gboolean dry_run, gboolean save_copy, gboolean json,
	  gboolean save_to_drafts,
	  GError **error)
{
	CamelMimeMessage *message = build_message (account, to, cc, bcc,
						   subject, body, html,
						   attach_paths, error);
	if (message == NULL)
		return FALSE;

	if (save_to_drafts)
		return evo_save_to_drafts (app, account, message, error);

	return evo_send_message_raw (app, account, message,
				     to != NULL ? to : "",
				     subject != NULL ? subject : "",
				     dry_run, save_copy, json, error);
}

gboolean
evo_send_reply (EvoApp *app, EvoAccount *account, EvoMessage *original,
		const gchar *body, gboolean html, GPtrArray *attach_paths,
		gboolean dry_run, gboolean save_copy, gboolean json,
		gboolean reply_all,
		GError **error)
{
	CamelMimeMessage *message = build_reply_message (account, original, body,
							 reply_all, error);
	if (message == NULL)
		return FALSE;

	return evo_send_message_raw (app, account, message,
				     original->from != NULL ? original->from : "",
				     original->subject != NULL ? original->subject : "",
				     dry_run, save_copy, json, error);
}

gboolean
evo_send_forward (EvoApp *app, EvoAccount *account, EvoMessage *original,
		  const gchar *to, const gchar *cc, const gchar *bcc,
		  const gchar *body, gboolean html, GPtrArray *attach_paths,
		  gboolean dry_run, gboolean save_copy, gboolean json,
		  GError **error)
{
	CamelMimeMessage *message = build_forward_message (account, original,
							   to, cc, bcc,
							   body, attach_paths,
							   error);
	if (message == NULL)
		return FALSE;

	return evo_send_message_raw (app, account, message,
				     to != NULL ? to : "",
				     original->subject != NULL ? original->subject : "",
				     dry_run, save_copy, json, error);
}

gboolean
evo_send_forward_as_attachment (EvoApp *app, EvoAccount *account,
				EvoMessage *original, const gchar *to,
				const gchar *cc, const gchar *bcc,
				const gchar *body, gboolean html,
				GPtrArray *attach_paths,
				gboolean dry_run, gboolean save_copy,
				gboolean json, GError **error)
{
	CamelMimeMessage *message = camel_mime_message_new ();
	CamelInternetAddress *from;
	CamelInternetAddress *to_recipient;
	CamelMimePart *body_part;
	CamelMultipart *multipart;
	CamelMimePart *text_part;
	CamelMimePart *forward_part;
	gchar *subject;
	GOutputStream *stream;
	CamelMimeMessage *orig_msg;
	gchar *orig_str;
	CamelInternetAddress *to_addr;
	CamelInternetAddress *cc_addr;
	CamelInternetAddress *bcc_addr;

	message = camel_mime_message_new ();

	from = camel_internet_address_new ();
	camel_internet_address_add (from,
				    account->name != NULL ? account->name : "",
				    account->email != NULL ? account->email : "");
	camel_mime_message_set_from (message, from);
	g_object_unref (from);

	to_recipient = camel_internet_address_new ();
	camel_internet_address_add (to_recipient, "", original->from != NULL ? original->from : "");
	camel_mime_message_set_recipients (message, CAMEL_RECIPIENT_TYPE_TO, to_recipient);
	g_object_unref (to_recipient);

	to_addr = parse_address (to, "To", error);
	if (to_addr == NULL)
		return FALSE;
	cc_addr = parse_address (cc, "Cc", error);
	if (cc_addr == NULL) {
		g_object_unref (to_addr);
		return FALSE;
	}
	bcc_addr = parse_address (bcc, "Bcc", error);
	if (bcc_addr == NULL) {
		g_object_unref (to_addr);
		g_object_unref (cc_addr);
		return FALSE;
	}

	if (camel_address_length (CAMEL_ADDRESS (to_addr)) > 0)
		camel_mime_message_set_recipients (message, CAMEL_RECIPIENT_TYPE_TO, to_addr);
	if (camel_address_length (CAMEL_ADDRESS (cc_addr)) > 0)
		camel_mime_message_set_recipients (message, CAMEL_RECIPIENT_TYPE_CC, cc_addr);
	if (camel_address_length (CAMEL_ADDRESS (bcc_addr)) > 0)
		camel_mime_message_set_recipients (message, CAMEL_RECIPIENT_TYPE_BCC, bcc_addr);

	subject = g_strdup_printf ("Fwd: %s",
				   original->subject != NULL && g_str_has_prefix (original->subject, "Fw: ") ?
				   original->subject : original->subject);
	if (original->subject != NULL && !g_str_has_prefix (original->subject, "Fw: "))
	{
		g_free (subject);
		subject = g_strdup_printf ("Fw: %s", original->subject);
	}
	camel_mime_message_set_subject (message, subject);
	g_free (subject);

	orig_msg = camel_mime_message_new ();
	{
		CamelInternetAddress *orig_from = camel_internet_address_new ();
		camel_internet_address_add (orig_from,
					    "",
					    original->from != NULL ? original->from : "");
		camel_mime_message_set_from (orig_msg, orig_from);
		g_object_unref (orig_from);
	}
	camel_mime_message_set_subject (orig_msg, original->subject ? original->subject : "");
	camel_mime_message_set_date (orig_msg, time (NULL), 0);
	if (original->message_id)
		camel_mime_message_set_message_id (orig_msg, original->message_id);

	/* Add body content to orig_msg so it can be serialized */
	{
		CamelMimePart *orig_body = camel_mime_part_new ();
		const gchar *orig_text = original->text != NULL ? original->text :
			(original->html != NULL ? original->html : "");
		camel_mime_part_set_content (orig_body, orig_text, strlen (orig_text),
					     "text/plain; charset=UTF-8");
		camel_mime_part_set_encoding (orig_body, CAMEL_TRANSFER_ENCODING_QUOTEDPRINTABLE);
		camel_medium_set_content (CAMEL_MEDIUM (orig_msg),
					  CAMEL_DATA_WRAPPER (orig_body));
		g_object_unref (orig_body);
	}

	stream = g_memory_output_stream_new_resizable ();
	camel_data_wrapper_write_to_output_stream_sync (
		CAMEL_DATA_WRAPPER (orig_msg), stream, NULL, NULL);
	g_output_stream_close (stream, NULL, NULL);
	orig_str = (const gchar *) g_memory_output_stream_get_data (G_MEMORY_OUTPUT_STREAM (stream));
	orig_str = g_strdup (orig_str);
	g_object_unref (stream);
	g_object_unref (orig_msg);

	multipart = camel_multipart_new ();
	camel_data_wrapper_set_mime_type (CAMEL_DATA_WRAPPER (multipart), "multipart/mixed");
	camel_multipart_set_boundary (multipart, g_strdup_printf ("----=_ForwardPart_%u.%u",
								 (guint) time (NULL), g_random_int ()));

	text_part = make_text_part (body != NULL ? body : "", "text/plain");
	camel_multipart_add_part (multipart, text_part);
	g_object_unref (text_part);

	forward_part = camel_mime_part_new ();
	camel_mime_part_set_content (forward_part, orig_str, strlen (orig_str),
				     "message/rfc822");
	camel_mime_part_set_filename (forward_part, "message.eml");
	camel_mime_part_set_disposition (forward_part, "attachment");
	camel_multipart_add_part (multipart, forward_part);
	g_object_unref (forward_part);

	/* Add any attachments */
	if (attach_paths != NULL) {
		for (guint i = 0; i < attach_paths->len; i++) {
			const gchar *path = g_ptr_array_index (attach_paths, i);
			CamelMimePart *attachment = make_attachment_part (path, error);

			if (attachment == NULL) {
				g_object_unref (multipart);
				return FALSE;
			}
			camel_multipart_add_part (multipart, attachment);
			g_object_unref (attachment);
		}
	}

	body_part = camel_mime_part_new ();
	camel_medium_set_content (CAMEL_MEDIUM (body_part), CAMEL_DATA_WRAPPER (multipart));
	camel_mime_part_set_disposition (body_part, "inline");
	camel_medium_set_content (CAMEL_MEDIUM (message), CAMEL_DATA_WRAPPER (body_part));
	g_object_unref (body_part);
	g_object_unref (multipart);
	g_free (orig_str);

	return evo_send_message_raw (app, account, message,
				     to != NULL ? to : "",
				     original->subject != NULL ? original->subject : "",
				     dry_run, save_copy, json, error);
}
