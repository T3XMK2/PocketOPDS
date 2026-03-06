#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <inkview.h>
#include "app.h"
#include "opds.h"
#include "config.h"
#include "net.h"

/* ─────────────────────────────────────────────────────────────────────────────
 * Catalog screen
 *
 * Shows entries from an OPDS Atom feed.
 * Navigation entries open a sub-catalog (recursive call).
 * Acquisition entries open the detail/download screen.
 * A "[Search]" row is prepended when the feed has a search URL.
 * A "[Load more…]" row is appended when a "next" page link exists.
 * ─────────────────────────────────────────────────────────────────────────── */

#define ROW_PAD    10

/* Pseudo-index offsets for special rows */
#define SPECIAL_NONE   0
#define SPECIAL_SEARCH 1
#define SPECIAL_MORE   2

/* Per-catalog state (heap-allocated; freed in LIST_EXIT) */
typedef struct cat_state_s {
    int                 server_idx;
    char                title[OPDS_MAX_TITLE]; /* list header title */
    char                url[OPDS_MAX_URL];     /* URL to fetch (async) */
    opds_feed_t        *feed;                  /* NULL until async load done */
    char                search_query[OPDS_MAX_TITLE];
    int                 has_search;
    int                 has_more;
    int                 first_entry;
    struct cat_state_s *prev;
} cat_state_t;

/* ── Loading helper ───────────────────────────────────────────────────────── */

static opds_feed_t *load_feed(int server_idx, const char *url)
{
    server_t *s = &g_servers[server_idx];

    /* Ensure WiFi is up */
    if (!net_wifi_ensure(30)) {
        opds_feed_t *err = calloc(1, sizeof(*err));
        if (err)
            snprintf(err->error, sizeof(err->error),
                     "Cannot connect to Wi-Fi. "
                     "Please check network settings.");
        return err;
    }

    opds_feed_t *feed = opds_fetch(url, s->username, s->password);

    /* Resolve relative URLs in entries using the feed's base */
    if (feed) {
        char resolved[OPDS_MAX_URL];
        for (int i = 0; i < feed->count; i++) {
            opds_entry_t *e = &feed->entries[i];

            if (e->nav_url[0]) {
                opds_resolve_url(feed, e->nav_url, resolved, sizeof(resolved));
                strncpy(e->nav_url, resolved, sizeof(e->nav_url) - 1);
            }
            if (e->download_url[0]) {
                opds_resolve_url(feed, e->download_url,
                                 resolved, sizeof(resolved));
                strncpy(e->download_url, resolved,
                        sizeof(e->download_url) - 1);
            }
            if (e->alt_url[0]) {
                opds_resolve_url(feed, e->alt_url, resolved, sizeof(resolved));
                strncpy(e->alt_url, resolved, sizeof(e->alt_url) - 1);
            }
        }
        /* Resolve search_url */
        if (feed->search_url[0]) {
            opds_resolve_url(feed, feed->search_url,
                             resolved, sizeof(resolved));
            strncpy(feed->search_url, resolved,
                    sizeof(feed->search_url) - 1);
        }
        /* Resolve next_page_url — same treatment as search_url */
        if (feed->next_page_url[0]) {
            opds_resolve_url(feed, feed->next_page_url,
                             resolved, sizeof(resolved));
            strncpy(feed->next_page_url, resolved,
                    sizeof(feed->next_page_url) - 1);
        }
    }

    return feed;
}

/* Build a search URL by substituting the query into an OpenSearch template.
 * Handles both {searchTerms} and {searchTerms?} patterns.               */
