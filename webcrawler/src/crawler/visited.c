#include "visited.h"
#include <stdlib.h>
#include <string.h>

static unsigned int hash_str(const char *s) {
    unsigned int h = 5381;
    while (*s) h = h * 33 ^ (unsigned char)*s++;
    return h;
}

static int _insert_no_lock(visited_set_t *v, char *owned_str) {
    unsigned int mask = v->capacity - 1;
    unsigned int idx  = hash_str(owned_str) & mask;
    while (v->slots[idx]) {
        if (strcmp(v->slots[idx], owned_str) == 0) return 1; /* duplicate */
        idx = (idx + 1) & mask;
    }
    v->slots[idx] = owned_str;
    v->count++;
    return 0;
}

static void _resize(visited_set_t *v) {
    unsigned int new_cap = v->capacity * 2;
    char **new_slots = calloc(new_cap, sizeof(char *));
    if (!new_slots) return;
    unsigned int old_cap = v->capacity;
    char **old_slots = v->slots;
    v->slots    = new_slots;
    v->capacity = new_cap;
    v->count    = 0;
    for (unsigned int i = 0; i < old_cap; i++) {
        if (old_slots[i])
            _insert_no_lock(v, old_slots[i]);
    }
    free(old_slots);
}

int visited_init(visited_set_t *v, unsigned int initial_capacity) {
    /* round up to power of 2 */
    unsigned int cap = 64;
    while (cap < initial_capacity) cap <<= 1;
    v->slots    = calloc(cap, sizeof(char *));
    v->capacity = cap;
    v->count    = 0;
    if (!v->slots) return -1;
    return pthread_mutex_init(&v->lock, NULL);
}

void visited_destroy(visited_set_t *v) {
    for (unsigned int i = 0; i < v->capacity; i++)
        free(v->slots[i]);
    free(v->slots);
    pthread_mutex_destroy(&v->lock);
}

int visited_check_and_insert(visited_set_t *v, const char *url) {
    pthread_mutex_lock(&v->lock);
    /* Check first without inserting */
    unsigned int mask = v->capacity - 1;
    unsigned int idx  = hash_str(url) & mask;
    while (v->slots[idx]) {
        if (strcmp(v->slots[idx], url) == 0) {
            pthread_mutex_unlock(&v->lock);
            return 1; /* already seen */
        }
        idx = (idx + 1) & mask;
    }
    /* Not found – resize if load > 0.6 */
    if (v->count * 10 >= v->capacity * 6)
        _resize(v);
    char *copy = strdup(url);
    _insert_no_lock(v, copy);
    pthread_mutex_unlock(&v->lock);
    return 0;
}

unsigned int visited_count(visited_set_t *v) {
    pthread_mutex_lock(&v->lock);
    unsigned int c = v->count;
    pthread_mutex_unlock(&v->lock);
    return c;
}
