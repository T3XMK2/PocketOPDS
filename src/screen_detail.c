#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <inkview.h>
#include "app.h"
#include "opds.h"
#include "config.h"
#include "net.h"

/* ─────────────────────────────────────────────────────────────────────────────
 * Book detail & download screen
 *
 * Shows: Title, Author, Summary/description
 * Buttons shown as list items:
 *   [Download … (epub)]     ← best format
 *   [Download … (pdf)]      ← alternate format, if available
 *   [Back]
 * ─────────────────────────────────────────────────────────────────────────── */

#define ROW_PAD    12

/* Download destination root */
#define BOOKS_DIR  FLASHDIR "/Books"

/* ── State for the current detail view ───────────────────────────────────── */

typedef struct detail_state_s {
    int                    server_idx;
    opds_entry_t           entry;
    struct detail_state_s *prev;
} detail_state_t;

static detail_state_t *s_active_detail = NULL;

/* ── Determine book filename ─────────────────────────────────────────────── */

/* Sanitize a string for use as a filename component. */
static void sanitize_filename(const char *src, char *out, size_t out_size)
{
    size_t j = 0;
    for (size_t i = 0; src[i] && j < out_size - 1; i++) {
        char c = src[i];
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
            c == '"' || c == '<'  || c == '>'  || c == '|')
            c = '_';
        out[j++] = c;
    }
    out[j] = '\0';
}

/* Try to extract filename from Content-Disposition: attachment; filename="x" */
static int parse_content_disposition(const char *cd, char *out, size_t out_size)
{
    if (!cd || !cd[0]) return 0;
    /* Case-insensitive search for "filename=" */
    const char *p = cd;
    while (*p) {
        if (strncasecmp(p, "filename=", 9) == 0) { p += 9; break; }
        p++;
    }
    if (!*p) return 0;
    /* Skip optional encoding prefix like UTF-8''  */
    if (strncmp(p, "UTF-8''", 7) == 0 || strncmp(p, "utf-8''", 7) == 0)
        p += 7;
    /* Strip surrounding quotes */
    if (*p == '"') {
        p++;
        const char *end = strchr(p, '"');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        if (len >= out_size) len = out_size - 1;
        memcpy(out, p, len);
        out[len] = '\0';
    } else {
        /* No quotes: read until ; or whitespace */
        size_t len = 0;
        while (p[len] && p[len] != ';' && p[len] != ' ' && p[len] != '\r')
            len++;
        if (len >= out_size) len = out_size - 1;
        memcpy(out, p, len);
        out[len] = '\0';
    }
    return out[0] != '\0';
}

static const char *mime_to_ext(const char *mime)
{
    if (!mime || !mime[0]) return ".bin";
    /* Check epub before zip — application/epub+zip contains "zip" */
    if (strstr(mime, "epub"))                              return ".epub";
    if (strstr(mime, "pdf"))                               return ".pdf";
    if (strstr(mime, "mobi") || strstr(mime, "mobipocket")) return ".mobi";
    if (strstr(mime, "amazon.ebook"))                      return ".azw";
    if (strstr(mime, "fb2"))                               return ".fb2";
    if (strstr(mime, "djvu"))                              return ".djvu";
    if (strstr(mime, "cbz"))                               return ".cbz";
    if (strstr(mime, "cbr"))                               return ".cbr";
    if (strstr(mime, "zip"))                               return ".zip";
    if (strstr(mime, "text"))                              return ".txt";
    return ".bin";
}


/* ── Perform the download ────────────────────────────────────────────────── */

