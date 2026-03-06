#ifndef POCKETOPDS_OPDS_H
#define POCKETOPDS_OPDS_H

/* ── OPDS Atom feed fetcher + parser ────────────────────────────────────────
 *
 * Fetches an OPDS Atom URL and returns a list of entries.
 *
 * OPDS entry types:
 *   ENTRY_NAVIGATION  – leads to another catalog page (subsection / start)
 *   ENTRY_ACQUISITION – a downloadable book (epub, pdf, …)
 *
 * Usage:
 *   opds_feed_t *feed = opds_fetch(url, username, password);
 *   if (feed) {
 *       for (int i = 0; i < feed->count; i++) { ... }
 *       opds_feed_free(feed);
 *   }
 * ─────────────────────────────────────────────────────────────────────────── */

#define OPDS_MAX_TITLE    256
#define OPDS_MAX_URL      512
#define OPDS_MAX_AUTHOR   128
#define OPDS_MAX_SUMMARY  512
#define OPDS_MAX_MIME      64

typedef enum {
    ENTRY_NAVIGATION,    /* subsection link → open another catalog page */
    ENTRY_ACQUISITION,   /* download link → book file                   */
} opds_entry_type_t;

typedef struct {
    char title       [OPDS_MAX_TITLE];
    char author      [OPDS_MAX_AUTHOR];
    char summary     [OPDS_MAX_SUMMARY];

    /* For ENTRY_NAVIGATION: URL of the sub-catalog */
    char nav_url     [OPDS_MAX_URL];

    /* For ENTRY_ACQUISITION: best download URL + MIME type */
    char download_url [OPDS_MAX_URL];
    char download_mime[OPDS_MAX_MIME];

    /* Alternate/additional download formats (epub preferred, then pdf) */
    char alt_url     [OPDS_MAX_URL];
    char alt_mime    [OPDS_MAX_MIME];

    opds_entry_type_t type;
} opds_entry_t;

typedef struct {
    char feed_title    [OPDS_MAX_TITLE];
    char search_url    [OPDS_MAX_URL];    /* OpenSearch template URL */
    char next_page_url [OPDS_MAX_URL];   /* "next" pagination link  */
    char base_url      [OPDS_MAX_URL];   /* base for resolving relative hrefs */

    opds_entry_t *entries;
    int           count;
    int           capacity;

    char error[256];   /* non-empty if fetch/parse failed */
} opds_feed_t;

/* Fetch and parse an OPDS feed.  Returns NULL only on allocation failure.
 * On HTTP / parse errors the returned feed has feed->error set and count==0.
 * Always free with opds_feed_free(). */
opds_feed_t *opds_fetch(const char *url,
                        const char *username,
                        const char *password);

/* Resolve a (possibly relative) href against the feed's base_url.
 * Writes result into out_url[OPDS_MAX_URL].                         */
void opds_resolve_url(const opds_feed_t *feed, const char *href,
                      char *out_url, size_t out_size);

/* Free a feed returned by opds_fetch(). */
void opds_feed_free(opds_feed_t *feed);

/* Return a short uppercase format label for a MIME type (e.g. "EPUB", "PDF"). */
const char *opds_mime_short(const char *mime);

#endif /* POCKETOPDS_OPDS_H */
