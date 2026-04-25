#ifndef VISITED_H
#define VISITED_H

#include <pthread.h>

/* Open-addressing hash set for visited URLs.
   check+insert is atomic under the internal lock. */

typedef struct {
    char          **slots;
    unsigned int    capacity;
    unsigned int    count;
    pthread_mutex_t lock;
} visited_set_t;

int  visited_init(visited_set_t *v, unsigned int initial_capacity);
void visited_destroy(visited_set_t *v);

/* Returns 1 if already seen (not inserted), 0 if newly inserted */
int  visited_check_and_insert(visited_set_t *v, const char *url);

unsigned int visited_count(visited_set_t *v);

#endif /* VISITED_H */
