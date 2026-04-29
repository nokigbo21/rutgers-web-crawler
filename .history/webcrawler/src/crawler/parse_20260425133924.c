#include "parse.h"
#define _GNU_SOURCE
#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <strings.h>

/* ── text extraction ─────────────────────────────────────────────── */

char *html_extract_text(const char *html, size_t html_len) {
    char *out = malloc(html_len + 1);
    if (!out) return NULL;
    size_t oi = 0;
    int in_tag = 0;
    int in_script = 0;
    for (size_t i = 0; i < html_len; i++) {
        if (!in_tag && i + 7 < html_len &&
            strncasecmp(html + i, "<script", 7) == 0) { in_script = 1; }
        if (!in_tag && i + 8 < html_len &&
            strncasecmp(html + i, "</script", 8) == 0) { in_script = 0; }
        if (!in_tag && i + 6 < html_len &&
            strncasecmp(html + i, "<style", 6) == 0)  { in_script = 1; }
        if (!in_tag && i + 7 < html_len &&
            strncasecmp(html + i, "</style", 7) == 0)  { in_script = 0; }

        if (html[i] == '<') { in_tag = 1; continue; }
        if (html[i] == '>') { in_tag = 0;
            if (oi > 0 && out[oi-1] != ' ') out[oi++] = ' ';
            continue; }
        if (in_tag || in_script) continue;
        out[oi++] = html[i];
    }
    out[oi] = '\0';
    return out;
}

/* ── URL normalization ───────────────────────────────────────────── */

char *normalize_url(const char *url) {
    if (!url) return NULL;
    /* Must start with http:// or https:// */
    if (strncasecmp(url, "http://",  7) != 0 &&
        strncasecmp(url, "https://", 8) != 0)
        return NULL;

    char *copy = strdup(url);
    if (!copy) return NULL;

    /* Lowercase scheme and host */
    char *p = copy;
    while (*p && *p != ':') { *p = tolower((unsigned char)*p); p++; }
    p += 3; /* skip :// */
    while (*p && *p != '/' && *p != '?' && *p != '#')
        { *p = tolower((unsigned char)*p); p++; }

    /* Strip fragment */
    char *frag = strchr(copy, '#');
    if (frag) *frag = '\0';

    return copy;
}

/* ── Resolve href against base ───────────────────────────────────── */

static char *resolve(const char *base, const char *href) {
    if (!href || href[0] == '\0') return NULL;
    /* Absolute */
    if (strncasecmp(href, "http://",  7) == 0 ||
        strncasecmp(href, "https://", 8) == 0)
        return normalize_url(href);

    /* Protocol-relative */
    if (href[0] == '/' && href[1] == '/') {
        /* Pick scheme from base */
        char scheme[8] = "http";
        if (strncasecmp(base, "https", 5) == 0) strcpy(scheme, "https");
        char tmp[4096];
        snprintf(tmp, sizeof(tmp), "%s:%s", scheme, href);
        return normalize_url(tmp);
    }

    /* Absolute-path */
    if (href[0] == '/') {
        /* Extract origin from base */
        const char *after_scheme = strstr(base, "://");
        if (!after_scheme) return NULL;
        after_scheme += 3;
        const char *path_start = strchr(after_scheme, '/');
        char origin[2048];
        if (path_start)
            snprintf(origin, sizeof(origin), "%.*s",
                     (int)(path_start - base), base);
        else
            snprintf(origin, sizeof(origin), "%s", base);
        char tmp[4096];
        snprintf(tmp, sizeof(tmp), "%s%s", origin, href);
        return normalize_url(tmp);
    }

    /* Relative – not fully resolving for simplicity; skip */
    return NULL;
}

/* ── Link extraction ─────────────────────────────────────────────── */

link_node_t *html_extract_links(const char *html, size_t html_len,
                                const char *base_url) {
    link_node_t *head = NULL, *tail = NULL;
    const char *p = html;
    const char *end = html + html_len;

    while (p < end) {
        /* Find <a */
        const char *tag = strcasestr(p, "<a ");
        if (!tag) break;
        p = tag + 3;

        /* Find href=" or href=' */
        const char *href_attr = strcasestr(p, "href=");
        const char *close_tag = strchr(p, '>');
        if (!href_attr || (close_tag && href_attr > close_tag)) {
            p = close_tag ? close_tag + 1 : end;
            continue;
        }
        href_attr += 5;
        char delim = *href_attr;
        if (delim != '"' && delim != '\'') {
            /* unquoted – skip */
            p = href_attr;
            continue;
        }
        href_attr++;
        const char *href_end = memchr(href_attr, delim, (size_t)(end - href_attr));
        if (!href_end) break;

        size_t hlen = (size_t)(href_end - href_attr);
        char *href = strndup(href_attr, hlen);
        p = href_end + 1;

        char *resolved = resolve(base_url, href);
        free(href);
        if (!resolved) continue;

        link_node_t *node = malloc(sizeof(*node));
        if (!node) { free(resolved); break; }
        node->url  = resolved;
        node->next = NULL;
        if (tail) tail->next = node;
        else      head = node;
        tail = node;
    }
    return head;
}

void links_free(link_node_t *head) {
    while (head) {
        link_node_t *next = head->next;
        free(head->url);
        free(head);
        head = next;
    }
}
