#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/stat.h>
#include <errno.h>
#include <time.h>
#include <stdatomic.h>
#include <unistd.h>
#include <curl/curl.h>

#include "queue.h"
#include "visited.h"
#include "fetch.h"
#include "parse.h"
#include "ipc_client.h"

/* ── Global config ──────────────────────────────────────────────── */

typedef struct {
    char *seed;
    int   max_depth;
    int   max_pages;
    int   num_threads;
    char *out_dir;
    char *ipc_path;
} config_t;

/* ── Shared crawler state ───────────────────────────────────────── */

typedef struct {
    config_t       *cfg;
    bounded_queue_t queue;
    visited_set_t   visited;
    int             ipc_fd;

    atomic_int pages_fetched;
    atomic_int pages_scheduled;
    atomic_int pages_skipped;
    atomic_int pages_failed;
    atomic_uint next_docid;

    pthread_mutex_t ipc_lock;   /* serialise IPC writes */
} crawler_state_t;

/* ── Worker thread ──────────────────────────────────────────────── */

static void *worker(void *arg) {
    crawler_state_t *st = (crawler_state_t *)arg;
    config_t        *cfg = st->cfg;

    while (1) {
        char *url = NULL;
        int   depth = 0;
        if (queue_pop(&st->queue, &url, &depth) < 0)
            break; /* shutdown */

        /* Check page limit */
        int fetched = atomic_fetch_add(&st->pages_fetched, 1);
        if (fetched >= cfg->max_pages) {
            atomic_fetch_sub(&st->pages_fetched, 1);
            atomic_fetch_add(&st->pages_skipped, 1);
            free(url);
            queue_shutdown(&st->queue);
            break;
        }

        /* Fetch */
        size_t html_len = 0;
        char *html = fetch_url(url, &html_len);
        if (!html) {
            atomic_fetch_add(&st->pages_failed, 1);
            atomic_fetch_sub(&st->pages_fetched, 1);
            fprintf(stderr, "[FAIL] %s\n", url);
            free(url);
            continue;
        }

        /* Assign docid and persist */
        unsigned int docid = atomic_fetch_add(&st->next_docid, 1);
        char filepath[1024];
        snprintf(filepath, sizeof(filepath), "%s/pages/%u.html",
                 cfg->out_dir, docid);

        FILE *f = fopen(filepath, "wb");
        if (f) {
            fwrite(html, 1, html_len, f);
            fclose(f);
        } else {
            fprintf(stderr, "[WARN] Cannot write %s: %s\n",
                    filepath, strerror(errno));
        }

        printf("[FETCH] depth=%d docid=%u %s\n", depth, docid, url);

        /* Send metadata to indexer */
        pthread_mutex_lock(&st->ipc_lock);
        ipc_send_doc(st->ipc_fd, docid, url, filepath, (unsigned)depth);
        pthread_mutex_unlock(&st->ipc_lock);

        /* Parse links and enqueue if within depth */
        if (depth < cfg->max_depth) {
            link_node_t *links = html_extract_links(html, html_len, url);
            for (link_node_t *ln = links; ln; ln = ln->next) {
                if (visited_check_and_insert(&st->visited, ln->url) == 0) {
                    /* Check page limit before enqueuing */
                    int scheduled = atomic_fetch_add(&st->pages_scheduled, 1);

                    if (scheduled < cfg->max_pages) {
                        queue_push(&st->queue, ln->url, depth + 1);
                    } else {
                        atomic_fetch_sub(&st->pages_scheduled, 1);
                    }
                }
            links_free(links);
            }
        }

        free(html);
        free(url);

        /* If queue empty and we have enough pages, shut down */
        if (atomic_load(&st->pages_fetched) >= cfg->max_pages)
            queue_shutdown(&st->queue);
    }
    return NULL;
}

/* ── Helpers ────────────────────────────────────────────────────── */

static void usage(const char *prog) {
    fprintf(stderr,
        "USAGE: %s --seed <url> --max-depth <D> --max-pages <N>"
        " -t <threads> --out <dir> --ipc <path>\n", prog);
    exit(1);
}

