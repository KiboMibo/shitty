/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "ui_native_tabs.h"

#include "composer.h"
#include "listener.h"
#include "session.h"
#include "ui.h"

#include <plt/window.h>

#include <std/alg/minmax.h>
#include <std/lib/vector.h>
#include <std/mem/obj_pool.h>
#include <std/str/view.h>

#include <math.h>
#include <stdint.h>

using namespace stl;

namespace {
    struct NativeTabsUi;

    struct CallStripChanged final: public Listener {
        explicit CallStripChanged(NativeTabsUi* parent_);

        void onListen(void*) override;

        NativeTabsUi* parent;
    };

    struct NativeTabsUi final: public Ui, public plt::TabStripEvents {
        NativeTabsUi(ObjPool& owner, Composer& composer_);

        plt::RenderContext terminalContext() override;
        void beginFrame(const plt::WindowInfo& info) override;
        void endFrame() override;

        void tabSelected(size_t index) override;
        void tabOpened() override;

        void project();

        Composer& composer;
        Ui* raw = nullptr;
        CallStripChanged stripChanged{this};
    };
}

CallStripChanged::CallStripChanged(NativeTabsUi* parent_)
    : parent(parent_)
{
}

void CallStripChanged::onListen(void*) {
    parent->project();
}

NativeTabsUi::NativeTabsUi(ObjPool& owner, Composer& composer_)
    : composer(composer_)
    , raw(createRawUi(owner, composer_))
{
    composer.sessionsChangedListeners.pushBack(&stripChanged);
}

plt::RenderContext NativeTabsUi::terminalContext() {
    return raw->terminalContext();
}

void NativeTabsUi::beginFrame(const plt::WindowInfo& info) {
    raw->beginFrame(info);
}

void NativeTabsUi::endFrame() {
    raw->endFrame();
}

void NativeTabsUi::project() {
    SessionSet* const sessions = composer.sessions;
    if (sessions == nullptr || composer.window == nullptr) {
        return;
    }
    const size_t count = sessions->count();
    if (count < 2) {
        // A lone session keeps the clean native title, exactly like the
        // window before tabs existed.
        composer.window->setTabStrip(nullptr, 0, 0, this);
        return;
    }
    Vector<StringView> titles;
    titles.grow(count);
    for (size_t at = 0; at < count; ++at) {
        titles.pushBack(sessions->title(at));
    }
    composer.window->setTabStrip(titles.data(), count, sessions->activeIndex(), this);
}

void NativeTabsUi::tabSelected(size_t index) {
    if (composer.sessions != nullptr) {
        composer.sessions->activate(index);
        composer.window->requestFrame();
    }
}

void NativeTabsUi::tabOpened() {
    if (composer.sessions != nullptr) {
        composer.sessions->newSession();
    }
}

Ui* createNativeTabsUi(ObjPool& owner, Composer& composer) {
    return owner.make<NativeTabsUi>(owner, composer);
}
