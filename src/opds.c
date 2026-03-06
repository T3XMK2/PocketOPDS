#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <expat.h>
#include "opds.h"
#include "net.h"

/* ── Namespace URIs we care about ────────────────────────────────────────── */
#define NS_ATOM  "http://www.w3.org/2005/Atom"
#define NS_SEP   '|'           /* expat namespace separator character */

/* ── Known OPDS acquisition rel values ──────────────────────────────────── */
static const char *ACQUISITION_RELS[] = {
    "http://opds-spec.org/acquisition",
    "http://opds-spec.org/acquisition/open-access",
    "http://opds-spec.org/acquisition/borrow",
    "http://opds-spec.org/acquisition/buy",
    "http://opds-spec.org/acquisition/sample",
    NULL
};

/* Preferred MIME types in order (lower index = higher priority) */
static const char *PREFERRED_MIME[] = {
    "application/epub+zip",
    "application/pdf",
    "application/x-mobipocket-ebook",
    "application/vnd.amazon.ebook",
    "application/x-cbz",
    "application/x-cbr",
    "application/fb2",
    "application/x-fb2",
    "image/vnd.djvu",
    "text/plain",
    NULL
};

/* ── Parser state ─────────────────────────────────────────────────────────── */

typedef enum {
    CTX_NONE,
    CTX_FEED,
    CTX_FEED_TITLE,
    CTX_FEED_LINK,
    CTX_ENTRY,
    CTX_ENTRY_TITLE,
    CTX_ENTRY_AUTHOR_NAME,
    CTX_ENTRY_SUMMARY,
    CTX_ENTRY_CONTENT,
    CTX_ENTRY_LINK,
} parse_ctx_t;

typedef struct {
    opds_feed_t *feed;
    opds_entry_t cur;          /* entry being built */
    int          in_entry;
    int          in_author;
    parse_ctx_t  ctx;
    char         charbuf[4096];
    size_t       charlen;
} parse_state_t;

/* ── Helpers ──────────────────────────────────────────────────────────────── */

/* Strip "namespace|" prefix to get local name. */
static const char *local_name(const char *full)
{
    const char *p = strchr(full, NS_SEP);
    return p ? p + 1 : full;
}

