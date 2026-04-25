#include "queue.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

int queue_init(bounded_queue_t *q, int capacity) {
    memset(q, 0, sizeof(*q));
    q->capacity = capacity;
    if (pthread_mutex_init(&q->lock, NULL) != 0) return -1;
    if (pthread_cond_init(&q->not_empty, NULL) != 0) return -1;
    if (pthread_cond_init(&q->not_full,  NULL) != 0) return -1;
    return 0;
}

void queue_destroy(bounded_queue_t *q) {
    queue_entry_t *e = q->head;
    while (e) {
        queue_entry_t *next = e->next;
        free(e->url);
        free(e);
        e = next;
    }
    pthread_mutex_destroy(&q->lock);
    pthread_cond_destroy(&q->not_empty);
    pthread_cond_destroy(&q->not_full);
}

int queue_push(bounded_queue_t *q, const char *url, int depth) {
    pthread_mutex_lock(&q->lock);
    while (q->size >= q->capacity && !q->shutdown) {
        pthread_cond_wait(&q->not_full, &q->lock);
    }
    if (q->shutdown) {
        pthread_mutex_unlock(&q->lock);
        return -1;
    }
    queue_entry_t *e = malloc(sizeof(*e));
    if (!e) {
        pthread_mutex_unlock(&q->lock);
        return -1;
    }
    e->url   = strdup(url);
    e->depth = depth;
    e->next  = NULL;
    if (q->tail) q->tail->next = e;
    else         q->head = e;
    q->tail = e;
    q->size++;
    if ((size_t)q->size > q->max_depth_seen)
        q->max_depth_seen = (size_t)q->size;
    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->lock);
    return 0;
}

int queue_pop(bounded_queue_t *q, char **url_out, int *depth_out) {
    pthread_mutex_lock(&q->lock);
    while (q->size == 0 && !q->shutdown) {
        pthread_cond_wait(&q->not_empty, &q->lock);
    }
    if (q->size == 0) {
        pthread_mutex_unlock(&q->lock);
        return -1;
    }
    queue_entry_t *e = q->head;
    q->head = e->next;
    if (!q->head) q->tail = NULL;
    q->size--;
    pthread_cond_signal(&q->not_full);
    pthread_mutex_unlock(&q->lock);
    *url_out   = e->url;   /* caller must free */
    *depth_out = e->depth;
    free(e);
    return 0;
}

void queue_shutdown(bounded_queue_t *q) {
    pthread_mutex_lock(&q->lock);
    q->shutdown = 1;
    pthread_cond_broadcast(&q->not_empty);
    pthread_cond_broadcast(&q->not_full);
    pthread_mutex_unlock(&q->lock);
}

int queue_size(bounded_queue_t *q) {
    pthread_mutex_lock(&q->lock);
    int s = q->size;
    pthread_mutex_unlock(&q->lock);
    return s;
}
