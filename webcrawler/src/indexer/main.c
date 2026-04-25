#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

#include "ipc_server.h"
#include "tokenizer.h"

/* ── In-memory index structures ─────────────────────────────────── */

#define DICT_INIT_CAP 16384

typedef struct posting_node {
    unsigned int      docid;
    struct posting_node *next;
} posting_node_t;

typedef struct dict_entry {
    char           *term;
    unsigned int    df;            /* document frequency */
    posting_node_t *postings;
    struct dict_entry *next;       /* chaining in hash table */
} dict_entry_t;

typedef struct {
    dict_entry_t **buckets;
    unsigned int   cap;
    unsigned int   count;
} hash_dict_t;

static hash_dict_t g_dict;

/* doc map: docid -> url, filepath, depth */
typedef struct {
    char        *url;
    char        *filepath;
    unsigned int depth;
} doc_rec_t;

#define DOC_CAP_INIT 1024
static doc_rec_t *g_docs   = NULL;
static unsigned int g_docs_cap   = 0;
static unsigned int g_docs_count = 0;

/* ── Hash dict helpers ───────────────────────────────────────────── */

static unsigned int djb2(const char *s) {
    unsigned int h = 5381;
    while (*s) h = h * 33 ^ (unsigned char)*s++;
    return h;
}

static void dict_init(hash_dict_t *d) {
    d->cap     = DICT_INIT_CAP;
    d->count   = 0;
    d->buckets = calloc(d->cap, sizeof(dict_entry_t *));
}

static dict_entry_t *dict_get_or_create(hash_dict_t *d, const char *term) {
    unsigned int idx = djb2(term) & (d->cap - 1);
    for (dict_entry_t *e = d->buckets[idx]; e; e = e->next)
        if (strcmp(e->term, term) == 0) return e;

    dict_entry_t *e = calloc(1, sizeof(*e));
    e->term    = strdup(term);
    e->next    = d->buckets[idx];
    d->buckets[idx] = e;
    d->count++;
    return e;
}

/* ── Token callback ──────────────────────────────────────────────── */

typedef struct { unsigned int docid; } tok_ctx_t;

static void on_token(const char *word, void *userdata) {
    tok_ctx_t    *ctx = (tok_ctx_t *)userdata;
    dict_entry_t *e   = dict_get_or_create(&g_dict, word);

    /* Avoid duplicate docids in postings list */
    for (posting_node_t *p = e->postings; p; p = p->next)
        if (p->docid == ctx->docid) return;

    posting_node_t *pn = malloc(sizeof(*pn));
    pn->docid = ctx->docid;
    pn->next  = e->postings;
    e->postings = pn;
    e->df++;
}

/* ── Doc map ─────────────────────────────────────────────────────── */

static void doc_add(unsigned int docid, const char *url,
                    const char *filepath, unsigned int depth) {
    if (docid >= g_docs_cap) {
        unsigned int new_cap = g_docs_cap ? g_docs_cap * 2 : DOC_CAP_INIT;
        while (new_cap <= docid) new_cap *= 2;
        g_docs = realloc(g_docs, new_cap * sizeof(doc_rec_t));
        memset(g_docs + g_docs_cap, 0,
               (new_cap - g_docs_cap) * sizeof(doc_rec_t));
        g_docs_cap = new_cap;
    }
    g_docs[docid].url      = strdup(url);
    g_docs[docid].filepath = strdup(filepath);
    g_docs[docid].depth    = depth;
    if (docid >= g_docs_count) g_docs_count = docid + 1;
}

/* ── Flush to disk ───────────────────────────────────────────────── */