static int str_starts(const char *s, const char *prefix)
{
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

static int is_acquisition_rel(const char *rel)
{
    if (!rel) return 0;
    for (int i = 0; ACQUISITION_RELS[i]; i++)
        if (strcmp(rel, ACQUISITION_RELS[i]) == 0) return 1;
    return 0;
}

static int mime_priority(const char *mime)
{
    if (!mime) return 99;
    for (int i = 0; PREFERRED_MIME[i]; i++)
        if (strcmp(mime, PREFERRED_MIME[i]) == 0) return i;
    return 50;
}

/* Grab attribute value by name (NULL if not found). */
static const char *attr_get(const XML_Char **attrs, const char *name)
{
    for (int i = 0; attrs[i]; i += 2)
        if (strcmp(local_name(attrs[i]), name) == 0)
            return attrs[i + 1];
    return NULL;
}

static void flush_charbuf(parse_state_t *ps)
{
    ps->charbuf[ps->charlen] = '\0';
}

/* Append a heap-allocated entry to the feed, growing as needed. */
static void feed_push_entry(opds_feed_t *feed, const opds_entry_t *e)
{
    if (feed->count >= feed->capacity) {
        int newcap = feed->capacity ? feed->capacity * 2 : 32;
        opds_entry_t *tmp = realloc(feed->entries,
                                    newcap * sizeof(opds_entry_t));
        if (!tmp) return;
        feed->entries   = tmp;
        feed->capacity  = newcap;
    }
    feed->entries[feed->count++] = *e;
}

/* ── Expat handlers ───────────────────────────────────────────────────────── */

static void XMLCALL on_start(void *ud, const XML_Char *name,
                             const XML_Char **attrs)
{
    parse_state_t *ps  = (parse_state_t *)ud;
    const char    *loc = local_name(name);
    ps->charlen = 0;

    if (strcmp(loc, "feed") == 0 && !ps->in_entry) {
        ps->ctx = CTX_FEED;
        return;
    }

    if (strcmp(loc, "entry") == 0) {
        memset(&ps->cur, 0, sizeof(ps->cur));
        ps->cur.type = ENTRY_NAVIGATION;  /* default */
        ps->in_entry = 1;
        ps->ctx      = CTX_ENTRY;
        return;
    }

    /* ── Inside <feed> (but not inside <entry>) ── */
    if (!ps->in_entry) {
        if (strcmp(loc, "title") == 0) {
            ps->ctx = CTX_FEED_TITLE;
        } else if (strcmp(loc, "link") == 0) {
            const char *rel  = attr_get(attrs, "rel");
            const char *href = attr_get(attrs, "href");
            if (!href) return;

            if (rel && strcmp(rel, "search") == 0)
                strncpy(ps->feed->search_url, href,
                        sizeof(ps->feed->search_url) - 1);
            else if (rel && strcmp(rel, "next") == 0)
                strncpy(ps->feed->next_page_url, href,
                        sizeof(ps->feed->next_page_url) - 1);
        }
        return;
    }

    /* ── Inside <entry> ── */
    if (strcmp(loc, "title") == 0) {
        ps->ctx = CTX_ENTRY_TITLE;
    } else if (strcmp(loc, "author") == 0) {
        ps->in_author = 1;
    } else if (ps->in_author && strcmp(loc, "name") == 0) {
        ps->ctx = CTX_ENTRY_AUTHOR_NAME;
    } else if (strcmp(loc, "summary") == 0) {
        ps->ctx = CTX_ENTRY_SUMMARY;
    } else if (strcmp(loc, "content") == 0) {
        ps->ctx = CTX_ENTRY_CONTENT;
    } else if (strcmp(loc, "link") == 0) {
        const char *rel   = attr_get(attrs, "rel");
        const char *href  = attr_get(attrs, "href");
        const char *type  = attr_get(attrs, "type");
        if (!href) return;

        if (is_acquisition_rel(rel)) {
            /* Book download link */
            ps->cur.type = ENTRY_ACQUISITION;
            int cur_prio = mime_priority(ps->cur.download_mime);
            int new_prio = mime_priority(type);
            if (!ps->cur.download_url[0] || new_prio < cur_prio) {
                /* Better or first download link */
                if (ps->cur.download_url[0]) {
                    /* Demote current to alt */
                    strncpy(ps->cur.alt_url, ps->cur.download_url,
                            sizeof(ps->cur.alt_url) - 1);
                    strncpy(ps->cur.alt_mime, ps->cur.download_mime,
                            sizeof(ps->cur.alt_mime) - 1);
                }
                strncpy(ps->cur.download_url, href,
                        sizeof(ps->cur.download_url) - 1);
                if (type)
                    strncpy(ps->cur.download_mime, type,
                            sizeof(ps->cur.download_mime) - 1);
            } else if (!ps->cur.alt_url[0]) {
                strncpy(ps->cur.alt_url, href,
                        sizeof(ps->cur.alt_url) - 1);
                if (type)
                    strncpy(ps->cur.alt_mime, type,
                            sizeof(ps->cur.alt_mime) - 1);
            }
        } else if (rel && (strcmp(rel, "subsection") == 0
                        || strcmp(rel, "http://opds-spec.org/featured") == 0
                        || strcmp(rel, "http://opds-spec.org/recommended") == 0
                        || str_starts(rel, "http://opds-spec.org/sort"))) {
            /* Navigation link with explicit rel */
            if (!ps->cur.nav_url[0])
                strncpy(ps->cur.nav_url, href,
                        sizeof(ps->cur.nav_url) - 1);
        } else if (!ps->cur.nav_url[0] && type &&
                   str_starts(type, "application/atom+xml")) {
            /* Navigation link identified by MIME type alone (Calibre style:
             * no rel attribute, but type="application/atom+xml;type=feed…") */
            strncpy(ps->cur.nav_url, href, sizeof(ps->cur.nav_url) - 1);
        }
    }
}

static void XMLCALL on_end(void *ud, const XML_Char *name)
{
    parse_state_t *ps  = (parse_state_t *)ud;
    const char    *loc = local_name(name);
    flush_charbuf(ps);

    if (strcmp(loc, "entry") == 0 && ps->in_entry) {
        /* Only add entries that have at least a title */
        if (ps->cur.title[0])
            feed_push_entry(ps->feed, &ps->cur);
        ps->in_entry  = 0;
        ps->in_author = 0;
        ps->ctx       = CTX_FEED;
        return;
    }

    if (strcmp(loc, "author") == 0)
        ps->in_author = 0;

    switch (ps->ctx) {
        case CTX_FEED_TITLE:
            strncpy(ps->feed->feed_title, ps->charbuf,
                    sizeof(ps->feed->feed_title) - 1);
            ps->ctx = CTX_FEED;
            break;
        case CTX_ENTRY_TITLE:
            strncpy(ps->cur.title, ps->charbuf,
                    sizeof(ps->cur.title) - 1);
            ps->ctx = CTX_ENTRY;
            break;
        case CTX_ENTRY_AUTHOR_NAME:
            strncpy(ps->cur.author, ps->charbuf,
                    sizeof(ps->cur.author) - 1);
            ps->ctx = CTX_ENTRY;
            break;
        case CTX_ENTRY_SUMMARY:
        case CTX_ENTRY_CONTENT:
            /* Use content if summary is empty */
            if (!ps->cur.summary[0])
                strncpy(ps->cur.summary, ps->charbuf,
                        sizeof(ps->cur.summary) - 1);
            ps->ctx = CTX_ENTRY;
            break;
        default:
            break;
    }

    ps->charlen = 0;
}

static void XMLCALL on_char(void *ud, const XML_Char *s, int len)
{
    parse_state_t *ps = (parse_state_t *)ud;
    size_t avail = sizeof(ps->charbuf) - ps->charlen - 1;
    if ((size_t)len > avail) len = (int)avail;
    if (len <= 0) return;
    memcpy(ps->charbuf + ps->charlen, s, len);
    ps->charlen += len;
}

/* ── URL helpers ─────────────────────────────────────────────────────────── */

/* Extract scheme+host+port from a URL into buf[]. */
static void url_base(const char *url, char *buf, size_t sz)
{
    /* Find end of "scheme://host[:port]" */
    const char *proto_end = strstr(url, "://");
    if (!proto_end) { strncpy(buf, url, sz - 1); return; }
    proto_end += 3;
    const char *path_start = strchr(proto_end, '/');
    if (!path_start) { strncpy(buf, url, sz - 1); return; }
    size_t base_len = path_start - url;
    if (base_len >= sz) base_len = sz - 1;
    memcpy(buf, url, base_len);
    buf[base_len] = '\0';
}

void opds_resolve_url(const opds_feed_t *feed, const char *href,
                      char *out_url, size_t out_size)
{
    if (!href || !href[0]) { out_url[0] = '\0'; return; }

    /* Already absolute */
    if (str_starts(href, "http://") || str_starts(href, "https://")) {
        strncpy(out_url, href, out_size - 1);
        return;
    }

    char base[OPDS_MAX_URL];
    url_base(feed->base_url, base, sizeof(base));

    if (href[0] == '/') {
        snprintf(out_url, out_size, "%s%s", base, href);
    } else {
        /* Relative path: append to directory of base_url */
        char dir[OPDS_MAX_URL];
        strncpy(dir, feed->base_url, sizeof(dir) - 1);
        char *slash = strrchr(dir, '/');
        if (slash) *(slash + 1) = '\0';
        snprintf(out_url, out_size, "%s%s", dir, href);
    }
}

/* ── Public API ──────────────────────────────────────────────────────────── */

opds_feed_t *opds_fetch(const char *url,
                        const char *username,
                        const char *password)
{
    opds_feed_t *feed = calloc(1, sizeof(*feed));
    if (!feed) return NULL;

    /* Normalize: ensure URL has a scheme so url_base() resolves correctly */
    char norm_url[OPDS_MAX_URL];
    if (!strstr(url, "://")) {
        snprintf(norm_url, sizeof(norm_url), "http://%s", url);
        url = norm_url;
    }

    /* Store base URL for relative-href resolution */
    strncpy(feed->base_url, url, sizeof(feed->base_url) - 1);

    /* ── Fetch via libcurl ── */
    net_response_t *resp = net_get(url, username, password);
    if (!resp) {
        snprintf(feed->error, sizeof(feed->error), "net_get allocation failed");
        return feed;
    }
    if (!resp->data) {
        snprintf(feed->error, sizeof(feed->error),
                 "HTTP error %ld: %s", (long)resp->http_code,
                 resp->error[0] ? resp->error : "(unknown)");
        net_response_free(resp);
        return feed;
    }
    if (resp->http_code == 401) {
        snprintf(feed->error, sizeof(feed->error),
                 "Authentication required (HTTP 401). "
                 "Check username and password.");
        net_response_free(resp);
        return feed;
    }
    if (resp->http_code < 200 || resp->http_code >= 300) {
        /* Include first line of body to show server's reason */
        char snippet[81] = {0};
        if (resp->data && resp->size > 0) {
            strncpy(snippet, resp->data, sizeof(snippet) - 1);
            char *nl = strchr(snippet, '\n'); if (nl) *nl = '\0';
            char *cr = strchr(snippet, '\r'); if (cr) *cr = '\0';
            /* Strip HTML tags for readability */
            char clean[81] = {0}; int ci = 0; int in_tag = 0;
            for (int i = 0; snippet[i] && ci < 79; i++) {
                if (snippet[i] == '<') { in_tag = 1; continue; }
                if (snippet[i] == '>') { in_tag = 0; continue; }
                if (!in_tag) clean[ci++] = snippet[i];
            }
            snprintf(feed->error, sizeof(feed->error),
                     "HTTP %d: %s", resp->http_code,
                     clean[0] ? clean : "(no body)");
        } else {
            snprintf(feed->error, sizeof(feed->error),
                     "HTTP %d (empty response)", resp->http_code);
        }
        net_response_free(resp);
        return feed;
    }

    /* ── Parse with expat (namespace-aware) ── */
    XML_Parser p = XML_ParserCreateNS(NULL, NS_SEP);
    if (!p) {
        snprintf(feed->error, sizeof(feed->error), "XML_ParserCreateNS failed");
        net_response_free(resp);
        return feed;
    }

    parse_state_t ps;
    memset(&ps, 0, sizeof(ps));
    ps.feed = feed;

    XML_SetUserData(p, &ps);
    XML_SetElementHandler(p, on_start, on_end);
    XML_SetCharacterDataHandler(p, on_char);

    if (XML_Parse(p, resp->data, (int)resp->size, 1) == XML_STATUS_ERROR) {
        snprintf(feed->error, sizeof(feed->error),
                 "XML parse error at line %lu: %s",
                 (unsigned long)XML_GetCurrentLineNumber(p),
                 XML_ErrorString(XML_GetErrorCode(p)));
    }

    XML_ParserFree(p);
    net_response_free(resp);
    return feed;
}

void opds_feed_free(opds_feed_t *feed)
{
    if (!feed) return;
    free(feed->entries);
    free(feed);
}

const char *opds_mime_short(const char *mime)
{
    if (!mime || !mime[0])                               return "";
    if (strstr(mime, "epub"))                            return "EPUB";
    if (strstr(mime, "pdf"))                             return "PDF";
    if (strstr(mime, "mobi") || strstr(mime, "mobipocket")) return "MOBI";
    if (strstr(mime, "amazon.ebook"))                    return "AZW";
    if (strstr(mime, "fb2"))                             return "FB2";
    if (strstr(mime, "djvu"))                            return "DJVU";
    if (strstr(mime, "cbz"))                             return "CBZ";
    if (strstr(mime, "cbr"))                             return "CBR";
    if (strstr(mime, "text"))                            return "TXT";
    return "FILE";
}
