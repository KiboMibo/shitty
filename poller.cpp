/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "poller.h"

#include "composer.h"
#include "listener.h"

#include <platform/platform.h>

#include <std/lib/vector.h>
#include <std/mem/obj_pool.h>
#include <std/sym/i_map.h>
#include <std/sys/crt.h>

#include <climits>
#include <poll.h>

using namespace stl;

namespace {
    struct ArmedFD {
        int fd;
        int mode;
    };

    struct PollerImpl final: public Poller {
        explicit PollerImpl(Composer& composer);

        void attach(plt::Platform* platform) override;
        void arm(int fd, int mode) override;
        void disarm(int fd) override;
        void timeout(u64 microseconds) override;
        void deadline(u64 monotonicMicroseconds) override;
        int poll(struct pollfd* sourceFDs, size_t sourceCount, double* timeout) override;
        void dispatch() override;

        static short toNative(int mode);
        static int fromNative(short events);
        static int toMilliseconds(double seconds);

        Composer& composer;
        IntMap<ArmedFD> armed;
        Vector<struct pollfd> waiting;
        Vector<struct pollfd> pendingSource;
        Vector<FDReady> ready;
        plt::Platform* platform = nullptr;
        u64 minDeadline = 0;
        bool sourcePending = false;
        bool timeoutReady = false;
    };
}

PollerImpl::PollerImpl(Composer& composer_)
    : composer(composer_)
    , armed(ObjPool::create(composer.pool))
{
}

void PollerImpl::attach(plt::Platform* value) {
    platform = value;
    if (platform == nullptr) {
        return;
    }
    armed.visit([this](const ArmedFD& current) {
        platform->arm(current.fd, current.mode);
    });
    if (minDeadline != 0) {
        platform->deadline(minDeadline);
    }
}

void PollerImpl::arm(int fd, int mode) {
    armed[fd] = {fd, mode};
    if (platform != nullptr) {
        platform->arm(fd, mode);
    }
}

void PollerImpl::disarm(int fd) {
    armed.erase(fd);
    if (platform != nullptr) {
        platform->disarm(fd);
    }
}

void PollerImpl::timeout(u64 microseconds) {
    if (platform != nullptr) {
        platform->timeout(microseconds);
        return;
    }
    deadline(monotonicNowUs() + microseconds);
}

void PollerImpl::deadline(u64 monotonicMicroseconds) {
    if (platform != nullptr) {
        platform->deadline(monotonicMicroseconds);
        return;
    }
    if (monotonicMicroseconds == 0) {
        monotonicMicroseconds = monotonicNowUs();
    }
    if (minDeadline == 0 || monotonicMicroseconds < minDeadline) {
        minDeadline = monotonicMicroseconds;
    }
}

short PollerImpl::toNative(int mode) {
    short events = 0;
    if (mode & PollRead) {
        events |= POLLIN;
    }
    if (mode & PollWrite) {
        events |= POLLOUT;
    }
    return events;
}

int PollerImpl::fromNative(short events) {
    int mode = 0;
    if (events & POLLIN) {
        mode |= PollRead;
    }
    if (events & POLLOUT) {
        mode |= PollWrite;
    }
    if (events & (POLLERR | POLLNVAL)) {
        mode |= PollError;
    }
    if (events & POLLHUP) {
        mode |= PollHangup;
    }
    return mode;
}

int PollerImpl::toMilliseconds(double seconds) {
    if (seconds <= 0.0) {
        return 0;
    }
    if (seconds >= INT_MAX / 1000.0) {
        return INT_MAX;
    }
    return (int)(seconds * 1000.0 + 0.999);
}

int PollerImpl::poll(struct pollfd* sourceFDs, size_t sourceCount, double* timeout) {
    if (sourcePending) {
        int result = 0;
        for (size_t index = 0; index != sourceCount; ++index) {
            sourceFDs[index].revents = pendingSource[index].revents;
            result += sourceFDs[index].revents != 0;
        }
        pendingSource.clear();
        sourcePending = false;
        return result;
    }
    if (!ready.empty() || timeoutReady) {
        for (size_t index = 0; index != sourceCount; ++index) {
            sourceFDs[index].revents = 0;
        }
        if (timeout != nullptr) {
            *timeout = 0.0;
        }
        return 0;
    }

    waiting.clear();
    if (sourceCount != 0) {
        waiting.append(sourceFDs, sourceCount);
    }
    armed.visit([this](const ArmedFD& current) {
        waiting.pushBack({
            .fd = current.fd,
            .events = toNative(current.mode),
            .revents = 0,
        });
    });

    const u64 started = monotonicNowUs();
    double waitSeconds = timeout == nullptr ? 0.0 : *timeout;
    bool waitForever = timeout == nullptr;
    if (minDeadline != 0) {
        const double untilDeadline = minDeadline > started ? (minDeadline - started) / 1'000'000.0 : 0.0;
        if (waitForever || untilDeadline < waitSeconds) {
            waitSeconds = untilDeadline;
            waitForever = false;
        }
    }
    const int milliseconds = waitForever ? -1 : toMilliseconds(waitSeconds);
    const int result = ::poll(waiting.mutData(), waiting.length(), milliseconds);
    if (timeout != nullptr) {
        const double elapsed = (monotonicNowUs() - started) / 1'000'000.0;
        *timeout = elapsed < *timeout ? *timeout - elapsed : 0.0;
    }
    for (size_t index = 0; index != sourceCount; ++index) {
        sourceFDs[index].revents = waiting[index].revents;
    }
    bool externalReady = false;
    if (result > 0) {
        for (size_t index = sourceCount; index != waiting.length(); ++index) {
            const struct pollfd& source = waiting[index];
            if (source.revents == 0) {
                continue;
            }
            externalReady = true;
            FDReady ready{
                .fd = source.fd,
                .what = fromNative(source.revents),
            };
            // The caller may have prepared an event source read. Dispatching
            // here would deadlock a listener that enters that source again.
            this->ready.pushBack(ready);
        }
    }
    if (minDeadline != 0 && monotonicNowUs() >= minDeadline) {
        minDeadline = 0;
        timeoutReady = true;
    }
    if (externalReady) {
        pendingSource.clear();
        if (sourceCount != 0) {
            pendingSource.append(sourceFDs, sourceCount);
        }
        sourcePending = true;
        for (size_t index = 0; index != sourceCount; ++index) {
            sourceFDs[index].revents = 0;
        }
    }
    return result;
}

void PollerImpl::dispatch() {
    for (const FDReady& event : ready) {
        for (IntrusiveNode* node = composer.onFDReady.mutFront(); node != composer.onFDReady.mutEnd();) {
            Listener* const listener = static_cast<Listener*>(node);
            node = node->next;
            listener->onListen((void*)(&event));
        }
    }
    ready.clear();

    if (!timeoutReady) {
        return;
    }
    timeoutReady = false;
    for (IntrusiveNode* node = composer.onTimeout.mutFront(); node != composer.onTimeout.mutEnd();) {
        Listener* const listener = static_cast<Listener*>(node);
        node = node->next;
        listener->onListen();
    }
}

Poller* Poller::create(Composer& composer) {
    return composer.pool->make<PollerImpl>(composer);
}
