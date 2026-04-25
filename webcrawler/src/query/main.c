#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "index_reader.h"

static int uint_cmp(const void *a, const void *b) {
    unsigned int x = *(unsigned int *)a, y = *(unsigned int *)b;
    return (x > y) - (x < y);
}

/* Intersect two sorted arrays; result written into out (caller-allocated >= min(an,bn)).
   Returns result count. */
static unsigned int intersect(const unsigned int *a, unsigned int an,
                              const unsigned int *b, unsigned int bn,
                              unsigned int *out) {
    unsigned int i = 0, j = 0, k = 0;
    while (i < an && j < bn) {
        if      (a[i] < b[j]) i++;
        else if (a[i] > b[j]) j++;
        else { out[k++] = a[i]; i++; j++; }
    }
    return k;
}

int main(int argc, char **argv) {
    char *index_dir = "data/index";
    int   term_start = 1;

    if (argc < 2) {
        fprintf(stderr, "USAGE: %s --index <dir> <term1> [term2 ...]\n", argv[0]);
        return 1;
    }
    if (strcmp(argv[1], "--index") == 0) {
        if (argc < 3) { fprintf(stderr, "Missing index dir\n"); return 1; }
        index_dir  = argv[2];
        term_start = 3;
    }

    if (term_start >= argc) {
        fprintf(stderr, "No query terms provided.\n"); return 1;
    }

    index_reader_t *ir = index_open(index_dir);
    if (!ir) return 1;

    /* Lowercase query terms */
    for (int i = term_start; i < argc; i++)
        for (char *p = argv[i]; *p; p++) *p = (char)tolower((unsigned char)*p);

    /* Lookup first term */
    unsigned int count = 0;
    unsigned int *result = index_lookup(ir, argv[term_start], &count);
    if (!result || count == 0) {
        printf("No documents matched all query terms.\n");
        index_close(ir); return 0;
    }
    qsort(result, count, sizeof(unsigned int), uint_cmp);

    /* AND with each subsequent term */
    for (int i = term_start + 1; i < argc; i++) {
        unsigned int cnt2 = 0;
        unsigned int *list2 = index_lookup(ir, argv[i], &cnt2);
        if (!list2 || cnt2 == 0) {
            free(result); free(list2);
            printf("No documents matched all query terms.\n");
            index_close(ir); return 0;
        }
        qsort(list2, cnt2, sizeof(unsigned int), uint_cmp);

        unsigned int *tmp = malloc(count * sizeof(unsigned int));
        count = intersect(result, count, list2, cnt2, tmp);
        free(result); free(list2);
        result = tmp;
        if (count == 0) break;
    }

    if (count == 0) {
        printf("No documents matched all query terms.\n");
    } else {
        printf("Found %u matching document%s (AND across terms):\n",
               count, count == 1 ? "" : "s");
        for (unsigned int i = 0; i < count; i++) {
            printf("  %u\t%s\n", result[i], index_doc_url(ir, result[i]));
        }
    }

    free(result);
    index_close(ir);
    return 0;
}
