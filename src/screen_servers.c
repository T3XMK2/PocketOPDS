#include <string.h>
#include <stdio.h>
#include <inkview.h>
#include "app.h"
#include "config.h"
#include "opds.h"

/* ─────────────────────────────────────────────────────────────────────────────
 * Server list screen
 *
 * Items:
 *   [0 … g_server_count-1]  configured servers
 *   [g_server_count]          "+ Add server…" row
 * ─────────────────────────────────────────────────────────────────────────── */

/* Pending add-server state (filled across multiple keyboard callbacks) */
static char s_new_name[MAX_NAME_LEN]  = {0};
static char s_new_url [MAX_URL_LEN]   = {0};
static char s_new_user[MAX_CRED_LEN]  = {0};
static char s_new_pass[MAX_CRED_LEN]  = {0};

/* Index of server being deleted/edited (-1 = none) */
static int s_target_idx = -1;

/* Forward declarations */
static void ask_server_url(void);
static void ask_server_user(void);
static void ask_server_pass(void);
static void finish_add_server(void);

/* ── Keyboard callbacks ───────────────────────────────────────────────────── */

static void kb_name_done(char *text)
{
    if (text && text[0])
        strncpy(s_new_name, text, MAX_NAME_LEN - 1);
    else
        s_new_name[0] = '\0';

    if (s_new_name[0])
        SetHardTimer("kburl", ask_server_url, 150);
    /* else: cancelled */
}

static void kb_url_done(char *text)
{
    if (text && text[0]) {
        /* Strip leading/trailing whitespace and common URL garbage */
        char *p = text;
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
        strncpy(s_new_url, p, MAX_URL_LEN - 1);
        /* Strip trailing whitespace */
        int len = (int)strlen(s_new_url);
        while (len > 0 && (s_new_url[len-1] == ' ' || s_new_url[len-1] == '\t' ||
                           s_new_url[len-1] == '\r' || s_new_url[len-1] == '\n'))
            s_new_url[--len] = '\0';
    } else {
        s_new_url[0] = '\0';
    }

    if (s_new_url[0]) {
        /* Prepend http:// if the user omitted the scheme */
        if (!strstr(s_new_url, "://")) {
            char tmp[MAX_URL_LEN];
            snprintf(tmp, sizeof(tmp), "http://%s", s_new_url);
            strncpy(s_new_url, tmp, MAX_URL_LEN - 1);
        }
        SetHardTimer("kbusr", ask_server_user, 150);
    }
    /* else: cancelled */
}

static void kb_user_done(char *text)
{
    /* username is optional – empty means anonymous */
    strncpy(s_new_user, text ? text : "", MAX_CRED_LEN - 1);
    SetHardTimer("kbpass", ask_server_pass, 150);
}

static void kb_pass_done(char *text)
{
    strncpy(s_new_pass, text ? text : "", MAX_CRED_LEN - 1);
    SetHardTimer("kbfin", finish_add_server, 150);
}

static void ask_server_url(void)
{
    OpenKeyboard("Server URL (e.g. http://192.168.1.10:8080/opds)",
                 s_new_url, MAX_URL_LEN - 1,
                 KBD_NORMAL, kb_url_done);
}

static void ask_server_user(void)
{
    OpenKeyboard("Username (leave empty for anonymous)",
                 s_new_user, MAX_CRED_LEN - 1,
                 KBD_NORMAL, kb_user_done);
}

static void ask_server_pass(void)
{
    if (!s_new_user[0]) {
        /* No username → skip password */
        s_new_pass[0] = '\0';
        finish_add_server();
        return;
    }
    OpenKeyboard("Password", s_new_pass, MAX_CRED_LEN - 1,
                 KBD_PASSWORD, kb_pass_done);
}

static void finish_add_server(void)
{
    if (s_target_idx < 0) {
        /* New server */
        config_add_server(s_new_name, s_new_url, s_new_user, s_new_pass);
    } else {
        /* Edit existing */
        config_update_server(s_target_idx,
                             s_new_name, s_new_url,
                             s_new_user, s_new_pass);
    }
    s_target_idx = -1;
    /* Re-open list so it picks up the new server count */
    show_server_list();
}

/* ── Delete confirmation ─────────────────────────────────────────────────── */

static void start_edit_server(int idx);

static void delete_dialog_cb(int button)
{
    if (button == 1 /* "Edit" */ && s_target_idx >= 0) {
        int idx = s_target_idx;
        s_target_idx = -1;
        start_edit_server(idx);
        return;
    }
    if (button == 2 /* "Delete" */ && s_target_idx >= 0) {
        config_remove_server(s_target_idx);
    }
    s_target_idx = -1;
}

