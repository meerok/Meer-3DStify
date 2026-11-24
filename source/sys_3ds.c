/* 3DStify - sys.h on libctru */
#include <3ds.h>
#include "sys.h"

_Static_assert(sizeof(LightLock) <= sizeof(SysLock), "SysLock too small");

SysThread sys_thread_start(void (*fn)(void *), void *arg, size_t stack) {
    s32 prio = 0x30;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    s32 wp = prio + 1;
    if (wp > 0x3F) wp = 0x3F;
    Thread t = threadCreate(fn, arg, stack, wp, -2, false);
    return (SysThread)t;
}

void sys_thread_join(SysThread t) {
    if (!t) return;
    threadJoin((Thread)t, U64_MAX);
    threadFree((Thread)t);
}

void sys_lock_init(SysLock *l) { LightLock_Init((LightLock *)l); }
void sys_lock(SysLock *l) { LightLock_Lock((LightLock *)l); }
void sys_unlock(SysLock *l) { LightLock_Unlock((LightLock *)l); }
void sys_sleep_ms(int ms) { svcSleepThread((s64)ms * 1000000LL); }