static void build_search_url(const char *tmpl, const char *query,
                             char *out, size_t out_size)
{
    char encoded[OPDS_MAX_URL * 3] = {0};
    /* Simple percent-encoding for the query */
    size_t ei = 0;
    for (size_t i = 0; query[i] && ei < sizeof(encoded) - 4; i++) {
        unsigned char c = (unsigned char)query[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' ||
            c == '.' || c == '~') {
            encoded[ei++] = (char)c;
        } else {
            snprintf(encoded + ei, 4, "%%%02X", c);
            ei += 3;
        }
    }

    /* Find {searchTerms} or {searchTerms?} in template */
    const char *p1 = strstr(tmpl, "{searchTerms}");
    const char *p2 = strstr(tmpl, "{searchTerms?}");
    const char *p  = p1 ? p1 : p2;
    int         plen = p1 ? 13 : 14;

    if (p) {
        size_t prefix_len = (size_t)(p - tmpl);
        snprintf(out, out_size, "%.*s%s%s",
                 (int)prefix_len, tmpl, encoded,
                 tmpl + (prefix_len + plen));
    } else {
        /* Fallback: append ?query= (Calibre and most simple servers use this) */
        snprintf(out, out_size, "%s?query=%s", tmpl, encoded);
    }
}

/* ── Row drawing ─────────────────────────────────────────────────────────── */


static void draw_entry_row(int x, int y, int w, int h,
                           const opds_entry_t *e)
{
    DrawLine(x, y + h - 1, x + w, y + h - 1, COL_LGRAY);

    if (e->type == ENTRY_NAVIGATION) {
        /* ── Navigation row: title left, › right ── */
        int arr_w = g_font_size + ROW_PAD;
        SetFont(g_font_body, COL_LGRAY);
        DrawTextRect(x + w - arr_w - ROW_PAD, y, arr_w, h,
                     ">", ALIGN_RIGHT | VALIGN_MIDDLE);

        SetFont(g_font_bold, COL_BLACK);
        DrawTextRect(x + ROW_PAD, y,
                     w - ROW_PAD * 3 - arr_w, h,
                     e->title, ALIGN_LEFT | VALIGN_MIDDLE | DOTS);
    } else {
        /* ── Acquisition row: title left + ↓ FORMAT badge on the same line ── */
        char badge[32];
        snprintf(badge, sizeof(badge), "\u2193 %s", opds_mime_short(e->download_mime));

        SetFont(g_font_small, COL_DGRAY);
        int badge_w = StringWidth(badge) + ROW_PAD;
        /* Draw badge aligned with the title baseline, not centred in full row */
        DrawTextRect(x + w - badge_w - ROW_PAD, y + ROW_PAD,
                     badge_w, g_font_size + 4,
                     badge, ALIGN_RIGHT | VALIGN_MIDDLE);

        int text_w = w - ROW_PAD * 2 - badge_w - ROW_PAD;

        SetFont(g_font_bold, COL_BLACK);
        DrawTextRect(x + ROW_PAD, y + ROW_PAD,
                     text_w, g_font_size + 4,
                     e->title, ALIGN_LEFT | DOTS);

        if (e->author[0]) {
            SetFont(g_font_small, COL_DGRAY);
            DrawTextRect(x + ROW_PAD,
                         y + ROW_PAD + g_font_size + 6,
                         text_w, g_font_size + 2,
                         e->author, ALIGN_LEFT | DOTS);
        }
    }
}

/* ── Search keyboard callback ─────────────────────────────────────────────── */

/* Passed via pointer as user data; not ideal in pure C but workable. */
static cat_state_t *s_pending_search_state = NULL;
/* Resolve the real search template by fetching the OpenSearch description.
 * Many servers (e.g. Calibre) advertise only the description URL in the feed
 * (no {searchTerms} placeholder).  This function fetches that description and
 * extracts the template= URL from the <Url> element, then calls show_catalog.
 */