/* Pre-fill and start the keyboard flow for editing an existing server */
static void start_edit_server(int idx)
{
    if (idx < 0 || idx >= g_server_count) return;
    s_target_idx = idx;
    strncpy(s_new_name, g_servers[idx].name,     MAX_NAME_LEN - 1);
    strncpy(s_new_url,  g_servers[idx].url,      MAX_URL_LEN  - 1);
    strncpy(s_new_user, g_servers[idx].username, MAX_CRED_LEN - 1);
    strncpy(s_new_pass, g_servers[idx].password, MAX_CRED_LEN - 1);
    /* Re-use the same add-server keyboard flow; finish_add_server() detects
     * s_target_idx >= 0 and calls config_update_server() instead.          */
    OpenKeyboard("Server name", s_new_name,
                 MAX_NAME_LEN - 1, KBD_NORMAL, kb_name_done);
}

/* ── List item drawing ───────────────────────────────────────────────────── */

#define ROW_PAD     12

static void draw_server_row(int x, int y, int w, int h,
                            int idx, int selected)
{
    (void)selected;

    if (idx < g_server_count) {
        /* ── server row ── */
        server_t *s = &g_servers[idx];

        /* divider line at bottom */
        DrawLine(x, y + h - 1, x + w, y + h - 1, COL_LGRAY);

        SetFont(g_font_bold, COL_BLACK);
        DrawTextRect(x + ROW_PAD, y + ROW_PAD,
                     w - ROW_PAD * 2, g_font_size + 4,
                     s->name[0] ? s->name : "(unnamed)",
                     ALIGN_LEFT | DOTS);

        SetFont(g_font_small, COL_DGRAY);
        DrawTextRect(x + ROW_PAD, y + ROW_PAD + g_font_size + 6,
                     w - ROW_PAD * 2, g_font_size + 2,
                     s->url, ALIGN_LEFT | DOTS);

        /* auth badge */
        if (s->username[0]) {
            const char *badge = "[auth]";
            SetFont(g_font_small, COL_DGRAY);
            int bw = StringWidth(badge) + 8;
            DrawTextRect(x + w - bw - ROW_PAD, y + ROW_PAD,
                         bw, g_font_size + 4,
                         badge, ALIGN_RIGHT);
        }
    } else {
        /* ── "Add server" row ── */
        DrawLine(x, y + h - 1, x + w, y + h - 1, COL_LGRAY);
        SetFont(g_font_bold, COL_DGRAY);
        DrawTextRect(x, y, w, h, "+ Add server\u2026",
                     ALIGN_CENTER | VALIGN_MIDDLE);
    }
}

/* ── List handler ─────────────────────────────────────────────────────────── */

static int servers_list_handler(int action, int x, int y, int idx, int state)
{
    (void)state;

    int total = g_server_count + 1;     /* +1 for "add" row */

    switch (action) {
        case LIST_BEGINPAINT:
            return 0;

        case LIST_PAINT:
            draw_server_row(x, y, g_screen_w, g_item_h, idx, 0);
            return 0;

        case LIST_ENDPAINT:
            return 0;

        case LIST_OPEN:
            if (idx < g_server_count) {
                /* Call show_catalog DIRECTLY from LIST_OPEN — InkView pushes the
                 * catalog onto the nav stack so it shows ← (back), not ⌂ (home).
                 * show_catalog is non-blocking: it opens a placeholder list and
                 * defers the actual network fetch via SetHardTimer. */
                char title[MAX_NAME_LEN + 8];
                snprintf(title, sizeof(title), "%s", g_servers[idx].name);
                show_catalog(idx, g_servers[idx].url, title);
            } else {
                /* Add new server */
                memset(s_new_name, 0, sizeof(s_new_name));
                memset(s_new_url,  0, sizeof(s_new_url));
                memset(s_new_user, 0, sizeof(s_new_user));
                memset(s_new_pass, 0, sizeof(s_new_pass));
                s_target_idx = -1;
                OpenKeyboard("Server name", s_new_name,
                             MAX_NAME_LEN - 1, KBD_NORMAL, kb_name_done);
            }
            return 1;

        case LIST_MENU:
            /* Long-press → offer Edit / Delete */
            if (idx >= 0 && idx < g_server_count) {
                s_target_idx = idx;
                char msg[MAX_NAME_LEN + 32];
                snprintf(msg, sizeof(msg), "Server: %s", g_servers[idx].name);
                Dialog(ICON_QUESTION, "Server options", msg,
                       "Edit", "Delete", delete_dialog_cb);
            }
            return 1;

        case LIST_EXIT:
            CloseApp();
            return 1;

        default:
            break;
    }
    (void)x; (void)y; (void)total;
    return 0;
}

/* ── Public entry point ───────────────────────────────────────────────────── */

void show_server_list(void)
{
    OpenList("PocketOPDS", NULL,
             g_screen_w, g_item_h,
             g_server_count + 1,   /* +1 for "add" row */
             0,
             servers_list_handler);
    SetListHeaderLevel(0);  /* server list is the root screen: show ⌂ */
}
