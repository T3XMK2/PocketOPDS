#ifndef POCKETOPDS_NET_H
#define POCKETOPDS_NET_H

/* ── Network helper ──────────────────────────────────────────────────────────
 *
 * Wraps libcurl for HTTP(S) GET requests with optional Basic-Auth.
 * WiFi is connected automatically via InkView's NetConnect before the first
 * request; subsequent requests reuse the connection.
 *
 * All functions are SYNCHRONOUS (blocking).  Call them from a timer callback
 * or after the user triggers an action; never from the main event handler
 * directly (it would block repaints).
 * ─────────────────────────────────────────────────────────────────────────── */

/* HTTP response buffer.  Caller must call net_response_free() when done. */
typedef struct {
    char *data;      /* NUL-terminated response body */
    long  size;      /* byte count, not including NUL */
    int   http_code; /* e.g. 200, 401, 404 … */
    char  error[256];/* libcurl error message if data == NULL */
    char  content_disposition[512]; /* raw Content-Disposition header value */
} net_response_t;

/* Initialise libcurl global state.  Call once at app start (EVT_INIT). */
void net_init(void);

/* Clean up libcurl.  Call at EVT_EXIT. */
void net_cleanup(void);

/* Ensure WiFi is up.  Blocks until connected or timeout (seconds).
 * Returns 1 on success, 0 on failure.                                  */
int net_wifi_ensure(int timeout_sec);

/* Perform an HTTP GET.
 *   url      - full URL (http:// or https://)
 *   username - may be NULL for anonymous access
 *   password - may be NULL
 *   Returns a heap-allocated net_response_t; always free with net_response_free(). */
net_response_t *net_get(const char *url,
                        const char *username,
                        const char *password);

/* Free a response returned by net_get(). */
void net_response_free(net_response_t *r);

/* Stream a file directly to disk — avoids loading large files into RAM.
 * Returns the HTTP response code (200 on success).
 * If content_disp_out is not NULL, the Content-Disposition header value
 * is copied there (up to cd_size bytes). */
int net_download_to_file(const char *url,
                         const char *username,
                         const char *password,
                         const char *dest_path,
                         char *content_disp_out, size_t cd_size,
                         char *err_out, size_t err_size);

#endif /* POCKETOPDS_NET_H */
