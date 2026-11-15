#include "evomail.h"
#include "evoapp.h"

#include <camel/camel.h>
#include <libedataserver/libedataserver.h>

/* EvoApp is opaque - its definition lives only here. */
struct _EvoApp {
	ESourceRegistry *registry;
	CamelSession    *session;
	GPtrArray       *accounts;
};

/* CamelSession subclass (OAuth via EDS) */
typedef struct {
	CamelSession parent;
	ESourceRegistry *registry;
} EvoSession;

typedef struct {
	CamelSessionClass parent_class;
} EvoSessionClass;

G_DEFINE_TYPE (EvoSession, evo_session, CAMEL_TYPE_SESSION)

static ESource *
ref_source_cb (gpointer user_data, const gchar *uid)
{
	EvoSession *self = user_data;
	return e_source_registry_ref_source (self->registry, uid);
}

static gboolean
evo_session_get_oauth2_access_token_sync (CamelSession *session,
					  CamelService *service,
					  gchar **out_access_token,
					  gint *out_expires_in,
					  GCancellable *cancellable,
					  GError **error)
{
	EvoSession *self = (EvoSession *) session;
	const gchar *uid = camel_service_get_uid (service);
	ESource *source;
	EOAuth2Services *services;
	EOAuth2Service *oauth2;
	gboolean success = FALSE;

	source = e_source_registry_ref_source (self->registry, uid);
	if (source == NULL) {
		g_set_error (error, CAMEL_SERVICE_ERROR,
			     CAMEL_SERVICE_ERROR_UNAVAILABLE,
			     "No Evolution source found for account %s", uid);
		return FALSE;
	}

	services = e_oauth2_services_new ();
	oauth2 = e_oauth2_services_find (services, source);
	if (oauth2 != NULL) {
		success = e_oauth2_service_get_access_token_sync (
			oauth2, source, ref_source_cb, self,
			out_access_token, out_expires_in, cancellable, error);
	} else {
		g_set_error (error, CAMEL_SERVICE_ERROR,
			     CAMEL_SERVICE_ERROR_UNAVAILABLE,
			     "Account %s is not an OAuth2 account", uid);
	}

	g_object_unref (services);
	g_object_unref (source);
	return success;
}

static gboolean
evo_session_authenticate_sync (CamelSession *session,
			       CamelService *service,
			       const gchar *mechanism,
			       GCancellable *cancellable,
			       GError **error)
{
	CamelAuthenticationResult result;

	(void) session;

	if (g_strcmp0 (mechanism, "none") == 0)
		mechanism = NULL;

	result = camel_service_authenticate_sync (service, mechanism,
						  cancellable, error);
	if (result == CAMEL_AUTHENTICATION_REJECTED &&
	    error != NULL && *error == NULL) {
		g_set_error (error, CAMEL_SERVICE_ERROR,
			     CAMEL_SERVICE_ERROR_CANT_AUTHENTICATE,
			     "%s authentication failed",
			     mechanism != NULL ? mechanism : "Unknown");
	}

	return result == CAMEL_AUTHENTICATION_ACCEPTED;
}

static void
evo_session_class_init (EvoSessionClass *klass)
{
	CamelSessionClass *session_class = CAMEL_SESSION_CLASS (klass);

	session_class->get_oauth2_access_token_sync =
		evo_session_get_oauth2_access_token_sync;
	session_class->authenticate_sync = evo_session_authenticate_sync;
}

static void
evo_session_init (EvoSession *self)
{
	(void) self;
}

static void
evo_session_set_registry (EvoSession *self, ESourceRegistry *registry)
{
	g_clear_object (&self->registry);
	self->registry = g_object_ref (registry);
}

