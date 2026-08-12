/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "session.h"

#include "composer.h"
#include "listener.h"
#include "pty.h"
#include "startup.h"
#include "vterm.h"

#include <plt/fiber.h>
#include <plt/platform.h>
#include <plt/platform_headless.h>

#include <std/ios/input.h>
#include <std/ios/out.h>
#include <std/ios/output.h>
#include <std/lib/vector.h>
#include <std/mem/obj_pool.h>
#include <std/tst/ut.h>

using namespace stl;

namespace {
    struct ParkInput final: public Input {
        explicit ParkInput(plt::Scheduler& scheduler_)
            : scheduler(scheduler_)
        {
        }

        size_t readImpl(void*, size_t) override {
            scheduler.current()->park();
            return 0;
        }

        plt::Scheduler& scheduler;
    };

    struct ParkOutput final: public Output {
        ParkOutput(plt::Scheduler& scheduler_, bool* entered_, bool* resumed_)
            : scheduler(scheduler_)
            , entered(entered_)
            , resumed(resumed_)
        {
        }

        size_t writeImpl(const void*, size_t size) override {
            if (entered == nullptr) {
                return size;
            }
            *entered = true;
            scheduler.current()->park();
            *resumed = true;
            return size;
        }

        plt::Scheduler& scheduler;
        bool* entered;
        bool* resumed;
    };

    struct StubHandle final: public PtyHandle {
        StubHandle(Composer& composer_, size_t* destroyed_, bool* writeEntered, bool* writeResumed)
            : composer(composer_)
            , destroyed(destroyed_)
            , input_(*composer.platform->scheduler())
            , parkedOutput_(*composer.platform->scheduler(), writeEntered, writeResumed)
            , output_(writeEntered != nullptr ? static_cast<Output*>(&parkedOutput_) : composer.ptyOutput)
        {
        }

        ~StubHandle() noexcept {
            ++*destroyed;
        }

        Input* input() override {
            return &input_;
        }

        Output* output() override {
            return output_;
        }

        void resize(const PtySize& requested) override {
            size = requested;
            ++resizes;
        }

        Composer& composer;
        size_t* destroyed;
        ParkInput input_;
        ParkOutput parkedOutput_;
        Output* output_;
        PtySize size{};
        size_t resizes = 0;
    };

    struct StubPty final: public Pty {
        StubPty(Composer& composer_, size_t& destroyed_)
            : composer(composer_)
            , destroyed(destroyed_)
        {
        }

        PtyHandle* spawn(ObjPool& owner, const LaunchCommand&) override {
            StubHandle* const handle = owner.make<StubHandle>(
                composer,
                &destroyed,
                blockNextWrite ? &writeEntered : nullptr,
                blockNextWrite ? &writeResumed : nullptr
            );
            blockNextWrite = false;
            handles.pushBack(handle);
            return handle;
        }

        Composer& composer;
        Vector<StubHandle*> handles;
        size_t& destroyed;
        bool blockNextWrite = false;
        bool writeEntered = false;
        bool writeResumed = false;
    };

    void publish(IntrusiveList& listeners) {
        for (IntrusiveNode* node = listeners.mutFront(); node != listeners.mutEnd();) {
            Listener* const listener = static_cast<Listener*>(node);
            node = node->next;
            listener->onListen();
        }
    }

    struct Harness {
        explicit Harness(size_t* destroyed = nullptr)
            : composer(*pool->make<Composer>(pool.mutPtr()))
            , pty(composer, destroyed == nullptr ? ownedDestroyed : *destroyed)
        {
            composer.platform = plt::createHeadlessPlatform(*composer.pool);
            composer.window = composer.platform->createWindow(*composer.pool, {.width = 80, .height = 24});
            composer.setGlyphSize(1, 1);
            composer.resize(80, 24);
            composer.ptyOutput = createNullOutput(composer.pool);
            composer.pty = &pty;
            composer.launch = &command;
            sessions = SessionSet::create(composer);
        }

        void newTab() {
            publish(composer.newTabListeners);
        }

        void closeTab() {
            publish(composer.closeTabListeners);
        }

        void nextTab() {
            publish(composer.nextTabListeners);
        }

        void previousTab() {
            publish(composer.prevTabListeners);
        }

        size_t ownedDestroyed = 0;
        ObjPool::Ref pool = ObjPool::fromMemory();
        Composer& composer;
        LaunchCommand command;
        StubPty pty;
        SessionSet* sessions = nullptr;
    };
}

namespace {
    struct ModelProbe final: public Listener {
        explicit ModelProbe(SessionSet* sessions_);

        void onListen(void*) override;

        SessionSet* sessions;
        size_t count = 0;
        size_t active = 0;
        unsigned notified = 0;
    };
}

ModelProbe::ModelProbe(SessionSet* sessions_)
    : sessions(sessions_)
{
}

void ModelProbe::onListen(void*) {
    count = sessions->count();
    active = sessions->activeIndex();
    ++notified;
}

