/* DKU shim: KOS mutexes on pthreads. */
#ifndef DKU_SHIM_KOS_MUTEX_H
#define DKU_SHIM_KOS_MUTEX_H

#include <pthread.h>

typedef struct {
    pthread_mutex_t m;
} mutex_t;

#define MUTEX_TYPE_NORMAL 0
#define MUTEX_INITIALIZER { PTHREAD_MUTEX_INITIALIZER }

static inline int mutex_init(mutex_t *m, int type) { (void)type; return pthread_mutex_init(&m->m, NULL) ? -1 : 0; }
static inline int mutex_destroy(mutex_t *m) { return pthread_mutex_destroy(&m->m) ? -1 : 0; }
static inline int mutex_lock(mutex_t *m) { return pthread_mutex_lock(&m->m) ? -1 : 0; }
static inline int mutex_trylock(mutex_t *m) { return pthread_mutex_trylock(&m->m) ? -1 : 0; }
static inline int mutex_unlock(mutex_t *m) { return pthread_mutex_unlock(&m->m) ? -1 : 0; }

#endif
