#pragma once

#include <std/sys/types.h>

namespace stl {
    class ObjPool;
    struct Runable;
}

namespace plt {
    struct Poller;

    // A running fiber. park() blocks the fiber until wake() and may be
    // called only from the fiber itself; wake() may be called from anywhere
    // on the platform thread. Being single-threaded there is no publication
    // race to defend against: a wake that arrives while the fiber is not
    // parked is remembered and the next park returns immediately.
    struct Fiber {
        virtual void park() = 0;
        // Parks until wake() or the deadline; false when the wait timed
        // out. May be called only from the fiber itself.
        virtual bool parkFor(u64 timeoutUs) = 0;
        virtual void wake() = 0;
    };

    // A single-threaded cooperative fiber scheduler married to a Poller.
    // Fibers run on the platform thread: a blocked fiber resumes inside the
    // poller callback that made it runnable and switches back before the
    // loop continues, so fibers, callbacks and the event loop interleave
    // freely. Everything a fiber touches lives on its own stack, which lets
    // deeply nested code block on I/O without stopping the loop.
    struct Scheduler {
        // Runs entry immediately on the caller-provided stack until it
        // first blocks or returns. The control block lives at the base of
        // that stack: both entry and the memory stay the caller's, must
        // outlive the fiber, and the engine never frees or recycles them.
        // Once the fiber finishes the memory may be reused for the next
        // spawn; a caller with churn keeps its own free list.
        virtual void spawn(stl::Runable& entry, void* stack, size_t size) = 0;

        // The calls below block the calling fiber only and must not be used
        // outside one. false means the wait timed out; a timeout of 0 waits
        // without a deadline.
        virtual bool awaitReadable(int fd, u64 timeoutUs) = 0;
        virtual bool awaitWritable(int fd, u64 timeoutUs) = 0;
        virtual void sleep(u64 timeoutUs) = 0;
        virtual void yield() = 0;
        // The running fiber, nullptr outside any.
        virtual Fiber* current() = 0;

        static Scheduler* create(stl::ObjPool& owner, Poller& poller);
    };

    // Enough for a leaf fiber that keeps only chunk buffers of a few
    // kilobytes on its stack; give parser- or renderer-deep fibers more.
    inline constexpr size_t lightFiberStack = 32 * 1024;
}
