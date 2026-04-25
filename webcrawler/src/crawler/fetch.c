#include "fetch.h"
#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char  *buf;
    size_t len;
    size_t cap;
} membuf_t;

static size_t write_cb(void *data, size_t size, size_t nmemb, void *userp) {
    size_t bytes = size * nmemb;
    membuf_t *m  = (membuf_t *)userp;
    if (m->len + bytes + 1 > m->cap) {
        size_t new_cap = (m->cap ? m->cap * 2 : 4096);
        while (new_cap < m->len + bytes + 1) new_cap *= 2;
        char *tmp = realloc(m->buf, new_cap);
        if (!tmp) return 0;
        m->buf = tmp;
        m->cap = new_cap;
    }
    memcpy(m->buf + m->len, data, bytes);
    m->len += bytes;
    m->buf[m->len] = '\0';
    return bytes;
}

char *fetch_url(const char *url, size_t *len) {
    CURL *curl = curl_easy_init();
    if (!curl) return NULL;

    membuf_t m = {0};

    curl_easy_setopt(curl, CURLOPT_URL,            url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,  write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,      &m);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS,      5L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,        15L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT,      "WebCrawler/1.0");
    /* Accept only HTTP(S) */
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS,
                     CURLPROTO_HTTP | CURLPROTO_HTTPS);

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK || (http_code != 0 && (http_code < 200 || http_code >= 300))) {
        free(m.buf);
        return NULL;
    }
    if (len) *len = m.len;
    return m.buf;
}
