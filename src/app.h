#ifndef POCKETOPDS_APP_H
#define POCKETOPDS_APP_H

/* ── Shared application state ────────────────────────────────────────────── */

#include <inkview.h>
#include "config.h"
#include "opds.h"

/* UI dimensions (set up in EVT_INIT based on screen size) */
extern int g_screen_w;
extern int g_screen_h;
extern int g_item_h;      /* height of each list row in pixels */
extern int g_font_size;   /* body font size                    */

/* Shared fonts (opened once, closed at EVT_EXIT) */
extern ifont *g_font_body;
extern ifont *g_font_bold;
extern ifont *g_font_small;

/* Shared color palette for e-ink display */
#define COL_BLACK  0x000000
#define COL_WHITE  0xffffff
#define COL_LGRAY  0xbbbbbb
#define COL_DGRAY  0x555555
#define COL_MGRAY  0x888888

/* ── Screen entry points (called from main / recursively from each other) ── */

/* Show the server management list.  Returns when user presses Back/Home. */
void show_server_list(void);

/* Re-render the current s_active_cat (or server list if NULL).
 * Safe to call from a SetHardTimer callback. */
void reopen_catalog(void);

/* Show an OPDS catalog fetched from url for server server_idx.
 * title is shown as the list header.
 * Returns when the user navigates back past this level. */
void show_catalog(int server_idx, const char *url, const char *title);

/* Show the detail / download screen for a single entry.
 * Returns when user presses Back. */
void show_detail(int server_idx, const opds_entry_t *entry);

/* Initiate download for an acquisition entry.
 * If only one format is available: downloads directly without opening a detail
 * screen.  If two formats exist: opens a minimal format-picker screen. */
void start_download(int server_idx, const opds_entry_t *entry);

#endif /* POCKETOPDS_APP_H */
