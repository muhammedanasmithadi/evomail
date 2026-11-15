#include "evomail.h"

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

CamelSession *
evo_session_new (ESourceRegistry *registry)
{
	EvoSession *session;
	gchar *data_dir;
	gchar *cache_dir;

	data_dir = g_build_filename (g_get_user_data_dir (), "evolution", NULL);
	cache_dir = g_build_filename (g_get_user_cache_dir (), "evolution", NULL);

	session = g_object_new (evo_session_get_type (),
				"user-data-dir", data_dir,
				"user-cache-dir", cache_dir,
				NULL);
	evo_session_set_registry (session, registry);
	camel_session_set_online (CAMEL_SESSION (session), TRUE);

	g_free (data_dir);
	g_free (cache_dir);

	return CAMEL_SESSION (session);
}

CamelStore *
evo_open_store (App *app, Account *account, GError **error)
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
	return store;
}