EvoApp *
evo_app_new (GError **error)
{
	EvoApp *app;
	ESourceRegistry *registry;
	CamelSession *session;
	gchar *data_dir;
	gchar *cache_dir;

	app = g_new0 (EvoApp, 1);

	registry = e_source_registry_new_sync (NULL, error);
	if (registry == NULL) {
		g_free (app);
		return NULL;
	}

	data_dir = g_build_filename (g_get_user_data_dir (), "evolution", NULL);
	cache_dir = g_build_filename (g_get_user_cache_dir (), "evolution", NULL);

	session = g_object_new (evo_session_get_type (),
				"user-data-dir", data_dir,
				"user-cache-dir", cache_dir,
				NULL);
	evo_session_set_registry ((EvoSession *) session, registry);
	camel_session_set_online (session, TRUE);

	g_free (data_dir);
	g_free (cache_dir);

	app->registry = registry;
	app->session = session;
	app->accounts = NULL;

	return app;
}

void
evo_app_free (EvoApp *app)
{
	if (app == NULL)
		return;

	if (app->accounts != NULL)
		g_ptr_array_unref (app->accounts);
	if (app->session != NULL)
		g_object_unref (app->session);
	if (app->registry != NULL)
		g_object_unref (app->registry);

	g_free (app);
}

GPtrArray *
evo_app_get_accounts (EvoApp *app)
{
	return app->accounts;
}

CamelSession *
evo_app_get_session (EvoApp *app)
{
	return app->session;
}

ESourceRegistry *
evo_app_get_registry (EvoApp *app)
{
	return app->registry;
}

/* Account discovery */
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

static void
account_free (gpointer data)
{
	EvoAccount *account = data;

	if (account == NULL)
		return;

	g_free (account->uid);
	g_free (account->name);
	g_free (account->email);
	g_free (account->backend);
	g_free (account->host);
	g_free (account->parent_uid);
	g_free (account->smtp_uid);
	g_free (account);
}

static EvoAccount *
account_from_source (ESource *source)
{
	EvoAccount *account;
	ESourceMailAccount *mail_account;
	ESourceAuthentication *auth;
	const gchar *backend;

	mail_account = e_source_get_extension (source, E_SOURCE_EXTENSION_MAIL_ACCOUNT);
	if (mail_account == NULL)
		return NULL;

	backend = e_source_backend_get_backend_name (E_SOURCE_BACKEND (mail_account));
	if (backend == NULL || *backend == '\0')
		return NULL;

	account = g_new0 (EvoAccount, 1);
	account->uid = g_strdup (e_source_get_uid (source));
	account->backend = g_strdup (backend);
	account->local = g_strcmp0 (backend, "maildir") == 0;
	account->parent_uid = g_strdup (e_source_get_parent (source));

	account->name = g_strdup (e_source_get_display_name (source));

	auth = e_source_get_extension (source, E_SOURCE_EXTENSION_AUTHENTICATION);
	if (auth != NULL) {
		const gchar *host = e_source_authentication_get_host (auth);
		const gchar *user = e_source_authentication_get_user (auth);

		account->host = g_strdup (host);
		account->port = e_source_authentication_get_port (auth);
		if (user != NULL && *user != '\0')
			account->email = g_strdup (user);
	}

	return account;
}

static void
account_apply_identity (EvoAccount *account, ESourceRegistry *registry,
			const gchar *identity_uid)
{
	ESource *identity;
	ESourceMailIdentity *mail_identity;

	if (identity_uid == NULL || *identity_uid == '\0')
		return;

	identity = e_source_registry_ref_source (registry, identity_uid);
	if (identity == NULL)
		return;

	mail_identity = e_source_get_extension (identity,
						E_SOURCE_EXTENSION_MAIL_IDENTITY);
	if (mail_identity != NULL) {
		const gchar *address = e_source_mail_identity_get_address (mail_identity);
		const gchar *name = e_source_mail_identity_get_name (mail_identity);

		if (address != NULL && *address != '\0') {
			g_free (account->email);
			account->email = g_strdup (address);
		}
		if (name != NULL && *name != '\0') {
			g_free (account->name);
			account->name = g_strdup (name);
		}
	}

	g_object_unref (identity);
}

