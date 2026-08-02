#include "input.h"

#include "fiber.h"

#include <std/str/view.h>
#include <std/thr/runable.h>
#include <std/mem/obj_pool.h>

#include <deque>
#include <string>
#include <utility>

using namespace plt;
using namespace stl;

namespace {
    struct FiberSinkImpl;

    struct SinkEvent {
        enum class Type : u8 {
            Key,
            Text,
            Preedit,
            PointerMotion,
            PointerButton,
            Scroll,
            Focus,
            PointerPresence,
            Flush
        };

        Type type = Type::Flush;
        KeyInput key{};
        TextInput text{};
        PointerMotionInput motion{};
        PointerButtonInput button{};
        ScrollInput scroll{};
        bool flag = false;
        i32 cursorBegin = -1;
        i32 cursorEnd = -1;
        std::string payload;
    };

    struct SinkPump final: public Runable {
        explicit SinkPump(FiberSinkImpl* sink);

        void run() override;

        FiberSinkImpl* sink;
    };

    struct FiberSinkImpl final: public InputSink {
        FiberSinkImpl(Scheduler& scheduler, InputSink& target);

        void key(const KeyInput& input) override;
        void text(const TextInput& input) override;
        void preedit(StringView text, i32 cursorBegin, i32 cursorEnd) override;
        void pointerMotion(const PointerMotionInput& input) override;
        void pointerButton(const PointerButtonInput& input) override;
        void scroll(const ScrollInput& input) override;
        void focus(bool focused) override;
        void pointerPresence(bool present) override;
        void flush() override;

        void push(SinkEvent&& event);
        void deliver(const SinkEvent& event);

        Scheduler& scheduler;
        InputSink& target;
        SinkPump pump;
        std::deque<SinkEvent> queue;
        Fiber* fiber = nullptr;
        // Deliveries run through client handlers down to the PTY write, so
        // the pump is not a light fiber.
        alignas(16) u8 stack[64 * 1024];
    };
}

SinkPump::SinkPump(FiberSinkImpl* sink_)
    : sink(sink_)
{
}

void SinkPump::run() {
    FiberSinkImpl& impl = *sink;
    impl.fiber = impl.scheduler.current();
    for (;;) {
        while (impl.queue.empty()) {
            impl.fiber->park();
        }
        const SinkEvent event = std::move(impl.queue.front());
        impl.queue.pop_front();
        impl.deliver(event);
    }
}

FiberSinkImpl::FiberSinkImpl(Scheduler& scheduler_, InputSink& target_)
    : scheduler(scheduler_)
    , target(target_)
    , pump(this)
{
}

void FiberSinkImpl::push(SinkEvent&& event) {
    queue.push_back(std::move(event));
    if (fiber != nullptr) {
        fiber->wake();
    }
}

void FiberSinkImpl::deliver(const SinkEvent& event) {
    const StringView payload((const u8*)(event.payload.data()), event.payload.size());
    switch (event.type) {
        case SinkEvent::Type::Key: {
            target.key(event.key);
            break;
        }
        case SinkEvent::Type::Text: {
            target.text(event.text);
            break;
        }
        case SinkEvent::Type::Preedit: {
            target.preedit(payload, event.cursorBegin, event.cursorEnd);
            break;
        }
        case SinkEvent::Type::PointerMotion: {
            target.pointerMotion(event.motion);
            break;
        }
        case SinkEvent::Type::PointerButton: {
            target.pointerButton(event.button);
            break;
        }
        case SinkEvent::Type::Scroll: {
            target.scroll(event.scroll);
            break;
        }
        case SinkEvent::Type::Focus: {
            target.focus(event.flag);
            break;
        }
        case SinkEvent::Type::PointerPresence: {
            target.pointerPresence(event.flag);
            break;
        }
        case SinkEvent::Type::Flush: {
            target.flush();
            break;
        }
    }
}

void FiberSinkImpl::key(const KeyInput& input) {
    SinkEvent event;
    event.type = SinkEvent::Type::Key;
    event.key = input;
    push(std::move(event));
}

void FiberSinkImpl::text(const TextInput& input) {
    SinkEvent event;
    event.type = SinkEvent::Type::Text;
    event.text = input;
    push(std::move(event));
}

void FiberSinkImpl::preedit(StringView text, i32 cursorBegin, i32 cursorEnd) {
    SinkEvent event;
    event.type = SinkEvent::Type::Preedit;
    event.payload.assign((const char*)(text.data()), text.length());
    event.cursorBegin = cursorBegin;
    event.cursorEnd = cursorEnd;
    push(std::move(event));
}

void FiberSinkImpl::pointerMotion(const PointerMotionInput& input) {
    SinkEvent event;
    event.type = SinkEvent::Type::PointerMotion;
    event.motion = input;
    push(std::move(event));
}

void FiberSinkImpl::pointerButton(const PointerButtonInput& input) {
    SinkEvent event;
    event.type = SinkEvent::Type::PointerButton;
    event.button = input;
    push(std::move(event));
}

void FiberSinkImpl::scroll(const ScrollInput& input) {
    SinkEvent event;
    event.type = SinkEvent::Type::Scroll;
    event.scroll = input;
    push(std::move(event));
}

void FiberSinkImpl::focus(bool focused) {
    SinkEvent event;
    event.type = SinkEvent::Type::Focus;
    event.flag = focused;
    push(std::move(event));
}

void FiberSinkImpl::pointerPresence(bool present) {
    SinkEvent event;
    event.type = SinkEvent::Type::PointerPresence;
    event.flag = present;
    push(std::move(event));
}

void FiberSinkImpl::flush() {
    SinkEvent event;
    event.type = SinkEvent::Type::Flush;
    push(std::move(event));
}

InputSink* plt::createFiberInputSink(ObjPool& owner, Scheduler& scheduler, InputSink& target) {
    FiberSinkImpl* const sink = owner.make<FiberSinkImpl>(scheduler, target);
    scheduler.spawn(sink->pump, sink->stack, sizeof(sink->stack));
    return sink;
}
