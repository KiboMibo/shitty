#include "mutex.h"

#include "fiber.h"

#include <std/lib/node.h>

using namespace plt;
using namespace stl;

namespace {
    struct MutexWaiter final: public IntrusiveNode {
        Fiber* fiber = nullptr;
        bool granted = false;
    };
}

void FiberMutex::lock(Scheduler& scheduler) {
    if (tryLock()) {
        owner = scheduler.current();
        return;
    }
    MutexWaiter waiter;
    waiter.fiber = scheduler.current();
    waiters.pushBack(&waiter);
    // A remembered wake from an unrelated park/wake pair may end the park
    // early; only the grant made by unlock() releases the loop.
    while (!waiter.granted) {
        waiter.fiber->park();
    }
}

bool FiberMutex::tryLock() {
    if (held) {
        return false;
    }
    held = true;
    return true;
}

void FiberMutex::unlock() {
    if (waiters.empty()) {
        held = false;
        owner = nullptr;
        return;
    }
    // Ownership passes directly to the oldest waiter: held stays true, so
    // writers arriving between this unlock and the waiter resuming still
    // queue behind it.
    MutexWaiter* const waiter = static_cast<MutexWaiter*>(waiters.popFront());
    waiter->granted = true;
    owner = waiter->fiber;
    waiter->fiber->wake();
}

bool FiberMutex::heldByCurrent(Scheduler& scheduler) const {
    return held && owner != nullptr && owner == scheduler.current();
}

LockGuard::LockGuard(FiberMutex& mutex_, Scheduler& scheduler)
    : mutex(mutex_)
{
    mutex.lock(scheduler);
}

LockGuard::~LockGuard() {
    mutex.unlock();
}