static gboolean
transport_matches (ESource *transport, EvoAccount *account, ESource *account_source)
{
	ESourceAuthentication *auth;
	const gchar *parent;
	const gchar *account_parent;

	parent = e_source_get_parent (transport);
	account_parent = e_source_get_parent (account_source);
	if (parent != NULL && account_parent != NULL &&
	    g_strcmp0 (parent, account_parent) == 0)
		return TRUE;

	auth = e_source_get_extension (transport, E_SOURCE_EXTENSION_AUTHENTICATION);
	if (auth != NULL && account->email != NULL) {
		const gchar *user = e_source_authentication_get_user (auth);
		if (user != NULL && g_ascii_strcasecmp (user, account->email) == 0)
			return TRUE;
	}

	return FALSE;
}

gboolean
evo_load_accounts (EvoApp *app, GError **error)
{
	(void) error;
	GList *sources;
	GList *link;
	GPtrArray *transports;

	app->accounts = g_ptr_array_new_with_free_func (account_free);

	sources = e_source_registry_list_sources (app->registry,
						  E_SOURCE_EXTENSION_MAIL_ACCOUNT);
	for (link = sources; link != NULL; link = g_list_next (link)) {
		ESource *source = E_SOURCE (link->data);
		ESourceMailAccount *mail_account;
		EvoAccount *account;
		gchar *identity_uid;

		if (!e_source_get_enabled (source))
			continue;

		mail_account = e_source_get_extension (source,
						     E_SOURCE_EXTENSION_MAIL_ACCOUNT);
		if (mail_account == NULL)
			continue;

		account = account_from_source (source);
		if (account == NULL)
			continue;

		identity_uid = g_strdup (
			e_source_mail_account_get_identity_uid (mail_account));
		account_apply_identity (account, app->registry, identity_uid);
		g_free (identity_uid);

		if (account->name == NULL || *account->name == '\0') {
			g_free (account->name);
			account->name = g_strdup (account->email != NULL ?
						  account->email : account->uid);
		}

		g_ptr_array_add (app->accounts, account);
	}
	g_list_free (sources);

	transports = g_ptr_array_new ();
	sources = e_source_registry_list_sources (app->registry,
						  E_SOURCE_EXTENSION_MAIL_TRANSPORT);
	for (link = sources; link != NULL; link = g_list_next (link))
		g_ptr_array_add (transports, E_SOURCE (link->data));
	g_list_free (sources);

	for (guint i = 0; i < app->accounts->len; i++) {
		EvoAccount *account = g_ptr_array_index (app->accounts, i);
		ESource *account_source;

		if (account->local)
			continue;

		account_source = e_source_registry_ref_source (app->registry,
							       account->uid);
		if (account_source == NULL)
			continue;

		for (guint j = 0; j < transports->len; j++) {
			ESource *transport = g_ptr_array_index (transports, j);
			ESourceMailTransport *mail_transport;
			const gchar *backend;

			mail_transport = e_source_get_extension (transport,
							     E_SOURCE_EXTENSION_MAIL_TRANSPORT);
			if (mail_transport == NULL)
				continue;
			backend = e_source_backend_get_backend_name (
				E_SOURCE_BACKEND (mail_transport));
			if (g_strcmp0 (backend, "smtp") != 0)
				continue;
			if (!transport_matches (transport, account, account_source))
				continue;

			g_free (account->smtp_uid);
			account->smtp_uid = g_strdup (e_source_get_uid (transport));
			break;
		}

		g_object_unref (account_source);
	}

	g_ptr_array_unref (transports);
	return TRUE;
}

