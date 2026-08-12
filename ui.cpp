/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "ui.h"

#include "composer.h"

#include <std/alg/minmax.h>
#include <std/mem/obj_pool.h>

#include <math.h>
#include <stdint.h>

using namespace stl;

namespace {
    struct RawUi final: public Ui {
        explicit RawUi(Composer& composer_);

        plt::RenderContext terminalContext() override;
        void beginFrame(const plt::WindowInfo& info) override;
        void endFrame() override;

        Composer& composer;
    };
}

RawUi::RawUi(Composer& composer_)
    : composer(composer_)
{
}

plt::RenderContext RawUi::terminalContext() {
    return composer.window->renderContext();
}

void RawUi::beginFrame(const plt::WindowInfo& info) {
    if (isfinite(info.contentScale) && info.contentScale > 0.0f) {
        composer.setContentScale(info.contentScale);
    }
    composer.resize((u16)(min(info.width, (u32)(UINT16_MAX))), (u16)(min(info.height, (u32)(UINT16_MAX))));
}

void RawUi::endFrame() {
}

Ui* createRawUi(ObjPool& owner, Composer& composer) {
    return owner.make<RawUi>(composer);
}
