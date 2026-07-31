#include "mutex.h"

#include "fiber.h"
#include "poller.h"

#include <std/tst/ut.h>
#include <std/thr/poll_fd.h>
#include <std/thr/runable.h>
#include <std/mem/obj_pool.h>
#include <std/mem/small_obj_allocator.h>

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
        Scheduler* const scheduler = Scheduler::create(*pool, *SmallObjAllocator::create(pool.mutPtr()), poller);
        FiberMutex mutex;
        bool done = false;
        auto body = makeRunable([&] {
            mutex.lock(*scheduler);
            mutex.unlock();
            done = true;
        });
        scheduler->spawn(body);
        STD_INSIST(done);
        STD_INSIST(!mutex.held);
    }

    STD_TEST(WaitersResumeInFifoOrder) {
        ObjPool::Ref pool = ObjPool::fromMemory();
        InertPoller poller;
        Scheduler* const scheduler = Scheduler::create(*pool, *SmallObjAllocator::create(pool.mutPtr()), poller);
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
        scheduler->spawn(ownerBody);
        scheduler->spawn(firstBody);
        scheduler->spawn(secondBody);
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
        Scheduler* const scheduler = Scheduler::create(*pool, *SmallObjAllocator::create(pool.mutPtr()), poller);
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
        scheduler->spawn(waiterBody);
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

    STD_TEST(UnrelatedWakeDoesNotGrant) {
        ObjPool::Ref pool = ObjPool::fromMemory();
        InertPoller poller;
        Scheduler* const scheduler = Scheduler::create(*pool, *SmallObjAllocator::create(pool.mutPtr()), poller);
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
        scheduler->spawn(waiterBody);
        waiter->wake();
        STD_INSIST(!entered);
        mutex.unlock();
        STD_INSIST(entered);
        STD_INSIST(!mutex.held);
    }
}
