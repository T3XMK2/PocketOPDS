#ifndef POCKETOPDS_CONFIG_H
#define POCKETOPDS_CONFIG_H

/* ── Configuration / server-list management ─────────────────────────────────
 *
 * Servers are persisted to:
 *   /mnt/ext1/applications/PocketOPDS.cfg
 *
 * Layout (InkView key-value store):
 *   server_count   = N
 *   name_0         = "My Calibre"
 *   url_0          = "http://192.168.1.10:8080/opds"
 *   user_0         = ""
 *   pass_0         = ""
 *   ...
 * ─────────────────────────────────────────────────────────────────────────── */

#define MAX_SERVERS   16
#define MAX_NAME_LEN  64
#define MAX_URL_LEN   256
#define MAX_CRED_LEN  64

typedef struct {
    char name    [MAX_NAME_LEN];
    char url     [MAX_URL_LEN];
    char username[MAX_CRED_LEN];
    char password[MAX_CRED_LEN];
} server_t;

/* Global server list. Call config_load() at startup. */
extern server_t g_servers[MAX_SERVERS];
extern int      g_server_count;

/* Load servers from config file.  Safe to call even if file is missing. */
void config_load(void);

/* Persist current g_servers[] to config file. */
void config_save(void);

/* Add a new server.  Returns 1 on success, 0 if list is full. */
int  config_add_server(const char *name, const char *url,
                       const char *username, const char *password);

/* Remove server at index i. */
void config_remove_server(int i);

/* Update an existing server's fields in-place. */
void config_update_server(int i,
                          const char *name, const char *url,
                          const char *username, const char *password);

#endif /* POCKETOPDS_CONFIG_H */
