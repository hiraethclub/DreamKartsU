/* DKU shim: KOS threads on pthreads (port/shim/kos_thread.c). */
#ifndef DKU_SHIM_KOS_THREAD_H
#define DKU_SHIM_KOS_THREAD_H

#include <stdbool.h>
#include <stddef.h>

typedef struct kthread kthread_t;

typedef struct {
    bool create_detached;
    size_t stack_size;
    void *stack_ptr;
    int prio;
    const char *label;
} kthread_attr_t;

kthread_t *thd_create(bool detach, void *(*routine)(void *param), void *param);
kthread_t *thd_create_ex(const kthread_attr_t *attr, void *(*routine)(void *param), void *param);
int thd_join(kthread_t *thd, void **value_ptr);
int thd_detach(kthread_t *thd);
void thd_sleep(unsigned int ms);
void thd_pass(void);

#endif
