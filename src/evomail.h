#ifndef EVOMAIL_H
#define EVOMAIL_H

#include <camel/camel.h>
#include <glib.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque types */
typedef struct _EvoApp EvoApp;
typedef struct _EvoAccount EvoAccount;
typedef struct _EvoStore EvoStore;
typedef struct _EvoFolder EvoFolder;

/* Data-carrying types */
struct _EvoAccount {
	gchar *uid;
	gchar *name;
	gchar *email;
	gchar *backend;
	gchar *host;
	gint port;
	gboolean local;
	gchar *parent_uid;
	gchar *smtp_uid;
};
typedef struct _EvoAccount EvoAccount;

struct _EvoMessage {
	gchar *uid;
	gchar *subject;
	gchar *from;
	gchar *to;
	gchar *cc;
	gchar *date;
	gchar *message_id;
	guint32 flags;
	gchar *text;
	gchar *html;
	GPtrArray *attachments;
};
typedef struct _EvoMessage EvoMessage;

struct _EvoAttachment {
	gchar *filename;
	gchar *content_type;
	gsize size;
};
typedef struct _EvoAttachment EvoAttachment;

/* Message flags (evomail-native, not Camel bitmasks) */
typedef enum {
	EVO_MESSAGE_FLAG_SEEN = 1 << 0,
	EVO_MESSAGE_FLAG_FLAGGED = 1 << 1,
	EVO_MESSAGE_FLAG_DELETED = 1 << 2,
} EvoMessageFlag;

typedef enum {
	EVO_FLAG_OP_SET,
	EVO_FLAG_OP_CLEAR,
	EVO_FLAG_OP_TOGGLE,
} EvoFlagOp;

/* App lifecycle */
EvoApp *evo_app_new (GError **error);
void     evo_app_free (EvoApp *app);
GPtrArray *evo_app_get_accounts (EvoApp *app);
EvoAccount *evo_app_find_account (EvoApp *app, const gchar *selector, GError **error);

/* Config file */
gboolean evo_config_load (void);
void     evo_config_free (void);
const gchar *evo_config_get_default_account (void);
const gchar *evo_config_get_default_folder (void);

/* Account properties */
const gchar *evo_account_get_uid (EvoAccount *account);
const gchar *evo_account_get_name (EvoAccount *account);
const gchar *evo_account_get_email (EvoAccount *account);
const gchar *evo_account_get_backend (EvoAccount *account);
const gchar *evo_account_get_host (EvoAccount *account);
gint evo_account_get_port (EvoAccount *account);
gboolean evo_account_is_local (EvoAccount *account);
const gchar *evo_account_get_smtp_uid (EvoAccount *account);

/* Store and folder */
EvoStore *evo_store_open (EvoApp *app, EvoAccount *account, GError **error);
void evo_store_close (EvoStore *store);
EvoFolder *evo_folder_open (EvoStore *store, const gchar *name, GError **error);
void evo_folder_close (EvoFolder *folder);
gchar *evo_find_special_folder (EvoStore *store, guint32 type, GError **error);
GPtrArray *evo_folder_list_messages (EvoFolder *folder, gboolean refresh, GError **error);
GPtrArray *evo_folder_search_messages (EvoFolder *folder, const gchar *query, gboolean refresh, GError **error);
EvoMessage *evo_folder_read_message (EvoFolder *folder, const gchar *uid, const gchar *save_dir, GError **error);
gboolean evo_folder_mark_messages (EvoFolder *folder, const gchar * const *uids, guint n_uids, EvoMessageFlag mask, EvoFlagOp op, GError **error);
gboolean evo_folder_move_messages (EvoFolder *folder, const gchar * const *uids, guint n_uids, EvoFolder *dest_folder, GError **error);
gboolean evo_folder_copy_messages (EvoFolder *folder, const gchar * const *uids, guint n_uids, EvoFolder *dest_folder, GError **error);

/* Message getters */
const gchar *evo_message_get_uid (EvoMessage *msg);
const gchar *evo_message_get_subject (EvoMessage *msg);
const gchar *evo_message_get_from (EvoMessage *msg);
const gchar *evo_message_get_to (EvoMessage *msg);
const gchar *evo_message_get_cc (EvoMessage *msg);
const gchar *evo_message_get_date (EvoMessage *msg);
const gchar *evo_message_get_message_id (EvoMessage *msg);
guint32 evo_message_get_flags (EvoMessage *msg);
const gchar *evo_message_get_text (EvoMessage *msg);
const gchar *evo_message_get_html (EvoMessage *msg);
GPtrArray *evo_message_get_attachments (EvoMessage *msg);

/* Utility */
gchar *format_date (gint64 date);

/* Attachment getters */
const gchar *evo_attachment_get_filename (EvoAttachment *att);
const gchar *evo_attachment_get_content_type (EvoAttachment *att);
gsize evo_attachment_get_size (EvoAttachment *att);

/* Freeing */
void evo_message_free (EvoMessage *msg);
void evo_attachment_free (EvoAttachment *att);

/* Send */
gboolean evo_send (EvoApp *app, EvoAccount *account,
		   const gchar *to, const gchar *cc, const gchar *bcc,
		   const gchar *subject, const gchar *body,
		   gboolean html, GPtrArray *attach_paths,
		   gboolean dry_run, gboolean save_copy, gboolean json,
		   GError **error);
gboolean evo_send_reply (EvoApp *app, EvoAccount *account,
			 EvoMessage *original, const gchar *body,
			 gboolean html, GPtrArray *attach_paths,
			 gboolean dry_run, gboolean save_copy, gboolean json,
			 gboolean reply_all,
			 GError **error);
gboolean evo_send_forward (EvoApp *app, EvoAccount *account,
			   EvoMessage *original, const gchar *body,
			   gboolean html, GPtrArray *attach_paths,
			   gboolean dry_run, gboolean save_copy, gboolean json,
			   GError **error);

/* Folder operations */
gboolean evo_folder_move_messages (EvoFolder *folder, const gchar * const *uids,
				   guint n_uids, EvoFolder *dest_folder,
				   GError **error);
gboolean evo_folder_copy_messages (EvoFolder *folder, const gchar * const *uids,
				   guint n_uids, EvoFolder *dest_folder,
				   GError **error);

#ifdef __cplusplus
}
#endif

#endif /* EVOMAIL_H */
