#ifndef QUEUE_H
#define QUEUE_H

#include <pthread.h>
#include <stddef.h>

typedef struct queue_entry {
    char   *url;
    int     depth;
    struct queue_entry *next;
} queue_entry_t;

typedef struct {
    queue_entry_t  *head;
    queue_entry_t  *tail;
    int             size;
    int             capacity;
    int             shutdown;
    size_t          max_depth_seen;   /* for stats */
    pthread_mutex_t lock;
    pthread_cond_t  not_empty;
    pthread_cond_t  not_full;
} bounded_queue_t;

int  queue_init(bounded_queue_t *q, int capacity);
void queue_destroy(bounded_queue_t *q);

/* Returns 0 on success, -1 if shutdown signalled */
int  queue_push(bounded_queue_t *q, const char *url, int depth);

/* Returns 0 on success (url/depth filled), -1 if shutdown + empty */
int  queue_pop(bounded_queue_t *q, char **url_out, int *depth_out);

/* Signal all waiting threads to wake and drain */
void queue_shutdown(bounded_queue_t *q);

int  queue_size(bounded_queue_t *q);

#endif /* QUEUE_H */
