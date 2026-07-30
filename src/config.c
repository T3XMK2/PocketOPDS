#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <dirent.h>
#include <inkview.h>
#include "config.h"


/* ── Globals ─────────────────────────────────────────────────────────────── */


server_t g_servers[MAX_SERVERS];
int      g_server_count = 0;


/* Keep generated files out of the applications root. OpenConfig may create
 * auxiliary files next to the config, so the whole directory is dedicated
 * to PocketOPDS. */
#define DATA_DIR        FLASHDIR "/applications/PocketOPDSData"
#define CFG_PATH        DATA_DIR "/settings.cfg"


/* ── Helpers ─────────────────────────────────────────────────────────────── */


/* Build a key like "name_3" into buf[]. */
static void make_key(char *buf, size_t sz, const char *prefix, int idx)
{
    snprintf(buf, sz, "%s_%d", prefix, idx);
}

static void migrate_legacy_config(void)
{
    const char *legacy_name = "PocketOPDS.cfg";
    const size_t prefix_len = strlen(legacy_name);
    DIR *dir = opendir(FLASHDIR "/applications");
    if (!dir) return;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        /* Move PocketOPDS.cfg and any OpenConfig companions such as backups
         * or temporary files, but never PocketOPDS.app. */
        if (strncmp(entry->d_name, legacy_name, prefix_len) != 0)
            continue;
        char old_path[512];
        char new_path[512];
        snprintf(old_path, sizeof(old_path), "%s/applications/%s",
                 FLASHDIR, entry->d_name);
        snprintf(new_path, sizeof(new_path), "%s/settings.cfg%s",
                 DATA_DIR, entry->d_name + prefix_len);
        if (access(new_path, F_OK) != 0) {
            rename(old_path, new_path);
        } else {
            /* A new-format config already exists: preserve the older file
             * inside the data directory instead of leaving it loose. */
            snprintf(new_path, sizeof(new_path), "%s/legacy-%s",
                     DATA_DIR, entry->d_name);
            if (access(new_path, F_OK) != 0)
                rename(old_path, new_path);
        }
    }
    closedir(dir);
}


/* ── Public API ──────────────────────────────────────────────────────────── */


void config_load(void)
{
    iv_mkdir(DATA_DIR, 0755);
    migrate_legacy_config();

    iconfig *cfg = OpenConfig(CFG_PATH, NULL);
    if (!cfg) {
        g_server_count = 0;
        return;
    }


    g_server_count = ReadInt(cfg, "server_count", 0);
    if (g_server_count < 0) g_server_count = 0;
    if (g_server_count > MAX_SERVERS) g_server_count = MAX_SERVERS;


    char key[64];
    for (int i = 0; i < g_server_count; i++) {
        make_key(key, sizeof(key), "name", i);
        strncpy(g_servers[i].name, ReadString(cfg, key, ""), MAX_NAME_LEN - 1);


        make_key(key, sizeof(key), "url", i);
        strncpy(g_servers[i].url, ReadString(cfg, key, ""), MAX_URL_LEN - 1);


        make_key(key, sizeof(key), "user", i);
        strncpy(g_servers[i].username, ReadString(cfg, key, ""), MAX_CRED_LEN - 1);


        make_key(key, sizeof(key), "pass", i);
        strncpy(g_servers[i].password, ReadString(cfg, key, ""), MAX_CRED_LEN - 1);
    }


    CloseConfig(cfg);
}


void config_save(void)
{
    iv_mkdir(DATA_DIR, 0755);
    iconfig *cfg = OpenConfig(CFG_PATH, NULL);
    if (!cfg) return;


    WriteInt(cfg, "server_count", g_server_count);


    char key[64];
    for (int i = 0; i < g_server_count; i++) {
        make_key(key, sizeof(key), "name", i);
        WriteString(cfg, key, g_servers[i].name);


        make_key(key, sizeof(key), "url", i);
        WriteString(cfg, key, g_servers[i].url);


        make_key(key, sizeof(key), "user", i);
        WriteString(cfg, key, g_servers[i].username);


        make_key(key, sizeof(key), "pass", i);
        WriteString(cfg, key, g_servers[i].password);
    }


    SaveConfig(cfg);
    CloseConfig(cfg);
}


int config_add_server(const char *name, const char *url,
                      const char *username, const char *password)
{
    if (g_server_count >= MAX_SERVERS) return 0;


    int i = g_server_count++;
    strncpy(g_servers[i].name,     name     ? name     : "", MAX_NAME_LEN - 1);
    strncpy(g_servers[i].url,      url      ? url      : "", MAX_URL_LEN  - 1);
    strncpy(g_servers[i].username, username ? username : "", MAX_CRED_LEN - 1);
    strncpy(g_servers[i].password, password ? password : "", MAX_CRED_LEN - 1);


    config_save();
    return 1;
}


void config_remove_server(int i)
{
    if (i < 0 || i >= g_server_count) return;


    /* Shift remaining entries left */
    for (int j = i; j < g_server_count - 1; j++)
        g_servers[j] = g_servers[j + 1];


    g_server_count--;
    config_save();
}


void config_update_server(int i,
                          const char *name, const char *url,
                          const char *username, const char *password)
{
    if (i < 0 || i >= g_server_count) return;


    if (name)     strncpy(g_servers[i].name,     name,     MAX_NAME_LEN - 1);
    if (url)      strncpy(g_servers[i].url,       url,      MAX_URL_LEN  - 1);
    if (username) strncpy(g_servers[i].username,  username, MAX_CRED_LEN - 1);
    if (password) strncpy(g_servers[i].password,  password, MAX_CRED_LEN - 1);


    config_save();
}
