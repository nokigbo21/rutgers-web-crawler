#include "index_reader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

typedef struct dict_rec {
    char         *term;
    long          offset;
    unsigned int  df;
} dict_rec_t;

typedef struct doc_url {
    unsigned int  docid;
    char         *url;
} doc_url_t;

struct index_reader {
    dict_rec_t   *dict;
    unsigned int  dict_count;
    FILE         *postings_fp;
    doc_url_t    *docs;
    unsigned int  docs_count;
};

static int dict_cmp(const void *a, const void *b) {
    return strcmp(((dict_rec_t *)a)->term, ((dict_rec_t *)b)->term);
}

index_reader_t *index_open(const char *index_dir) {
    index_reader_t *ir = calloc(1, sizeof(*ir));
    char path[1024];

    /* Load dict.tsv */
    snprintf(path, sizeof(path), "%s/dict.tsv", index_dir);
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "Cannot open %s\n", path); free(ir); return NULL; }

    unsigned int cap = 1024;
    ir->dict = malloc(cap * sizeof(dict_rec_t));
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char term[256]; long off; unsigned int df;
        if (sscanf(line, "%255s\t%ld\t%u", term, &off, &df) != 3) continue;
        if (ir->dict_count >= cap) {
            cap *= 2;
            ir->dict = realloc(ir->dict, cap * sizeof(dict_rec_t));
        }
        ir->dict[ir->dict_count].term   = strdup(term);
        ir->dict[ir->dict_count].offset = off;
        ir->dict[ir->dict_count].df     = df;
        ir->dict_count++;
    }
    fclose(f);
    qsort(ir->dict, ir->dict_count, sizeof(dict_rec_t), dict_cmp);

    /* Open postings.bin */
    snprintf(path, sizeof(path), "%s/postings.bin", index_dir);
    ir->postings_fp = fopen(path, "rb");
    if (!ir->postings_fp) {
        fprintf(stderr, "Cannot open %s\n", path);
        free(ir); return NULL;
    }

    /* Load docs.tsv */
    snprintf(path, sizeof(path), "%s/docs.tsv", index_dir);
    f = fopen(path, "r");
    if (!f) { fprintf(stderr, "Cannot open %s\n", path); free(ir); return NULL; }

    unsigned int dcap = 1024;
    ir->docs = malloc(dcap * sizeof(doc_url_t));
    while (fgets(line, sizeof(line), f)) {
        unsigned int docid;
        char url[2048], fp[1024]; unsigned int depth;
        if (sscanf(line, "%u\t%2047s\t%1023s\t%u", &docid, url, fp, &depth) < 2) continue;
        if (ir->docs_count >= dcap) { dcap *= 2; ir->docs = realloc(ir->docs, dcap * sizeof(doc_url_t)); }
        ir->docs[ir->docs_count].docid = docid;
        ir->docs[ir->docs_count].url   = strdup(url);
        ir->docs_count++;
    }
    fclose(f);
    return ir;
}

void index_close(index_reader_t *ir) {
    if (!ir) return;
    for (unsigned int i = 0; i < ir->dict_count; i++) free(ir->dict[i].term);
    free(ir->dict);
    if (ir->postings_fp) fclose(ir->postings_fp);
    for (unsigned int i = 0; i < ir->docs_count; i++) free(ir->docs[i].url);
    free(ir->docs);
    free(ir);
}

unsigned int *index_lookup(index_reader_t *ir, const char *term, unsigned int *count) {
    /* Binary search in sorted dict */
    int lo = 0, hi = (int)ir->dict_count - 1, mid = -1;
    while (lo <= hi) {
        mid = (lo + hi) / 2;
        int c = strcmp(ir->dict[mid].term, term);
        if (c == 0) break;
        else if (c < 0) lo = mid + 1;
        else hi = mid - 1;
        mid = -1;
    }
    if (mid < 0) { *count = 0; return NULL; }

    dict_rec_t *rec = &ir->dict[mid];
    fseek(ir->postings_fp, rec->offset, SEEK_SET);
    uint32_t df;
    if (fread(&df, 4, 1, ir->postings_fp) != 1) { *count = 0; return NULL; }
    unsigned int *ids = malloc(df * sizeof(unsigned int));
    for (unsigned int i = 0; i < df; i++) {
        uint32_t did = 0;
        if (fread(&did, 4, 1, ir->postings_fp) != 1) break;
        ids[i] = did;
    }
    *count = df;
    return ids;
}

const char *index_doc_url(index_reader_t *ir, unsigned int docid) {
    for (unsigned int i = 0; i < ir->docs_count; i++)
        if (ir->docs[i].docid == docid) return ir->docs[i].url;
    return "(unknown)";
}
