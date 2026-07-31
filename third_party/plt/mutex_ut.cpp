#include "mutex.h"

#include "fiber.h"
#include "poller.h"

#include <std/tst/ut.h>
#include <std/thr/poll_fd.h>
#include <std/thr/runable.h>
#include <std/mem/obj_pool.h>

using namespace plt;
using namespace stl;

namespace {
    struct InertPoller final: Poller {
        void arm(PollFD, PollCallback&) override {
        }

        void disarm(int) override {
        }

        void timeout(u64, TimerCallback&) override {
        }

        void deadline(u64, TimerCallback&) override {
        }

        void cancel(TimerCallback&) override {
        }
    };
}

STD_TEST_SUITE(FiberMutexSuite) {
    STD_TEST(TryLockTakesAndReleases) {
        FiberMutex mutex;
        STD_INSIST(mutex.tryLock());
        STD_INSIST(!mutex.tryLock());
        mutex.unlock();
        STD_INSIST(mutex.tryLock());
        mutex.unlock();
    }

    STD_TEST(UncontendedLockDoesNotBlock) {
        ObjPool::Ref pool = ObjPool::fromMemory();
        InertPoller poller;
        Scheduler* const scheduler = Scheduler::create(*pool, poller);
        FiberMutex mutex;
        bool done = false;
        auto body = makeRunable([&] {
            mutex.lock(*scheduler);
            mutex.unlock();
            done = true;
        });
        alignas(16) static u8 bodyStack[lightFiberStack];
        scheduler->spawn(body, bodyStack, sizeof(bodyStack));
        STD_INSIST(done);
        STD_INSIST(!mutex.held);
    }

    STD_TEST(WaitersResumeInFifoOrder) {
        ObjPool::Ref pool = ObjPool::fromMemory();
        InertPoller poller;
        Scheduler* const scheduler = Scheduler::create(*pool, poller);
        FiberMutex mutex;
        Fiber* owner = nullptr;
        int order = 0;
        int ownerAt = 0;
        int firstAt = 0;
        int secondAt = 0;
        auto ownerBody = makeRunable([&] {
            mutex.lock(*scheduler);
            owner = scheduler->current();
            owner->park();
            ownerAt = ++order;
            mutex.unlock();
        });
        auto firstBody = makeRunable([&] {
            mutex.lock(*scheduler);
            firstAt = ++order;
            mutex.unlock();
        });
        auto secondBody = makeRunable([&] {
            mutex.lock(*scheduler);
            secondAt = ++order;
            mutex.unlock();
        });
        alignas(16) static u8 ownerBodyStack[lightFiberStack];
        scheduler->spawn(ownerBody, ownerBodyStack, sizeof(ownerBodyStack));
        alignas(16) static u8 firstBodyStack[lightFiberStack];
        scheduler->spawn(firstBody, firstBodyStack, sizeof(firstBodyStack));
        alignas(16) static u8 secondBodyStack[lightFiberStack];
        scheduler->spawn(secondBody, secondBodyStack, sizeof(secondBodyStack));
        STD_INSIST(order == 0);
        owner->wake();
        STD_INSIST(ownerAt == 1);
        STD_INSIST(firstAt == 2);
        STD_INSIST(secondAt == 3);
        STD_INSIST(!mutex.held);
    }

    STD_TEST(HandoffKeepsMutexHeld) {
        ObjPool::Ref pool = ObjPool::fromMemory();
        InertPoller poller;
        Scheduler* const scheduler = Scheduler::create(*pool, poller);
        FiberMutex mutex;
        Fiber* waiter = nullptr;
        bool waiterDone = false;
        STD_INSIST(mutex.tryLock());
        auto waiterBody = makeRunable([&] {
            waiter = scheduler->current();
            mutex.lock(*scheduler);
            waiter->park();
            mutex.unlock();
            waiterDone = true;
        });
        alignas(16) static u8 waiterBodyStack[lightFiberStack];
        scheduler->spawn(waiterBody, waiterBodyStack, sizeof(waiterBodyStack));
        // The waiter resumes inside unlock(), takes the mutex over and
        // blocks while still holding it.
        mutex.unlock();
        STD_INSIST(!waiterDone);
        STD_INSIST(!mutex.tryLock());
        waiter->wake();
        STD_INSIST(waiterDone);
        STD_INSIST(mutex.tryLock());
        mutex.unlock();
    }

    STD_TEST(LockGuardReleasesOnScopeExit) {
        ObjPool::Ref pool = ObjPool::fromMemory();
        InertPoller poller;
        Scheduler* const scheduler = Scheduler::create(*pool, poller);
        FiberMutex mutex;
        bool inner = false;
        auto body = makeRunable([&] {
            const LockGuard guard(mutex, *scheduler);
            inner = mutex.held;
        });
        alignas(16) static u8 bodyStack[lightFiberStack];
        scheduler->spawn(body, bodyStack, sizeof(bodyStack));
        STD_INSIST(inner);
        STD_INSIST(!mutex.held);
    }

    STD_TEST(UnrelatedWakeDoesNotGrant) {
        ObjPool::Ref pool = ObjPool::fromMemory();
        InertPoller poller;
        Scheduler* const scheduler = Scheduler::create(*pool, poller);
        FiberMutex mutex;
        Fiber* waiter = nullptr;
        bool entered = false;
        STD_INSIST(mutex.tryLock());
        auto waiterBody = makeRunable([&] {
            waiter = scheduler->current();
            mutex.lock(*scheduler);
            entered = true;
            mutex.unlock();
        });
        alignas(16) static u8 waiterBodyStack[lightFiberStack];
        scheduler->spawn(waiterBody, waiterBodyStack, sizeof(waiterBodyStack));
        waiter->wake();
        STD_INSIST(!entered);
        mutex.unlock();
        STD_INSIST(entered);
        STD_INSIST(!mutex.held);
    }
}