static void do_download(detail_state_t *ds, const char *dl_url,
                        const char *dl_mime)
{
    if (!dl_url || !dl_url[0]) {
        Message(ICON_WARNING, "Download", "No download URL available.", 2000);
        return;
    }

    if (!net_wifi_ensure(30)) {
        Message(ICON_ERROR, "Network error", "Cannot connect to Wi-Fi.", 0);
        return;
    }

    /* Build author subfolder: Books/<Author>/ or Books/Unknown Author/ */
    char safe_author[128];
    if (ds->entry.author[0])
        sanitize_filename(ds->entry.author, safe_author, sizeof(safe_author));
    else
        strncpy(safe_author, "Unknown Author", sizeof(safe_author) - 1);

    char dest_dir[384];
    snprintf(dest_dir, sizeof(dest_dir), "%s/%s", BOOKS_DIR, safe_author);
    iv_mkdir(BOOKS_DIR,   0755);
    iv_mkdir(dest_dir,    0755);

    /* Build tentative filename from title + MIME */
    char filename[256];
    if (ds->entry.title[0]) {
        char safe[200];
        sanitize_filename(ds->entry.title, safe, sizeof(safe));
        snprintf(filename, sizeof(filename), "%s%s", safe, mime_to_ext(dl_mime));
    } else {
        snprintf(filename, sizeof(filename), "book%s", mime_to_ext(dl_mime));
    }

    char dest_path[512];
    snprintf(dest_path, sizeof(dest_path), "%s/%s", dest_dir, filename);

    /* Show progress bar and flush to screen before blocking */
    OpenProgressbar(ICON_INFORMATION, "Downloading",
                    ds->entry.title[0] ? ds->entry.title : "Book",
                    0, NULL);
    FullUpdate();
    UpdateProgressbar("Downloading\u2026", 10);

    char cd_hdr[512] = {0};
    char err[300]    = {0};
    server_t *s = &g_servers[ds->server_idx];

    int code = net_download_to_file(dl_url, s->username, s->password,
                                    dest_path, cd_hdr, sizeof(cd_hdr),
                                    err, sizeof(err));

    CloseProgressbar();
    FullUpdate();

    if (code < 200 || code >= 300) {
        unlink(dest_path);   /* remove partial/empty file */
        char errmsg[400];
        snprintf(errmsg, sizeof(errmsg), "HTTP %d%s%s",
                 code, err[0] ? ": " : "", err);
        Message(ICON_ERROR, "Download failed", errmsg, 0);
        return;
    }
    if (err[0]) {   /* curl error */
        unlink(dest_path);   /* remove partial/empty file */
        Message(ICON_ERROR, "Download failed", err, 0);
        return;
    }

    /* If server sent Content-Disposition with a better filename, rename
     * within the same author subfolder. */
    char cd_name[256] = {0};
    if (parse_content_disposition(cd_hdr, cd_name, sizeof(cd_name))
            && cd_name[0]) {
        char safe_cd[256];
        sanitize_filename(cd_name, safe_cd, sizeof(safe_cd));
        char new_path[512];
        snprintf(new_path, sizeof(new_path), "%s/%s", dest_dir, safe_cd);
        if (strcmp(dest_path, new_path) != 0)
            rename(dest_path, new_path);
        strncpy(dest_path, new_path, sizeof(dest_path) - 1);
    }

    /* Flush filesystem buffers, then tell the library scanner to pick up
     * the new file.  EVT_STARTSCAN sent to OTHERTASKS causes the firmware's
     * background scanner process to rescan immediately — no reboot needed. */
    iv_sync();
    SendEventTo(OTHERTASKS, EVT_STARTSCAN, 0, 0);

    char success_msg[512];
    snprintf(success_msg, sizeof(success_msg), "Saved to:\n%s", dest_path);
    Message(ICON_INFORMATION, "Download complete", success_msg, 3000);
}

/* ── Row layout helpers ─────────────────────────────────────────────── */
/*
 * idx 0            — title + author (always)
 * idx 1            — summary      (only if e->summary[0])
 * idx first_dl     — primary download
 * idx first_dl+1   — alternate download (only if e->alt_url[0])
 */
static int detail_has_summary(const detail_state_t *ds)
{
    return ds->entry.summary[0] ? 1 : 0;
}
/* Index of the alternate-format download row (only present if alt_url[0]) */
static int detail_alt_idx(const detail_state_t *ds)
{
    return 1 + detail_has_summary(ds);
}
static int detail_item_count(const detail_state_t *ds)
{
    return 1 + detail_has_summary(ds) + (ds->entry.alt_url[0] ? 1 : 0);
}

static void draw_detail_row(int x, int y, int w, int h,
                            int idx, const detail_state_t *ds)
{
    const opds_entry_t *e = &ds->entry;

    DrawLine(x, y + h - 1, x + w, y + h - 1, COL_LGRAY);

    if (idx == 0) {
        /* ── Title row: title left, [FORMAT] badge on the same line as title ── */
        char badge[32];
        snprintf(badge, sizeof(badge), "[%s]", opds_mime_short(e->download_mime));

        SetFont(g_font_small, COL_DGRAY);
        int bw = StringWidth(badge) + ROW_PAD;
        DrawTextRect(x + w - bw - ROW_PAD, y + ROW_PAD,
                     bw, g_font_size + 4,
                     badge, ALIGN_RIGHT | VALIGN_MIDDLE);

        int text_w = w - ROW_PAD * 2 - bw - ROW_PAD;
        SetFont(g_font_bold, COL_BLACK);
        DrawTextRect(x + ROW_PAD, y + ROW_PAD,
                     text_w, g_font_size + 4,
                     e->title, ALIGN_LEFT | DOTS);

        if (e->author[0]) {
            SetFont(g_font_small, COL_DGRAY);
            DrawTextRect(x + ROW_PAD, y + ROW_PAD + g_font_size + 6,
                         w - ROW_PAD * 2, g_font_size + 2,
                         e->author, ALIGN_LEFT | DOTS);
        }

    } else if (detail_has_summary(ds) && idx == 1) {
        /* ── Summary row (informational, not tappable) ── */
        SetFont(g_font_small, COL_DGRAY);
        DrawTextRect(x + ROW_PAD, y + ROW_PAD / 2,
                     w - ROW_PAD * 2, h - ROW_PAD,
                     e->summary, ALIGN_LEFT | VALIGN_TOP | DOTS);

    } else {
        /* ── Alternate download row ── */
        char line[64];
        snprintf(line, sizeof(line), "\u2193 %s  \u2014  alternate format",
                 opds_mime_short(e->alt_mime));
        SetFont(g_font_body, COL_BLACK);
        DrawTextRect(x + ROW_PAD, y, w - ROW_PAD * 2, h,
                     line, ALIGN_LEFT | VALIGN_MIDDLE);
    }
}
/* Pending deferred download (set in LIST_OPEN, consumed by timer) */
static int  s_dl_btn = -1;   /* 0=primary, 1=alt */