static void mkdir_p(const char *path) {
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

/* ── main ───────────────────────────────────────────────────────── */

int main(int argc, char **argv) {
    config_t cfg = {
        .seed        = NULL,
        .max_depth   = 2,
        .max_pages   = 100,
        .num_threads = 4,
        .out_dir     = "data",
        .ipc_path    = "/tmp/crawl.sock",
    };

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0)
            usage(argv[0]);
        else if (strcmp(argv[i], "--seed")      == 0 && i+1 < argc) cfg.seed        = argv[++i];
        else if (strcmp(argv[i], "--max-depth") == 0 && i+1 < argc) cfg.max_depth   = atoi(argv[++i]);
        else if (strcmp(argv[i], "--max-pages") == 0 && i+1 < argc) cfg.max_pages   = atoi(argv[++i]);
        else if (strcmp(argv[i], "-t")          == 0 && i+1 < argc) cfg.num_threads = atoi(argv[++i]);
        else if (strcmp(argv[i], "--out")       == 0 && i+1 < argc) cfg.out_dir     = argv[++i];
        else if (strcmp(argv[i], "--ipc")       == 0 && i+1 < argc) cfg.ipc_path    = argv[++i];
        else { fprintf(stderr, "Unknown option: %s\n", argv[i]); usage(argv[0]); }
    }

    if (!cfg.seed) { fprintf(stderr, "Error: --seed is required\n"); usage(argv[0]); }

    /* Init curl globally */
    curl_global_init(CURL_GLOBAL_ALL);

    /* Create output directories */
    char pages_dir[1024];
    snprintf(pages_dir, sizeof(pages_dir), "%s/pages", cfg.out_dir);
    mkdir_p(pages_dir);

    /* Connect to indexer */
    int ipc_fd = ipc_connect(cfg.ipc_path);
    if (ipc_fd < 0) {
        fprintf(stderr, "Cannot connect to indexer at %s. Is it running?\n",
                cfg.ipc_path);
        return 1;
    }
    printf("[IPC] Connected to indexer at %s\n", cfg.ipc_path);

    /* Init state */
    crawler_state_t st;
    memset(&st, 0, sizeof(st));
    st.cfg    = &cfg;
    st.ipc_fd = ipc_fd;
    atomic_init(&st.pages_fetched, 0);
    atomic_init(&st.pages_scheduled, 1);
    atomic_init(&st.pages_skipped, 0);
    atomic_init(&st.pages_failed,  0);
    atomic_init(&st.next_docid,    0);
    pthread_mutex_init(&st.ipc_lock, NULL);

    int queue_cap = cfg.max_pages * 4;
    if (queue_cap < 1024) queue_cap = 1024;
    queue_init(&st.queue, queue_cap);
    visited_init(&st.visited, (unsigned)(cfg.max_pages * 2));

    /* Seed the queue */
    char *seed_norm = normalize_url(cfg.seed);
    if (!seed_norm) { fprintf(stderr, "Invalid seed URL\n"); return 1; }
    visited_check_and_insert(&st.visited, seed_norm);
    queue_push(&st.queue, seed_norm, 0);
    free(seed_norm);

    /* Start workers */
    struct timespec t_start, t_end;
    clock_gettime(CLOCK_MONOTONIC, &t_start);

    pthread_t *threads = malloc((size_t)cfg.num_threads * sizeof(pthread_t));
    for (int i = 0; i < cfg.num_threads; i++)
        pthread_create(&threads[i], NULL, worker, &st);

    for (int i = 0; i < cfg.num_threads; i++)
        pthread_join(threads[i], NULL);

    clock_gettime(CLOCK_MONOTONIC, &t_end);
    double elapsed = (t_end.tv_sec - t_start.tv_sec) +
                     (t_end.tv_nsec - t_start.tv_nsec) / 1e9;

    /* Send shutdown */
    ipc_send_shutdown(ipc_fd);
    ipc_close(ipc_fd);

    /* Summary */
    printf("\n=== Crawler Summary ===\n");
    printf("  Pages fetched : %d\n",  atomic_load(&st.pages_fetched));
    printf("  Pages skipped : %d\n",  atomic_load(&st.pages_skipped));
    printf("  Pages failed  : %d\n",  atomic_load(&st.pages_failed));
    printf("  Max queue depth seen: %zu\n", st.queue.max_depth_seen);
    printf("  Elapsed       : %.2f s\n", elapsed);

    /* Cleanup */
    free(threads);
    queue_destroy(&st.queue);
    visited_destroy(&st.visited);
    pthread_mutex_destroy(&st.ipc_lock);
    curl_global_cleanup();
    return 0;
}