static void timer_do_search(void)
{
    cat_state_t *cs = s_pending_search_state;
    s_pending_search_state = NULL;
    if (!cs) return;

    char tmpl[OPDS_MAX_URL];
    strncpy(tmpl, cs->feed->search_url, sizeof(tmpl) - 1);

    if (!strstr(tmpl, "{searchTerms}")) {
        /* Silently fetch the OpenSearch description (no progressbar here —
         * opening a new InkView widget this close to keyboard dismiss crashes) */
        server_t *s = &g_servers[cs->server_idx];
        net_response_t *resp = net_get(tmpl, s->username, s->password);
        if (resp && resp->data) {
            const char *tag = strstr(resp->data, "template=");
            if (tag) {
                tag += 9;
                char d = *tag++;
                if (d == '"' || d == '\'') {
                    const char *end = strchr(tag, d);
                    if (end) {
                        char raw[OPDS_MAX_URL] = {0};
                        size_t len = (size_t)(end - tag);
                        if (len >= sizeof(raw)) len = sizeof(raw) - 1;
                        memcpy(raw, tag, len);
                        opds_resolve_url(cs->feed, raw, tmpl, sizeof(tmpl));
                    }
                }
            }
        }
        net_response_free(resp);
    }

    char search_url[OPDS_MAX_URL];
    build_search_url(tmpl, cs->search_query, search_url, sizeof(search_url));

    char title[128];
    snprintf(title, sizeof(title), "Search: %s", cs->search_query);
    /* show_catalog opens a placeholder immediately, then loads asynchronously */
    show_catalog(cs->server_idx, search_url, title);
}

static void search_kb_done(char *text)
{
    if (!text || !text[0]) return;
    if (!s_pending_search_state) return;
    strncpy(s_pending_search_state->search_query, text,
            sizeof(s_pending_search_state->search_query) - 1);
    /* Give the keyboard more time to fully dismiss before we fire any
     * InkView widget calls — 300 ms is safe across all firmware versions. */
    SetHardTimer("srch", timer_do_search, 300);
}

/* ── List handler factory ─────────────────────────────────────────────────── */

/* We need a stable pointer to cat_state_t for the handler.
 * Since OpenList callbacks don't accept user-data pointers, we store
 * the current cat_state pointer in a static that is valid for the
 * stack frame owning this particular OpenList call.                    */
static cat_state_t *s_active_cat = NULL;

/* 1 while timer_load_more or timer_load_catalog is reopening — suppresses LIST_EXIT free */
static int s_loadmore_in_progress = 0;

static void timer_load_catalog(void); /* forward declaration */
static void timer_load_more(void);    /* forward declaration */

