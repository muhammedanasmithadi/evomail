#ifndef EVOMAIL_H
#define EVOMAIL_H

#include <camel/camel.h>
#include <libedataserver/libedataserver.h>

typedef struct {
	gchar *uid;
	gchar *name;
	gchar *email;
	gchar *backend;
	gchar *host;
	gint port;
	gboolean local;
	gchar *parent_uid;
	gchar *smtp_uid;
} Account;

typedef struct {
	ESourceRegistry *registry;
	CamelSession *session;
	GPtrArray *accounts; /* Account* */
} App;

typedef struct {
	gchar *uid;
	gchar *from;
	gchar *subject;
	gint64 date;
	guint32 flags;
} MsgSummary;

typedef struct {
	gchar *filename;
	gchar *content_type;
	gsize size;
} Attachment;

typedef struct {
	gchar *text;
	gchar *html;
	gchar *charset;
	GPtrArray *attachments; /* Attachment* */
} MessageBody;

typedef struct {
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
} ReadMessage;

/* session.c */
CamelSession *evo_session_new (ESourceRegistry *registry);
CamelStore *evo_open_store (App *app, Account *account, GError **error);

/* accounts.c */
gboolean evo_load_accounts (App *app, GError **error);
Account *evo_find_account (App *app, const gchar *selector, GError **error);
gboolean evo_open_transport (App *app, Account *account,
			    CamelTransport **out_transport, GError **error);

/* mail.c */
CamelFolder *evo_open_folder (CamelStore *store, const gchar *name,
			      gboolean writable, GError **error);
gchar *evo_find_special_folder (CamelStore *store, guint32 type, GError **error);
GPtrArray *evo_list_messages (CamelFolder *folder, gboolean refresh,
			      GError **error);
GPtrArray *evo_search_messages (CamelFolder *folder, const gchar *query,
				gboolean refresh, GError **error);
ReadMessage *evo_read_message (CamelFolder *folder, const gchar *uid,
			       const gchar *save_attachments, GError **error);
gboolean evo_mark_messages (CamelFolder *folder, gchar **uids,
			    guint n_uids, guint32 mask, guint32 set,
			    GError **error);
void msg_summary_free (MsgSummary *msg);
void read_message_free (ReadMessage *msg);
void message_body_free (MessageBody *body);
void attachment_free (Attachment *attachment);

/* output.c */
void json_string (GString *out, const gchar *value);
gchar *format_date (gint64 date);
gchar *format_flags (guint32 flags);
void print_message_table (GPtrArray *messages);
void print_message_json (GPtrArray *messages);
void print_read_message (ReadMessage *msg);
void print_read_json (ReadMessage *msg);

/* send.c */
gboolean evo_send (App *app, Account *account,
		   const gchar *to, const gchar *cc, const gchar *bcc,
		   const gchar *subject, const gchar *body, gboolean html,
		   GPtrArray *attach_paths, gboolean dry_run, gboolean save_copy,
		   gboolean json, GError **error);

#endif /* EVOMAIL_H */