static void flush_index(const char *out_dir) {
    /* docs.tsv */
    char path[1024];
    snprintf(path, sizeof(path), "%s/docs.tsv", out_dir);
    FILE *f = fopen(path, "w");
    if (!f) { perror(path); return; }
    for (unsigned int i = 0; i < g_docs_count; i++) {
        if (!g_docs[i].url) continue;
        fprintf(f, "%u\t%s\t%s\t%u\n",
                i, g_docs[i].url, g_docs[i].filepath, g_docs[i].depth);
    }
    fclose(f);
    printf("[INDEX] Wrote %s\n", path);

    /* postings.bin – simple format:
       for each term: 4-byte df, then df*4-byte docids */
    snprintf(path, sizeof(path), "%s/postings.bin", out_dir);
    FILE *pb = fopen(path, "wb");
    if (!pb) { perror(path); return; }

    /* dict.tsv: term <TAB> byte_offset <TAB> df */
    snprintf(path, sizeof(path), "%s/dict.tsv", out_dir);
    FILE *dt = fopen(path, "w");
    if (!dt) { perror(path); fclose(pb); return; }

    for (unsigned int b = 0; b < g_dict.cap; b++) {
        for (dict_entry_t *e = g_dict.buckets[b]; e; e = e->next) {
            long offset = ftell(pb);
            uint32_t df = e->df;
            fwrite(&df, 4, 1, pb);
            /* Collect docids */
            for (posting_node_t *p = e->postings; p; p = p->next) {
                uint32_t did = p->docid;
                fwrite(&did, 4, 1, pb);
            }
            fprintf(dt, "%s\t%ld\t%u\n", e->term, offset, e->df);
        }
    }
    fclose(pb);
    fclose(dt);
    printf("[INDEX] Wrote dict.tsv and postings.bin (%u terms)\n", g_dict.count);
}

/* ── mkdir helper ────────────────────────────────────────────────── */
static void mkdir_p(const char *path) {
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') { *p = '\0'; mkdir(tmp, 0755); *p = '/'; }
    }
    mkdir(tmp, 0755);
}

/* ── main ───────────────────────────────────────────────────────── */

int main(int argc, char **argv) {
    char *ipc_path = "/tmp/crawl.sock";
    char *out_dir  = "data/index";

    for (int i = 1; i < argc; i++) {
        if      (strcmp(argv[i], "--ipc") == 0 && i+1 < argc) ipc_path = argv[++i];
        else if (strcmp(argv[i], "--out") == 0 && i+1 < argc) out_dir  = argv[++i];
        else {
            fprintf(stderr, "USAGE: %s --ipc <path> --out <dir>\n", argv[0]);
            return 1;
        }
    }

    mkdir_p(out_dir);
    dict_init(&g_dict);

    int cfd = ipc_server_init(ipc_path);
    if (cfd < 0) return 1;

    unsigned int docs_indexed = 0;
    doc_meta_t meta;

    while (1) {
        int r = ipc_recv_doc(cfd, &meta);
        if (r < 0) { fprintf(stderr, "[IPC] Error reading message\n"); break; }
        if (r == 0) { printf("[IPC] Shutdown received.\n"); break; }

        /* Save to doc map */
        doc_add(meta.docid, meta.url, meta.filepath, meta.depth);

        /* Read saved HTML and index it */
        FILE *f = fopen(meta.filepath, "rb");
        if (!f) {
            fprintf(stderr, "[WARN] Cannot open %s\n", meta.filepath);
            docs_indexed++;
            continue;
        }
        fseek(f, 0, SEEK_END);
        long flen = ftell(f);
        rewind(f);
        char *html = malloc((size_t)flen + 1);
        if (html) {
            if (fread(html, 1, (size_t)flen, f) != (size_t)flen)
                fprintf(stderr, "[WARN] Short read on %s\n", meta.filepath);
            html[flen] = '\0';

            /* Extract text then tokenize */
            char *text = NULL;
            /* Simple strip: re-use html buffer in place */
            text = html; /* html_extract_text would be cleaner but we inline here */

            tok_ctx_t ctx = { .docid = meta.docid };
            tokenize(text, on_token, &ctx);
            free(html);
        }
        fclose(f);

        docs_indexed++;
        if (docs_indexed % 50 == 0)
            printf("[INDEX] Indexed %u documents so far...\n", docs_indexed);
    }

    close(cfd);
    unlink(ipc_path);

    printf("[INDEX] Total documents indexed: %u\n", docs_indexed);
    flush_index(out_dir);
    printf("[INDEX] Done.\n");
    return 0;
}
