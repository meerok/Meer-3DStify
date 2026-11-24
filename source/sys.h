#pragma once
/* tiny thread/lock wrappers so library.c and art.c also build for the PC preview */
#include <stddef.h>

typedef void *SysThread;
typedef struct { int v[4]; } SysLock;   /* big enough for LightLock and a pthread mutex pointer */

SysThread sys_thread_start(void (*fn)(void *), void *arg, size_t stack);  /* low priority worker */
void      sys_thread_join(SysThread t);
void      sys_lock_init(SysLock *l);
void      sys_lock(SysLock *l);
void      sys_unlock(SysLock *l);
void      sys_sleep_ms(int ms);
