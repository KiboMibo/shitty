#include "poller.h"

#include "timer_queue.h"

#include <std/sys/crt.h>
#include <std/sys/throw.h>
#include <std/str/view.h>
#include <std/alg/minmax.h>
#include <std/sym/i_map.h>
#include <std/lib/vector.h>
#include <std/mem/obj_pool.h>
#include <std/thr/poll_fd.h>

#include <cerrno>
#include <climits>
#include <poll.h>

using namespace plt;
using namespace stl;

namespace {
    struct ArmedFD {
        PollFD fd;
        PollCallback* callback = nullptr;
        u64 generation = 0;
    };

    struct ReadyFD {
        PollFD fd;
        PollCallback* callback = nullptr;
        u64 generation = 0;
    };

    struct PollerLoopImpl final: public PollerLoop {
        explicit PollerLoopImpl(ObjPool& owner);

        void arm(PollFD fd, PollCallback& callback) override;
        void disarm(int fd) override;
        void timeout(u64 microseconds, TimerCallback& callback) override;
        void deadline(u64 monotonicMicroseconds, TimerCallback& callback) override;
        void cancel(TimerCallback& callback) override;

        void wait(u64 monotonicDeadline) override;
        void dispatchTimers() override;
        u64 nextDeadline() const override;

        u64 allocateGeneration();

        IntMap<ArmedFD> armed;
        Vector<struct pollfd> pollFDs;
        Vector<ReadyFD> readyFDs;
        TimerQueue timers;
        u64 nextGeneration = 1;
    };
}

PollerLoopImpl::PollerLoopImpl(ObjPool& owner)
    : armed(ObjPool::create(&owner))
    , timers(owner)
{
}

u64 PollerLoopImpl::allocateGeneration() {
    const u64 result = nextGeneration++;
    if (nextGeneration == 0) {
        nextGeneration = 1;
    }
    return result;
}

void PollerLoopImpl::arm(PollFD fd, PollCallback& callback) {
    armed[fd.fd] = {
        .fd = fd,
        .callback = &callback,
        .generation = allocateGeneration(),
    };
}

void PollerLoopImpl::disarm(int fd) {
    armed.erase(fd);
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
}

u64 PollerLoopImpl::nextDeadline() const {
    return timers.nextDeadline();
}

void PollerLoopImpl::dispatchTimers() {
    timers.dispatch(monotonicNowUs());
}

void PollerLoopImpl::wait(u64 monotonicDeadline) {
    pollFDs.clear();
    armed.visit([this](const ArmedFD& source) {
        pollFDs.pushBack({
            .fd = source.fd.fd,
            .events = source.fd.toPollEvents(),
            .revents = 0,
        });
    });

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

    readyFDs.clear();
    for (size_t index = 0; index != pollFDs.length(); ++index) {
        const struct pollfd& source = pollFDs[index];
        ArmedFD* registration = armed.find(source.fd);
        if (source.revents == 0 || registration == nullptr) {
            continue;
        }
        readyFDs.pushBack({
            .fd =
                {
                    .fd = source.fd,
                    .flags = PollFD::fromPollEvents(source.revents),
                },
            .callback = registration->callback,
            .generation = registration->generation,
        });
    }
    for (const ReadyFD& ready : readyFDs) {
        ArmedFD* const registration = armed.find(ready.fd.fd);
        if (registration == nullptr || registration->callback != ready.callback || registration->generation != ready.generation) {
            continue;
        }
        armed.erase(ready.fd.fd);
        ready.callback->ready(ready.fd);
    }
    readyFDs.clear();
}

PollerLoop* PollerLoop::create(ObjPool& owner) {
    return owner.make<PollerLoopImpl>(owner);
}
