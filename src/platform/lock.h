// Engine lock: a reader/writer lock on Windows (SRWLOCK), os_unfair_lock on Apple.
#pragma once
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
class EngineLock {
public:
    void lock() { AcquireSRWLockExclusive(&l_); }
    void unlock() { ReleaseSRWLockExclusive(&l_); }
    void lockShared() { AcquireSRWLockShared(&l_); }
    void unlockShared() { ReleaseSRWLockShared(&l_); }

private:
    SRWLOCK l_ = SRWLOCK_INIT;
};
#elif defined(__APPLE__)
#include <os/lock.h>
// os_unfair_lock: the owner inherits the priority of a waiting audio thread (no priority inversion).
// It has no shared mode, so readers from the UI take it exclusively (they hold it for microseconds).
class EngineLock {
public:
    void lock() { os_unfair_lock_lock(&l_); }
    void unlock() { os_unfair_lock_unlock(&l_); }
    void lockShared() { lock(); }
    void unlockShared() { unlock(); }

private:
    os_unfair_lock l_ = OS_UNFAIR_LOCK_INIT;
};
#else
#error "EngineLock: unsupported platform"
#endif

// Spin-wait hint for short busy loops (the telemetry seqlock reader).
inline void cpuRelax() {
#if defined(_WIN32)
    YieldProcessor();
#elif defined(__aarch64__)
    __builtin_arm_yield();
#else
    __builtin_ia32_pause();
#endif
}
