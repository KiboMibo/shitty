#include "poller.h"

#include "timer_queue.h"

#include <std/sys/crt.h>
#include <std/str/view.h>
#include <std/sys/throw.h>
#include <std/alg/minmax.h>
#include <std/alg/xchg.h>
#include <std/lib/list.h>
#include <std/lib/vector.h>
#include <std/mem/obj_pool.h>

#include <cerrno>
#include <climits>
#include <poll.h>

using namespace plt;
using namespace stl;

namespace {
    struct PollerLoopImpl final: public PollerLoop {
        explicit PollerLoopImpl(ObjPool& owner);

        void arm(PollWaiter& waiter) override;
        void cancel(PollWaiter& waiter) override;
        void timeout(u64 microseconds, TimerCallback& callback) override;
        void deadline(u64 monotonicMicroseconds, TimerCallback& callback) override;
        void cancel(TimerCallback& callback) override;
        void defer(TimerCallback& callback) override;

        void wait(u64 monotonicDeadline) override;
        void dispatchTimers() override;
        u64 nextDeadline() const override;

        IntrusiveList armed;
        Vector<struct pollfd> pollFDs;
        Vector<PollWaiter*> pending;
        Vector<TimerCallback*> deferred;
        Vector<TimerCallback*> deferredRound;
        TimerQueue timers;
    };
}

PollerLoopImpl::PollerLoopImpl(ObjPool& owner)
    : timers(owner)
{
}

void PollerLoopImpl::arm(PollWaiter& waiter) {
    waiter.unlink();
    armed.pushBack(&waiter);
}

void PollerLoopImpl::cancel(PollWaiter& waiter) {
    // Works whichever list currently holds the node, including the ready
    // list of a dispatch round in progress.
    waiter.unlink();
}

void PollerLoopImpl::timeout(u64 microseconds, TimerCallback& callback) {
    timers.schedule(monotonicNowUs() + microseconds, callback);
}

void PollerLoopImpl::deadline(u64 monotonicMicroseconds, TimerCallback& callback) {
    if (monotonicMicroseconds == 0) {
        monotonicMicroseconds = monotonicNowUs();
    }
    timers.schedule(monotonicMicroseconds, callback);
}

void PollerLoopImpl::cancel(TimerCallback& callback) {
    timers.cancel(callback);
    for (size_t index = 0; index != deferred.length(); ++index) {
        if (deferred[index] == &callback) {
            deferred.mutData()[index] = nullptr;
        }
    }
    for (size_t index = 0; index != deferredRound.length(); ++index) {
        if (deferredRound[index] == &callback) {
            deferredRound.mutData()[index] = nullptr;
        }
    }
}

void PollerLoopImpl::defer(TimerCallback& callback) {
    deferred.pushBack(&callback);
}

u64 PollerLoopImpl::nextDeadline() const {
    // A pending deferred callback turns the next poll into a non-blocking
    // round: descriptors are still polled, then the callback runs.
    return deferred.empty() ? timers.nextDeadline() : 0;
}

void PollerLoopImpl::dispatchTimers() {
    timers.dispatch(monotonicNowUs());
}

void PollerLoopImpl::wait(u64 monotonicDeadline) {
    pollFDs.clear();
    pending.clear();
    for (IntrusiveNode* node = armed.mutFront(); node != armed.mutEnd(); node = node->next) {
        PollWaiter* const waiter = static_cast<PollWaiter*>(node);
        pending.pushBack(waiter);
        pollFDs.pushBack({
            .fd = waiter->fd.fd,
            .events = waiter->fd.toPollEvents(),
            .revents = 0,
        });
    }

    int timeoutMilliseconds = -1;
    if (monotonicDeadline != UINT64_MAX) {
        const u64 now = monotonicNowUs();
        const u64 timeoutUs = monotonicDeadline > now ? monotonicDeadline - now : 0;
        timeoutMilliseconds = (int)(min<u64>((timeoutUs + 999) / 1000, INT_MAX));
    }
    int result;
    do {
        result = ::poll(pollFDs.mutData(), pollFDs.length(), timeoutMilliseconds);
    } while (result < 0 && errno == EINTR);
    if (result < 0) {
        Errno(errno == 0 ? EINVAL : errno).raise(StringView(u8"poll failed"));
    }

    // Detach every ready waiter before the first callback runs: a callback
    // that cancels or re-arms another waiter simply pulls it out of this
    // round's list.
    IntrusiveList ready;
    for (size_t index = 0; index != pending.length(); ++index) {
        if (pollFDs[index].revents == 0) {
            continue;
        }
        PollWaiter* const waiter = pending[index];
        waiter->readyFlags = pollFDs[index].revents;
        waiter->unlink();
        ready.pushBack(waiter);
    }
    while (!ready.empty()) {
        PollWaiter* const waiter = static_cast<PollWaiter*>(ready.popFront());
        waiter->callback->ready({
            .fd = waiter->fd.fd,
            .flags = PollFD::fromPollEvents((short)(waiter->readyFlags)),
        });
    }

    // Deferred callbacks run once the round's descriptor waiters have been
    // dispatched; a callback deferring again lands in the next round.
    xchg(deferredRound, deferred);
    for (size_t index = 0; index != deferredRound.length(); ++index) {
        TimerCallback* const callback = deferredRound[index];
        if (callback != nullptr) {
            callback->ready();
        }
    }
    deferredRound.clear();
}

PollerLoop* PollerLoop::create(ObjPool& owner) {
    return owner.make<PollerLoopImpl>(owner);
}
