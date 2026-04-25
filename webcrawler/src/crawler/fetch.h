#ifndef FETCH_H
#define FETCH_H

#include <stddef.h>

/* Fetch a URL with libcurl.
   Returns a malloc'd NUL-terminated buffer of the response body,
   sets *len to the byte count (excluding NUL).
   Returns NULL on failure (timeout, HTTP error, alloc failure). */
char *fetch_url(const char *url, size_t *len);

#endif /* FETCH_H */