static int catalog_list_handler(int action, int x, int y,
                                int idx, int state)
{
    cat_state_t *cs = s_active_cat;
    if (!cs) return 0;

    /* When feed is still loading (placeholder) only handle paint and exit */
    if (!cs->feed) {
        switch (action) {
            case LIST_BEGINPAINT:
            case LIST_ENDPAINT:
                return 0;
            case LIST_PAINT:
                DrawLine(x, y + g_item_h - 1, x + g_screen_w,
                         y + g_item_h - 1, COL_LGRAY);
                SetFont(g_font_bold, COL_DGRAY);
                DrawTextRect(x, y, g_screen_w, g_item_h,
                             "Loading\u2026", ALIGN_CENTER | VALIGN_MIDDLE);
                return 0;
            case LIST_EXIT:
                if (!s_loadmore_in_progress) {
                    cat_state_t *dying = cs;
                    if (s_pending_search_state == dying)
                        s_pending_search_state = NULL;
                    s_active_cat = dying->prev;
                    opds_feed_free(dying->feed); /* feed is NULL here, safe */
                    free(dying);
                    /* Back button: redisplay the parent screen after LIST_EXIT
                     * returns — defer so InkView is in a stable state first. */
                    SetHardTimer("catback", reopen_catalog, 50);
                }
                return 0;
            default:
                return 0;
        }
    }

    (void)state;
    opds_feed_t *feed  = cs->feed;
    int          first = cs->first_entry;
    int          total = first + feed->count + cs->has_more;

    switch (action) {
        case LIST_BEGINPAINT:
            return 0;

        case LIST_PAINT: {
            DrawLine(x, y + g_item_h - 1, x + g_screen_w,
                     y + g_item_h - 1, COL_LGRAY);

            if (cs->has_search && idx == 0) {
                /* [Search] row */
                SetFont(g_font_bold, COL_DGRAY);
                DrawTextRect(x, y, g_screen_w, g_item_h,
                             "\U0001F50D  Search\u2026",
                             ALIGN_CENTER | VALIGN_MIDDLE);
            } else if (cs->has_more && idx == total - 1) {
                /* [Load more] row */
                SetFont(g_font_bold, COL_DGRAY);
                DrawTextRect(x, y, g_screen_w, g_item_h,
                             "Load more\u2026",
                             ALIGN_CENTER | VALIGN_MIDDLE);
            } else {
                int entry_idx = idx - first;
                if (entry_idx >= 0 && entry_idx < feed->count)
                    draw_entry_row(x, y, g_screen_w, g_item_h,
                                   &feed->entries[entry_idx]);
            }
            return 0;
        }

        case LIST_ENDPAINT:
            return 0;

        case LIST_OPEN: {
            if (cs->has_search && idx == 0) {
                /* Trigger search */
                s_pending_search_state = cs;
                char buf[OPDS_MAX_TITLE] = {0};
                OpenKeyboard("Search", buf, sizeof(buf) - 1,
                             KBD_NORMAL, search_kb_done);
                return 1;
            }

            int entry_idx = idx - first;

            if (cs->has_more && idx == first + feed->count) {
                /* Append next page in-place */
                SetHardTimer("ldmore", timer_load_more, 50);
                return 1;
            }

            if (!feed || entry_idx < 0 || entry_idx >= feed->count)
                return 0;

            opds_entry_t *e = &feed->entries[entry_idx];

            if (e->type == ENTRY_NAVIGATION) {
                if (!e->nav_url[0]) return 0;
                /* Call show_catalog DIRECTLY from LIST_OPEN — InkView pushes the
                 * new list onto its nav stack, so it will display ← (not ⌂).
                 * show_catalog opens a non-blocking placeholder immediately, then
                 * loads the feed asynchronously via SetHardTimer. */
                show_catalog(cs->server_idx, e->nav_url, e->title);
            } else {
                /* Acquisition: single-format → direct download; multi-format → picker */
                start_download(cs->server_idx, e);
            }
            return 1;
        }

        case LIST_EXIT:
            {
                if (!s_loadmore_in_progress) {
                    cat_state_t *dying = s_active_cat;
                    if (dying) {
                        if (s_pending_search_state == dying)
                            s_pending_search_state = NULL;
                        s_active_cat = dying->prev;
                        opds_feed_free(dying->feed);
                        free(dying);
                    }
                    /* Back button: redisplay the parent screen after LIST_EXIT
                     * returns — defer so InkView is in a stable state first. */
                    SetHardTimer("catback", reopen_catalog, 50);
                }
            }
            return 0;

        default:
            break;
    }
    (void)x; (void)y; (void)total;
    return 0;
}

/* Async feed loader — fires after show_catalog opened the placeholder list */
static void timer_load_catalog(void)
{
    cat_state_t *cs = s_active_cat;
    if (!cs || cs->feed != NULL) return;  /* nothing to do */

    opds_feed_t *feed = load_feed(cs->server_idx, cs->url);
    if (!feed) {
        Message(ICON_ERROR, "Error", "Out of memory", 2000);
        return;
    }
    if (feed->error[0]) {
        char detail[512];
        snprintf(detail, sizeof(detail), "URL: %s\n\n%s", cs->url, feed->error);
        Message(ICON_ERROR, "Error loading catalog", detail, 0);
        opds_feed_free(feed);
        return;
    }
    if (feed->count == 0) {
        char msg[256];
        snprintf(msg, sizeof(msg),
                 "The catalog returned no entries.\nURL: %s", cs->url);
        Message(ICON_INFORMATION, cs->title, msg, 0);
        opds_feed_free(feed);
        return;
    }

    cs->feed        = feed;
    cs->has_search  = feed->search_url[0] ? 1 : 0;
    cs->has_more    = feed->next_page_url[0] ? 1 : 0;
    cs->first_entry = cs->has_search ? 1 : 0;

    int total = cs->first_entry + feed->count + cs->has_more;
    s_loadmore_in_progress = 1;
    OpenList(cs->title, NULL, g_screen_w, g_item_h, total, 0, catalog_list_handler);
    SetListHeaderLevel(1);
    s_loadmore_in_progress = 0;
}

