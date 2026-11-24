/* 3DStify preview - sys.h on pthreads */
#include <pthread.h>
#include <stdlib.h>
#include <time.h>
#include "sys.h"

typedef struct { void (*fn)(void *); void *arg; pthread_t th; } Th;
static void *tramp(void *p) { Th *t = p; t->fn(t->arg); return NULL; }

SysThread sys_thread_start(void (*fn)(void *), void *arg, size_t stack) {
    (void)stack;
    Th *t = malloc(sizeof *t);
    t->fn = fn; t->arg = arg;
    if (pthread_create(&t->th, NULL, tramp, t)) { free(t); return NULL; }
    return t;
}
void sys_thread_join(SysThread h) { if (!h) return; Th *t = h; pthread_join(t->th, NULL); free(t); }
void sys_lock_init(SysLock *l) { pthread_mutex_t *m = malloc(sizeof *m); pthread_mutex_init(m, NULL); *(pthread_mutex_t **)l = m; }
void sys_lock(SysLock *l) { pthread_mutex_lock(*(pthread_mutex_t **)l); }
void sys_unlock(SysLock *l) { pthread_mutex_unlock(*(pthread_mutex_t **)l); }
void sys_sleep_ms(int ms) { struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L }; nanosleep(&ts, NULL); }
