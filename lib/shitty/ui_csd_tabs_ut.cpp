/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "ui_csd_tabs.h"

#include "composer.h"
#include "options.h"

#include <plt/platform.h>
#include <plt/platform_headless.h>
#include <plt/window.h>

#include <std/mem/obj_pool.h>
#include <std/tst/ut.h>

using namespace stl;

// createCsdTabsUi() is macOS-only chrome and only enters the build on
// darwin (build.py); off it there is nothing to link against.
#if defined(__APPLE__)

STD_TEST_SUITE(CsdTabsUi) {
    // The bridge cast to NSWindow lives in exactly one place in
    // ui_csd_tabs.mm, and it has to check the render context's backend
    // tag rather than its .window pointer: every backend hands back a
    // non-null .window, the headless one pointing at its own render
    // target. Without the tag check the constructor's own
    // applyTitlebarColor() sent an Objective-C message to that target
    // and took the whole test binary down with SIGSEGV (R2-qa round 2,
    // B5). transparentTitlebar is what carries the constructor past its
    // early exits and into the first message send, so it has to be on
    // for this to test anything.
    STD_TEST(HeadlessWindowIsNeverBridgedToAnNSWindow) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        plt::Platform* const platform = plt::createHeadlessPlatform(*pool);
        composer.window = platform->createWindow(*pool, {});
        Options* const options = pool->make<Options>();
        options->transparentTitlebar = true;
        composer.opts = options;
        STD_INSIST(composer.window->renderContext().backend != plt::RenderBackend::Cocoa);
        STD_INSIST(composer.window->renderContext().window != nullptr);

        createCsdTabsUi(*pool, composer);
    }
}

#endif
