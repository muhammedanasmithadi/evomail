#ifndef EVO_APP_H
#define EVO_APP_H

#include "evomail.h"

#include <camel/camel.h>
#include <libedataserver/libedataserver.h>

/* Create a new EvoApp connected to Evolution Data Server.
 * This initializes the ESourceRegistry, creates a CamelSession subclass
 * with OAuth support, and discovers all configured mail accounts.
 *
 * Returns a new EvoApp on success, or NULL and sets @error on failure.
 * The caller owns the returned EvoApp and must free it with evo_app_free().
 */
EvoApp *evo_app_new (GError **error);

/* Free an EvoApp and release all associated resources
 * (EDS registry, Camel session, account list).
 */
void     evo_app_free  (EvoApp *app);

/* Get the list of discovered accounts.
 * The returned GPtrArray is owned by @app and must not be freed by the
 * caller. Element type is EvoAccount *.
 */
GPtrArray *evo_app_get_accounts (EvoApp *app);
gboolean evo_load_accounts (EvoApp *app, GError **error);

CamelSession *evo_app_get_session (EvoApp *app);
ESourceRegistry *evo_app_get_registry (EvoApp *app);

CamelStore *evo_store_get_camel_store (EvoStore *store);
CamelFolder *evo_folder_get_camel_folder (EvoFolder *folder);

/* Config file */
gboolean evo_config_load (void);
void     evo_config_free (void);
const gchar *evo_config_get_default_account (void);
const gchar *evo_config_get_default_folder (void);

gboolean evo_config_set_default_account (const gchar *account_uid);
gboolean evo_config_set_default_folder (const gchar *folder_name);

#endif /* EVO_APP_H */