STD_TEST_SUITE(SessionSet) {
    STD_TEST(TabModelCommitsBeforeItNotifies) {
        Harness harness;
        ModelProbe probe{harness.sessions};
        harness.composer.sessionsChangedListeners.pushBack(&probe);

        harness.newTab();
        STD_INSIST(probe.notified != 0);
        STD_INSIST(probe.count == 2);
        STD_INSIST(probe.active == 1);
        STD_INSIST(harness.sessions->title(0).length() == 0);

        harness.sessions->activate(0);
        STD_INSIST(probe.active == 0);

        harness.closeTab();
        STD_INSIST(probe.count == 1);
        STD_INSIST(probe.active == 0);
    }

    STD_TEST(ClosingABackgroundTabKeepsTheViewPut) {
        Harness harness;
        ModelProbe probe{harness.sessions};
        harness.composer.sessionsChangedListeners.pushBack(&probe);

        harness.newTab();
        harness.newTab();
        STD_INSIST(harness.sessions->activeIndex() == 2);
        Vterm* const watched = harness.sessions->activeTerminal();

        STD_INSIST(harness.sessions->close(0));
        STD_INSIST(harness.sessions->activeTerminal() == watched);
        STD_INSIST(probe.count == 2);
        STD_INSIST(probe.active == 1);

        STD_INSIST(harness.sessions->close(0));
        STD_INSIST(harness.sessions->activeTerminal() == watched);
        STD_INSIST(probe.count == 1);
        STD_INSIST(probe.active == 0);
    }

    STD_TEST(CreateOpensAndActivatesTheFirstSession) {
        Harness harness;

        STD_INSIST(SessionSet::liveSessions == 1);
        STD_INSIST(harness.pty.handles.length() == 1);
        STD_INSIST(harness.sessions->activeTerminal() != nullptr);
        STD_INSIST(harness.pty.handles[0]->resizes == 1);
    }

    STD_TEST(NewTabUsesTheSameProductionSpawnPath) {
        Harness harness;
        Vterm* const first = harness.sessions->activeTerminal();

        harness.newTab();

        STD_INSIST(SessionSet::liveSessions == 2);
        STD_INSIST(harness.pty.handles.length() == 2);
        STD_INSIST(harness.sessions->activeTerminal() != first);
    }

    STD_TEST(NextAndPreviousWrapAround) {
        Harness harness;
        Vterm* const first = harness.sessions->activeTerminal();
        harness.newTab();
        Vterm* const second = harness.sessions->activeTerminal();
        harness.newTab();
        Vterm* const third = harness.sessions->activeTerminal();

        harness.nextTab();
        STD_INSIST(harness.sessions->activeTerminal() == first);
        harness.nextTab();
        STD_INSIST(harness.sessions->activeTerminal() == second);
        harness.previousTab();
        STD_INSIST(harness.sessions->activeTerminal() == first);
        harness.previousTab();
        STD_INSIST(harness.sessions->activeTerminal() == third);
    }

    STD_TEST(SwitchingOneSessionStaysPut) {
        Harness harness;
        Vterm* const only = harness.sessions->activeTerminal();

        harness.nextTab();
        harness.previousTab();

        STD_INSIST(harness.sessions->activeTerminal() == only);
    }

    STD_TEST(CloseTabDestroysItsWholeArena) {
        Harness harness;
        Vterm* const first = harness.sessions->activeTerminal();
        harness.newTab();

        harness.closeTab();

        STD_INSIST(SessionSet::liveSessions == 1);
        STD_INSIST(harness.pty.destroyed == 1);
        STD_INSIST(harness.sessions->activeTerminal() == first);
    }

    STD_TEST(ClosingTheLastSessionReportsNoLiveSessions) {
        Harness harness;

        harness.closeTab();

        STD_INSIST(SessionSet::liveSessions == 0);
    }

    STD_TEST(TeardownReleasesEverySessionArena) {
        size_t destroyed = 0;
        {
            Harness harness(&destroyed);
            harness.newTab();
        }

        STD_INSIST(destroyed == 2);
        STD_INSIST(SessionSet::liveSessions == 0);
    }

    STD_TEST(ResizeReachesEverySessionHandle) {
        Harness harness;
        harness.newTab();
        const size_t firstResizes = harness.pty.handles[0]->resizes;
        const size_t secondResizes = harness.pty.handles[1]->resizes;

        harness.composer.resize(100, 40);

        STD_INSIST(harness.pty.handles[0]->resizes == firstResizes + 1);
        STD_INSIST(harness.pty.handles[1]->resizes == secondResizes + 1);
    }

    STD_TEST(ClosingReleasesAParkedClientWriteFiber) {
        Harness harness;
        harness.pty.blockNextWrite = true;
        harness.newTab();

        harness.sessions->activeTerminal()->sendBytes(StringView(u8"x"), true);
        STD_INSIST(harness.pty.writeEntered);
        STD_INSIST(!harness.pty.writeResumed);

        harness.closeTab();

        STD_INSIST(harness.pty.destroyed == 1);
        STD_INSIST(!harness.pty.writeResumed);
    }

    STD_TEST(SessionCountDoesNotLengthenTheInputChain) {
        Harness harness;
        harness.newTab();
        harness.newTab();
        size_t handlers = 0;
        for (IntrusiveNode* node = harness.composer.inputHandlers.mutFront(); node != harness.composer.inputHandlers.mutEnd(); node = node->next) {
            ++handlers;
        }

        // InputBindings and the one SessionSet handler.
        STD_INSIST(handlers == 2);
    }
}