EvoAccount *
evo_app_find_account (EvoApp *app, const gchar *selector, GError **error)
{
	EvoAccount *fallback = NULL;
	GPtrArray *matches;
	const gchar *env_selector;

	env_selector = g_getenv ("EVOMAIL_ACCOUNT");
	if (selector == NULL || *selector == '\0') {
		selector = (env_selector != NULL && *env_selector != '\0') ?
			   env_selector : NULL;
	}

	if (selector == NULL) {
		for (guint i = 0; i < app->accounts->len; i++) {
			EvoAccount *account = g_ptr_array_index (app->accounts, i);

			if (fallback == NULL)
				fallback = account;
			if (!account->local && account->smtp_uid != NULL)
				return account;
		}
		if (fallback != NULL)
			return fallback;
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
				     "No Evolution mail accounts found.");
		return NULL;
	}

	matches = g_ptr_array_new ();
	for (guint i = 0; i < app->accounts->len; i++) {
		EvoAccount *account = g_ptr_array_index (app->accounts, i);
		gboolean match = FALSE;

		if (g_strcmp0 (account->uid, selector) == 0)
			match = TRUE;
		else if (account->email != NULL &&
			 g_ascii_strcasecmp (account->email, selector) == 0)
			match = TRUE;
		else if (account->name != NULL &&
			 g_ascii_strcasecmp (account->name, selector) == 0)
			match = TRUE;
		else if (strlen (selector) >= 4 &&
			 g_str_has_prefix (account->uid, selector))
			match = TRUE;

		if (match)
			g_ptr_array_add (matches, account);
	}

	if (matches->len == 1) {
		EvoAccount *account = g_ptr_array_index (matches, 0);
		g_ptr_array_unref (matches);
		return account;
	}

	if (matches->len == 0) {
		GString *available = g_string_new (NULL);

		for (guint i = 0; i < app->accounts->len; i++) {
			EvoAccount *account = g_ptr_array_index (app->accounts, i);

			if (i > 0)
				g_string_append (available, ", ");
			g_string_append_printf (available, "%s",
						account->email != NULL ?
						account->email : account->uid);
		}
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
			     "No account matches %s. Available: %s",
			     selector, available->str);
		g_string_free (available, TRUE);
		g_ptr_array_unref (matches);
		return NULL;
	}

	g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		     "Account selector %s is ambiguous.", selector);
	g_ptr_array_unref (matches);
	return NULL;
}

/* Store opening */
struct _EvoStore {
	CamelStore *store;
	EvoApp *app;
};

EvoStore *
evo_store_open (EvoApp *app, EvoAccount *account, GError **error)
{
	ESource *source;
	CamelService *service;
	CamelStore *store;

	source = e_source_registry_ref_source (app->registry, account->uid);
	if (source == NULL) {
		g_set_error (error, CAMEL_SERVICE_ERROR,
			     CAMEL_SERVICE_ERROR_UNAVAILABLE,
			     "No Evolution source for account %s",
			     account->uid);
		return NULL;
	}

	service = camel_session_add_service (app->session, account->uid,
					     account->backend,
					     CAMEL_PROVIDER_STORE, error);
	if (service == NULL) {
		g_object_unref (source);
		return NULL;
	}

	e_source_camel_configure_service (source, service);
	store = CAMEL_STORE (service);

	if (CAMEL_IS_OFFLINE_STORE (store)) {
		if (!camel_offline_store_set_online_sync (
				CAMEL_OFFLINE_STORE (store), TRUE,
				NULL, error)) {
			g_object_unref (source);
			g_object_unref (service);
			return NULL;
		}
	} else if (!camel_service_connect_sync (service, NULL, error)) {
		g_object_unref (source);
		g_object_unref (service);
		return NULL;
	}

	g_object_unref (source);

	EvoStore *estore = g_new0 (EvoStore, 1);
	estore->store = store;
	estore->app = app;
	return estore;
}

void
evo_store_close (EvoStore *store)
{
	if (store == NULL)
		return;
	g_object_unref (store->store);
	g_free (store);
}

CamelStore *
evo_store_get_camel_store (EvoStore *store)
{
	return store->store;
}

EvoApp *
evo_store_get_app (EvoStore *store)
{
	return store->app;
}

/* Account getters */
const gchar *
evo_account_get_uid (EvoAccount *account)
{
	return account->uid;
}

const gchar *
evo_account_get_name (EvoAccount *account)
{
	return account->name;
}

const gchar *
evo_account_get_email (EvoAccount *account)
{
	return account->email;
}

const gchar *
evo_account_get_backend (EvoAccount *account)
{
	return account->backend;
}

const gchar *
evo_account_get_host (EvoAccount *account)
{
	return account->host;
}

gint
evo_account_get_port (EvoAccount *account)
{
	return account->port;
}

gboolean
evo_account_is_local (EvoAccount *account)
{
	return account->local;
}

const gchar *
evo_account_get_smtp_uid (EvoAccount *account)
{
	return account->smtp_uid;
}
