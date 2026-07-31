#pragma once

#include <std/lib/list.h>

namespace plt {
    struct Fiber;
    struct Scheduler;

    // A cooperative FIFO mutex for fibers. The mutex itself is only the
    // wait list and a flag: each blocked lock() parks on a waiter node
    // living on the calling fiber's stack. unlock() hands the mutex to the
    // oldest waiter and resumes it inline; it may run outside a fiber.
    struct FiberMutex {
        // Blocks the calling fiber until the mutex is granted. May be
        // called only inside a fiber.
        void lock(Scheduler& scheduler);
        // Takes the mutex when it is free; never blocks.
        bool tryLock();
        void unlock();
        // True when the calling fiber holds the mutex; lets a writer called
        // from an owning transaction pass through instead of deadlocking.
        bool heldByCurrent(Scheduler& scheduler) const;

        stl::IntrusiveList waiters;
        Fiber* owner = nullptr;
        bool held = false;
    };

    // Scoped ownership of a FiberMutex: locks on construction, unlocks on
    // destruction, so a transaction releases the stream on every exit path.
    struct LockGuard {
        LockGuard(FiberMutex& mutex, Scheduler& scheduler);
        ~LockGuard();

        LockGuard(const LockGuard&) = delete;
        LockGuard& operator=(const LockGuard&) = delete;

        FiberMutex& mutex;
    };
}
