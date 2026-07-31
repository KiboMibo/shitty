#include "fiber.h"

#include "poller.h"

#include <std/thr/context.h>
#include <std/thr/poll_fd.h>
#include <std/thr/runable.h>
#include <std/mem/obj_pool.h>

#include <alloca.h>
#include <new>

using namespace plt;
using namespace stl;

namespace {
    constexpr size_t stackAlign = 16;

    size_t aligned(size_t size) {
        return (size + stackAlign - 1) & ~(stackAlign - 1);
    }

    struct SchedulerImpl;

    struct FiberImpl final: public Fiber, public PollCallback, public TimerCallback, public Runable {
        FiberImpl(SchedulerImpl& scheduler, Runable& entry);

        void ready(PollFD event) override;
        void ready() override;
        void run() override;
        void park() override;
        void wake() override;

        void block();

        SchedulerImpl& scheduler;
        Runable& entry;
        Context* context = nullptr;
        Context* resumeTo = nullptr;
        bool fdReady = false;
        bool timerFired = false;
        bool parked = false;
        bool wakePending = false;
        bool finished = false;
    };

    struct SchedulerImpl final: public Scheduler {
        explicit SchedulerImpl(Poller& poller);

        void spawn(Runable& entry, void* stack, size_t size) override;
        bool awaitReadable(int fd, u64 timeoutUs) override;
        bool awaitWritable(int fd, u64 timeoutUs) override;
        void sleep(u64 timeoutUs) override;
        void yield() override;
        bool inFiber() const override;
        Fiber* current() override;

        bool awaitFd(int fd, u32 flags, u64 timeoutUs);
        void resume(FiberImpl& fiber);

        Poller& poller;
        FiberImpl* active = nullptr;
    };
}

FiberImpl::FiberImpl(SchedulerImpl& scheduler_, Runable& entry_)
    : scheduler(scheduler_)
    , entry(entry_)
{
}

void FiberImpl::run() {
    entry.run();
    finished = true;
    // The final switch out; the caller owns the stack and may reuse it
    // afterwards, so nothing may run on this stack again.
    context->switchTo(*resumeTo);
}

void FiberImpl::ready(PollFD) {
    fdReady = true;
    scheduler.resume(*this);
}

void FiberImpl::ready() {
    timerFired = true;
    scheduler.resume(*this);
}

void FiberImpl::park() {
    if (wakePending) {
        wakePending = false;
        return;
    }
    parked = true;
    block();
}

void FiberImpl::wake() {
    if (!parked) {
        wakePending = true;
        return;
    }
    parked = false;
    scheduler.resume(*this);
}

void FiberImpl::block() {
    context->switchTo(*resumeTo);
}

SchedulerImpl::SchedulerImpl(Poller& poller_)
    : poller(poller_)
{
}

void SchedulerImpl::resume(FiberImpl& fiber) {
    // The host context lives on the resumer's stack frame, which stays alive
    // for as long as the fiber runs; nested resumes each bring their own.
    Context* const host = Context::create(alloca(Context::implSize()));
    FiberImpl* const previous = active;
    active = &fiber;
    fiber.resumeTo = host;
    host->switchTo(*fiber.context);
    active = previous;
}

void SchedulerImpl::spawn(Runable& entry, void* stack, size_t size) {
    // The control block and its context implementation are carved from the
    // base of the provided stack; the rest is the running stack.
    u8* const base = static_cast<u8*>(stack);
    const size_t reserved = aligned(sizeof(FiberImpl)) + aligned(Context::implSize());
    FiberImpl* const fiber = new (base) FiberImpl(*this, entry);
    fiber->context = Context::create(base + aligned(sizeof(FiberImpl)), base + reserved, size - reserved, *fiber);
    resume(*fiber);
}

bool SchedulerImpl::awaitFd(int fd, u32 flags, u64 timeoutUs) {
    FiberImpl& fiber = *active;
    fiber.fdReady = false;
    fiber.timerFired = false;
    poller.arm(
        {
            .fd = fd,
            .flags = flags,
        },
        fiber
    );
    if (timeoutUs != 0) {
        poller.timeout(timeoutUs, fiber);
    }
    fiber.block();
    if (fiber.fdReady) {
        if (timeoutUs != 0) {
            poller.cancel(fiber);
        }
        return true;
    }
    poller.disarm(fd);
    return false;
}

bool SchedulerImpl::awaitReadable(int fd, u64 timeoutUs) {
    return awaitFd(fd, PollFlag::In, timeoutUs);
}

bool SchedulerImpl::awaitWritable(int fd, u64 timeoutUs) {
    return awaitFd(fd, PollFlag::Out, timeoutUs);
}

void SchedulerImpl::sleep(u64 timeoutUs) {
    FiberImpl& fiber = *active;
    fiber.timerFired = false;
    poller.timeout(timeoutUs, fiber);
    fiber.block();
}

void SchedulerImpl::yield() {
    sleep(0);
}

bool SchedulerImpl::inFiber() const {
    return active != nullptr;
}

Fiber* SchedulerImpl::current() {
    return active;
}

Scheduler* Scheduler::create(ObjPool& owner, Poller& poller) {
    return owner.make<SchedulerImpl>(poller);
}
