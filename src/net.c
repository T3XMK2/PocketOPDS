#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <curl/curl.h>
#include <inkview.h>
#include "net.h"
#include "config.h"   /* MAX_CRED_LEN */

/* ── libcurl write callback ──────────────────────────────────────────────── */

typedef struct {
    char  *buf;
    size_t len;
    size_t capacity;
} write_ctx_t;

static size_t write_cb(void *ptr, size_t size, size_t nmemb, void *userdata)
{
    write_ctx_t *ctx  = (write_ctx_t *)userdata;
    size_t        n   = size * nmemb;
    size_t        need = ctx->len + n + 1;       /* +1 for NUL */

    if (need > ctx->capacity) {
        size_t newcap = ctx->capacity ? ctx->capacity : 4096;
        while (newcap < need) newcap *= 2;
        char *tmp = realloc(ctx->buf, newcap);
        if (!tmp) return 0;                       /* abort transfer */
        ctx->buf      = tmp;
        ctx->capacity = newcap;
    }

    memcpy(ctx->buf + ctx->len, ptr, n);
    ctx->len         += n;
    ctx->buf[ctx->len] = '\0';
    return n;
}

/* Capture Content-Disposition header */
static size_t header_cb(void *ptr, size_t size, size_t nmemb, void *userdata)
{
    net_response_t *resp = (net_response_t *)userdata;
    size_t n = size * nmemb;
    const char *hdr = (const char *)ptr;
    const char *prefix = "Content-Disposition:";
    if (strncasecmp(hdr, prefix, strlen(prefix)) == 0) {
        const char *val = hdr + strlen(prefix);
        while (*val == ' ') val++;
        size_t vlen = n - (val - hdr);
        /* strip trailing \r\n */
        while (vlen > 0 && (val[vlen-1] == '\r' || val[vlen-1] == '\n'))
            vlen--;
        if (vlen >= sizeof(resp->content_disposition))
            vlen = sizeof(resp->content_disposition) - 1;
        memcpy(resp->content_disposition, val, vlen);
        resp->content_disposition[vlen] = '\0';
    }
    return n;
}

void net_init(void)
{
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

void net_cleanup(void)
{
    curl_global_cleanup();
}

int net_wifi_ensure(int timeout_sec)
{
    iv_netinfo *info = NetInfo();
    if (info && info->connected) return 1;

    /* Ask InkView to connect using the default/remembered Wi-Fi profile */
    int ret = NetConnect(NULL);           /* NULL = default profile */
    if (ret != 0) return 0;

    /* Poll until connected or timeout */
    for (int i = 0; i < timeout_sec * 10; i++) {
        info = NetInfo();
        if (info && info->connected) return 1;
        usleep(100000);                   /* 100 ms */
    }
    return 0;
}

net_response_t *net_get(const char *url,
                        const char *username,
                        const char *password)
{
    net_response_t *resp = calloc(1, sizeof(*resp));
    if (!resp) return NULL;

    CURL       *curl = curl_easy_init();
    write_ctx_t ctx  = {0};
    char        errbuf[CURL_ERROR_SIZE] = {0};

    if (!curl) {
        snprintf(resp->error, sizeof(resp->error), "curl_easy_init failed");
        return resp;
    }

    /* Build optional "user:password" credential string */
    char userpwd[MAX_CRED_LEN * 2 + 2] = {0};
    if (username && username[0]) {
        snprintf(userpwd, sizeof(userpwd), "%s:%s",
                 username, password ? password : "");
    }

    curl_easy_setopt(curl, CURLOPT_URL,            url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,  write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,      &ctx);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_cb);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA,     resp);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER,    errbuf);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,        30L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS,      5L);
    /* Accept self-signed certs (common on home servers) */
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    /* Identify ourselves */
    curl_easy_setopt(curl, CURLOPT_USERAGENT,
                     "PocketOPDS/1.0 (PocketBook; InkView)");

    if (userpwd[0]) {
        curl_easy_setopt(curl, CURLOPT_HTTPAUTH,   CURLAUTH_BASIC);
        curl_easy_setopt(curl, CURLOPT_USERPWD,    userpwd);
    }

    /* Request Atom+XML (OPDS standard) */
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers,
        "Accept: application/atom+xml, application/xml, text/xml, */*");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    CURLcode rc = curl_easy_perform(curl);

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    resp->http_code = (int)http_code;
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK) {
        snprintf(resp->error, sizeof(resp->error), "%s", errbuf[0] ? errbuf
                                                    : curl_easy_strerror(rc));
        free(ctx.buf);
        return resp;
    }

    resp->data = ctx.buf;      /* caller owns this */
    resp->size = (long)ctx.len;
    return resp;
}

