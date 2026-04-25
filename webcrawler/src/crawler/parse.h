#ifndef PARSE_H
#define PARSE_H

#include <stddef.h>

/* Extract plain text from HTML (strips tags).
   Returns a malloc'd NUL-terminated string; caller frees. */
char *html_extract_text(const char *html, size_t html_len);

/* Link list returned by html_extract_links */
typedef struct link_node {
    char             *url;
    struct link_node *next;
} link_node_t;

/* Extract <a href="..."> links from HTML and resolve against base_url.
   Returns linked list; free with links_free(). */
link_node_t *html_extract_links(const char *html, size_t html_len,
                                const char *base_url);
void         links_free(link_node_t *head);

/* Normalize a URL: lowercase scheme+host, strip fragment.
   Returns malloc'd string or NULL if not HTTP(S). Caller frees. */
char *normalize_url(const char *url);

#endif /* PARSE_H */