static void timer_do_download(void)
{
    detail_state_t *ds = s_active_detail;
    if (!ds || s_dl_btn < 0) return;
    int btn = s_dl_btn;
    s_dl_btn = -1;
    const char *url  = (btn == 0) ? ds->entry.download_url : ds->entry.alt_url;
    const char *mime = (btn == 0) ? ds->entry.download_mime : ds->entry.alt_mime;
    do_download(ds, url, mime);
}
/* ── List handler ─────────────────────────────────────────────────────────── */

static int detail_list_handler(int action, int x, int y, int idx, int state)
{
    (void)state;
    detail_state_t *ds = s_active_detail;
    if (!ds) return 0;

    /* Row heights:
     *   idx 0  →  header row (taller)
     *   idx 1  →  summary row (taller)
     *   idx ≥2 →  button rows (standard height)
     * InkView OpenList uses a uniform item height.  We use g_item_h * 3 for the
     * header row and g_item_h * 4 for the summary by drawing them in the same
     * total space allocated.  For simplicity we'll use a single item height and
     * let text wrap work.                                                       */

    switch (action) {
        case LIST_BEGINPAINT:
            return 0;

        case LIST_PAINT:
            draw_detail_row(x, y, g_screen_w, g_item_h, idx, ds);
            return 0;

        case LIST_ENDPAINT:
            return 0;

        case LIST_OPEN: {
            int ai = detail_alt_idx(ds);
            if (idx == 0 && ds->entry.download_url[0]) {
                s_dl_btn = 0;
                SetHardTimer("dlstart", timer_do_download, 50);
            } else if (ds->entry.alt_url[0] && idx == ai) {
                s_dl_btn = 1;
                SetHardTimer("dlstart", timer_do_download, 50);
            }
            return 1;
        }

        case LIST_EXIT:
            {
                detail_state_t *dying = s_active_detail;
                if (dying) {
                    s_active_detail = dying->prev;
                    free(dying);
                }
                /* Back button: redisplay the catalog beneath this detail screen.
                 * Defer so InkView is in a stable state when OpenList is called. */
                SetHardTimer("detback", reopen_catalog, 50);
            }
            return 0;

        default:
            break;
    }
    (void)x; (void)y;
    return 0;
}

/* ── Public entry points ─────────────────────────────────────────────────── */

/* State for a one-shot download that bypasses the detail screen */
static detail_state_t *s_quick_dl_state = NULL;

static void timer_quick_download(void)
{
    detail_state_t *ds = s_quick_dl_state;
    s_quick_dl_state = NULL;
    if (!ds) return;
    do_download(ds, ds->entry.download_url, ds->entry.download_mime);
    free(ds);
    /* Redisplay the catalog after the download (and any progress/message dialogs) */
    reopen_catalog();
}

void start_download(int server_idx, const opds_entry_t *entry)
{
    if (!entry->alt_url[0]) {
        /* Single format — download immediately, no extra screen */
        detail_state_t *ds = calloc(1, sizeof(*ds));
        if (!ds) { Message(ICON_ERROR, "Error", "Out of memory", 2000); return; }
        ds->server_idx = server_idx;
        ds->entry      = *entry;
        s_quick_dl_state = ds;
        SetHardTimer("qdl", timer_quick_download, 50);
    } else {
        /* Multiple formats — open the format-picker detail screen */
        show_detail(server_idx, entry);
    }
}

void show_detail(int server_idx, const opds_entry_t *entry)
{
    detail_state_t *ds = calloc(1, sizeof(*ds));
    if (!ds) { Message(ICON_ERROR, "Error", "Out of memory", 2000); return; }
    ds->server_idx = server_idx;
    ds->entry      = *entry;   /* copy */

    detail_state_t *prev = s_active_detail;
    ds->prev        = prev;
    s_active_detail  = ds;

    OpenList(entry->title[0] ? entry->title : "Book detail",
             NULL,
             g_screen_w,
             g_item_h,
             detail_item_count(ds),
             0,
             detail_list_handler);
    SetListHeaderLevel(1);
    /* Do NOT restore s_active_detail or free ds here —
     * OpenList is non-blocking; LIST_EXIT does cleanup. */
}
