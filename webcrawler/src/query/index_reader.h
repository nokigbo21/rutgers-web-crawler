#ifndef INDEX_READER_H
#define INDEX_READER_H

#include <stddef.h>

/* Opaque index handle */
typedef struct index_reader index_reader_t;

index_reader_t *index_open(const char *index_dir);
void            index_close(index_reader_t *ir);

/* Returns malloc'd array of docids (sorted) matching term, sets *count.
   Returns NULL if term not found. Caller frees. */
unsigned int *index_lookup(index_reader_t *ir, const char *term,
                           unsigned int *count);

/* Look up URL for a docid. Returns pointer into internal buffer (valid
   until index_close). */
const char *index_doc_url(index_reader_t *ir, unsigned int docid);

#endif