/* Append next-page entries to the current feed and reopen the list in-place */
static void timer_load_more(void)
{
    cat_state_t *cs = s_active_cat;
    if (!cs || !cs->has_more) return;

    char url[OPDS_MAX_URL];
    strncpy(url, cs->feed->next_page_url, sizeof(url) - 1);

    opds_feed_t *more = load_feed(cs->server_idx, url);
    if (!more) {
        Message(ICON_ERROR, "Error", "Out of memory", 2000);
        return;
    }
    if (more->error[0]) {
        Message(ICON_ERROR, "Load more failed", more->error, 0);
        opds_feed_free(more);
        return;
    }
    if (more->count == 0) {
        Message(ICON_INFORMATION, "Load more", "No more entries.", 2000);
        opds_feed_free(more);
        return;
    }

    int old_count = cs->feed->count;

    /* Grow entries array and append new entries */
    int new_total = cs->feed->count + more->count;
    opds_entry_t *tmp = realloc(cs->feed->entries,
                                new_total * sizeof(opds_entry_t));
    if (tmp) {
        cs->feed->entries  = tmp;
        cs->feed->capacity = new_total;
        memcpy(cs->feed->entries + cs->feed->count, more->entries,
               more->count * sizeof(opds_entry_t));
        cs->feed->count = new_total;
    }

    /* Update pagination state */
    if (more->next_page_url[0]) {
        strncpy(cs->feed->next_page_url, more->next_page_url,
                sizeof(cs->feed->next_page_url) - 1);
        cs->has_more = 1;
    } else {
        cs->feed->next_page_url[0] = '\0';
        cs->has_more = 0;
    }

    opds_feed_free(more);

    /* Reopen the same list with updated total, scrolled to first new entry.
     * Guard LIST_EXIT so it does not free cs while we reopen. */
    int total = cs->first_entry + cs->feed->count + cs->has_more;
    s_loadmore_in_progress = 1;
    OpenList(cs->title, NULL,
             g_screen_w, g_item_h,
             total,
             cs->first_entry + old_count,   /* scroll to first new entry */
             catalog_list_handler);
    SetListHeaderLevel(1);
    s_loadmore_in_progress = 0;
}

/* ── Public entry points ─────────────────────────────────────────────────── */

/* Reopen whatever catalog is now current (or the server list if none).
 * Called from a SetHardTimer callback so InkView is idle when OpenList runs. */
void reopen_catalog(void)
{
    cat_state_t *cs = s_active_cat;
    if (!cs) {
        show_server_list();
        return;
    }
    int total = cs->first_entry
                + (cs->feed ? cs->feed->count : 0)
                + cs->has_more;
    if (total == 0) total = 1;   /* safety for placeholder (feed still loading) */
    OpenList(cs->title, NULL, g_screen_w, g_item_h, total, 0, catalog_list_handler);
    SetListHeaderLevel(1);
}

void show_catalog(int server_idx, const char *url, const char *title)
{
    cat_state_t *cs = calloc(1, sizeof(*cs));
    if (!cs) { Message(ICON_ERROR, "Error", "Out of memory", 2000); return; }

    cs->server_idx = server_idx;
    strncpy(cs->title, title, sizeof(cs->title) - 1);
    strncpy(cs->url,   url,   sizeof(cs->url)   - 1);
    cs->feed = NULL;   /* loaded asynchronously by timer_load_catalog */
    cs->prev = s_active_cat;
    s_active_cat = cs;

    /* Open the placeholder list IMMEDIATELY and mark it as a nested (non-root)
     * screen so InkView shows ← instead of ⌂, regardless of call context. */
    OpenList(title, NULL, g_screen_w, g_item_h, 1, 0, catalog_list_handler);
    SetListHeaderLevel(1);

    /* Kick off the blocking network fetch after a short defer so InkView can
     * finish rendering the placeholder before we block the event loop. */
    SetHardTimer("catload", timer_load_catalog, 50);
}