void net_response_free(net_response_t *r)
{
    if (!r) return;
    free(r->data);
    free(r);
}

/* ── Stream a download directly to a file ────────────────────────────────── */

typedef struct {
    FILE       *fp;
    long        written;
    int         error;   /* 1 if fwrite failed */
} file_write_ctx_t;

static size_t file_write_cb(void *ptr, size_t size, size_t nmemb, void *ud)
{
    file_write_ctx_t *ctx = (file_write_ctx_t *)ud;
    if (ctx->error) return 0;
    size_t n = fwrite(ptr, size, nmemb, ctx->fp);
    if (n != nmemb) { ctx->error = 1; return 0; }
    ctx->written += (long)(size * nmemb);
    return size * nmemb;
}

typedef struct {
    char  *content_disp;   /* points into caller buffer */
    size_t cd_size;
} file_hdr_ctx_t;

static size_t file_header_cb(void *ptr, size_t size, size_t nmemb, void *ud)
{
    file_hdr_ctx_t *ctx = (file_hdr_ctx_t *)ud;
    size_t n = size * nmemb;
    const char *hdr = (const char *)ptr;
    if (ctx->content_disp && strncasecmp(hdr, "Content-Disposition:", 20) == 0) {
        const char *val = hdr + 20;
        while (*val == ' ') val++;
        size_t vlen = n - (size_t)(val - hdr);
        while (vlen > 0 && (val[vlen-1] == '\r' || val[vlen-1] == '\n')) vlen--;
        if (vlen >= ctx->cd_size) vlen = ctx->cd_size - 1;
        memcpy(ctx->content_disp, val, vlen);
        ctx->content_disp[vlen] = '\0';
    }
    return n;
}

int net_download_to_file(const char *url,
                         const char *username, const char *password,
                         const char *dest_path,
                         char *content_disp_out, size_t cd_size,
                         char *err_out, size_t err_size)
{
    if (content_disp_out && cd_size > 0) content_disp_out[0] = '\0';
    if (err_out && err_size > 0) err_out[0] = '\0';

    FILE *fp = fopen(dest_path, "wb");
    if (!fp) {
        if (err_out)
            snprintf(err_out, err_size, "Cannot create: %s", dest_path);
        return -1;
    }

    CURL *curl = curl_easy_init();
    if (!curl) {
        fclose(fp); remove(dest_path);
        if (err_out) snprintf(err_out, err_size, "curl_easy_init failed");
        return -1;
    }

    char errbuf[CURL_ERROR_SIZE] = {0};
    file_write_ctx_t wctx = { fp, 0, 0 };
    file_hdr_ctx_t   hctx = { content_disp_out, cd_size };

    char userpwd[MAX_CRED_LEN * 2 + 2] = {0};
    if (username && username[0])
        snprintf(userpwd, sizeof(userpwd), "%s:%s",
                 username, password ? password : "");

    curl_easy_setopt(curl, CURLOPT_URL,            url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,  file_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,      &wctx);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, file_header_cb);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA,     &hctx);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER,    errbuf);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,        300L);   /* 5 min for big files */
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS,      5L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT,
                     "PocketOPDS/1.0 (PocketBook; InkView)");
    if (userpwd[0]) {
        curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
        curl_easy_setopt(curl, CURLOPT_USERPWD,  userpwd);
    }

    CURLcode rc = curl_easy_perform(curl);
    fclose(fp);

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK) {
        remove(dest_path);
        if (err_out)
            snprintf(err_out, err_size, "%s", errbuf[0] ? errbuf
                                                         : curl_easy_strerror(rc));
        return (int)http_code;
    }
    if (http_code < 200 || http_code >= 300 || wctx.error) {
        remove(dest_path);
        if (err_out && !err_out[0])
            snprintf(err_out, err_size, "HTTP %ld", http_code);
        return (int)http_code;
    }
    return (int)http_code;
}
