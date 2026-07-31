#include "fiber.h"
#include "poller.h"

#include <std/tst/ut.h>
#include <std/thr/poll_fd.h>
#include <std/thr/runable.h>
#include <std/mem/obj_pool.h>

using namespace plt;
using namespace stl;

namespace {
    struct ManualPoller final: Poller {
        void arm(PollWaiter& waiter) override {
            armedFd = waiter.fd.fd;
            fdCallback = waiter.callback;
        }

        void cancel(PollWaiter&) override {
            armedFd = -1;
            fdCallback = nullptr;
        }

        void timeout(u64, TimerCallback& callback) override {
            timer = &callback;
        }

        void deadline(u64, TimerCallback& callback) override {
            timer = &callback;
        }

        void cancel(TimerCallback&) override {
            timer = nullptr;
        }

        void defer(TimerCallback& callback) override {
            timer = &callback;
        }

        void fireFd() {
            PollCallback* const callback = fdCallback;
            fdCallback = nullptr;
            armedFd = -1;
            callback->ready(PollFD{});
        }

        void fireTimer() {
            TimerCallback* const callback = timer;
            timer = nullptr;
            callback->ready();
        }

        int armedFd = -1;
        PollCallback* fdCallback = nullptr;
        TimerCallback* timer = nullptr;
    };
}

STD_TEST_SUITE(FiberScheduler) {
    STD_TEST(SpawnRunsImmediately) {
        ObjPool::Ref pool = ObjPool::fromMemory();
        ManualPoller poller;
        Scheduler* const scheduler = Scheduler::create(*pool, poller);
        int steps = 0;
        auto body = makeRunable([&] {
            ++steps;
        });
        alignas(16) static u8 bodyStack[lightFiberStack];
        scheduler->spawn(body, bodyStack, sizeof(bodyStack));
        STD_INSIST(steps == 1);
        STD_INSIST(!scheduler->inFiber());
    }

    STD_TEST(AwaitResumesOnFd) {
        ObjPool::Ref pool = ObjPool::fromMemory();
        ManualPoller poller;
        Scheduler* const scheduler = Scheduler::create(*pool, poller);
        int phase = 0;
        bool ready = false;
        auto body = makeRunable([&] {
            phase = 1;
            ready = scheduler->awaitReadable(7, 1000);
            phase = 2;
        });
        alignas(16) static u8 bodyStack[lightFiberStack];
        scheduler->spawn(body, bodyStack, sizeof(bodyStack));
        STD_INSIST(phase == 1);
        STD_INSIST(poller.armedFd == 7);
        poller.fireFd();
        STD_INSIST(phase == 2);
        STD_INSIST(ready);
        STD_INSIST(poller.timer == nullptr);
    }

    STD_TEST(AwaitTimesOut) {
        ObjPool::Ref pool = ObjPool::fromMemory();
        ManualPoller poller;
        Scheduler* const scheduler = Scheduler::create(*pool, poller);
        bool ready = true;
        bool complete = false;
        auto body = makeRunable([&] {
            ready = scheduler->awaitReadable(7, 1000);
            complete = true;
        });
        alignas(16) static u8 bodyStack[lightFiberStack];
        scheduler->spawn(body, bodyStack, sizeof(bodyStack));
        poller.fireTimer();
        STD_INSIST(complete);
        STD_INSIST(!ready);
        STD_INSIST(poller.fdCallback == nullptr);
    }

    STD_TEST(SleepAndInterleave) {
        ObjPool::Ref pool = ObjPool::fromMemory();
        ManualPoller poller;
        Scheduler* const scheduler = Scheduler::create(*pool, poller);
        int order = 0;
        int firstAt = 0;
        int loopAt = 0;
        auto body = makeRunable([&] {
            firstAt = ++order;
            scheduler->sleep(1000);
            firstAt = ++order;
        });
        alignas(16) static u8 bodyStack[lightFiberStack];
        scheduler->spawn(body, bodyStack, sizeof(bodyStack));
        // The loop runs while the fiber sleeps.
        loopAt = ++order;
        poller.fireTimer();
        STD_INSIST(loopAt == 2);
        STD_INSIST(firstAt == 3);
    }

    STD_TEST(ParkAndWake) {
        ObjPool::Ref pool = ObjPool::fromMemory();
        ManualPoller poller;
        Scheduler* const scheduler = Scheduler::create(*pool, poller);
        Fiber* handle = nullptr;
        int phase = 0;
        auto body = makeRunable([&] {
            handle = scheduler->current();
            phase = 1;
            scheduler->current()->park();
            phase = 2;
            scheduler->current()->park();
            phase = 3;
        });
        alignas(16) static u8 bodyStack[lightFiberStack];
        scheduler->spawn(body, bodyStack, sizeof(bodyStack));
        STD_INSIST(phase == 1);
        handle->wake();
        STD_INSIST(phase == 2);
        handle->wake();
        STD_INSIST(phase == 3);
    }

    STD_TEST(WakeBeforeParkIsRemembered) {
        ObjPool::Ref pool = ObjPool::fromMemory();
        ManualPoller poller;
        Scheduler* const scheduler = Scheduler::create(*pool, poller);
        Fiber* handle = nullptr;
        bool woken = false;
        auto body = makeRunable([&] {
            handle = scheduler->current();
            scheduler->sleep(1000);
            scheduler->current()->park();
            woken = true;
        });
        alignas(16) static u8 bodyStack[lightFiberStack];
        scheduler->spawn(body, bodyStack, sizeof(bodyStack));
        handle->wake();
        STD_INSIST(!woken);
        poller.fireTimer();
        STD_INSIST(woken);
    }

    STD_TEST(NestedSpawn) {
        ObjPool::Ref pool = ObjPool::fromMemory();
        ManualPoller poller;
        Scheduler* const scheduler = Scheduler::create(*pool, poller);
        bool innerBlocked = false;
        bool innerDone = false;
        bool outerDone = false;
        auto inner = makeRunable([&] {
            innerBlocked = true;
            scheduler->sleep(1000);
            innerDone = true;
        });
        auto outer = makeRunable([&] {
            alignas(16) static u8 innerStack[lightFiberStack];
        scheduler->spawn(inner, innerStack, sizeof(innerStack));
            STD_INSIST(scheduler->inFiber());
            outerDone = true;
        });
        alignas(16) static u8 outerStack[lightFiberStack];
        scheduler->spawn(outer, outerStack, sizeof(outerStack));
        STD_INSIST(innerBlocked);
        STD_INSIST(outerDone);
        STD_INSIST(!innerDone);
        poller.fireTimer();
        STD_INSIST(innerDone);
    }
}
