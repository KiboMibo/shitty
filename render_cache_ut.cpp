/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "render_cache.h"

#include "composer.h"

#include <std/mem/obj_pool.h>
#include <std/tst/ut.h>

using namespace stl;

namespace {
    struct TestRenderer final: public RenderCacheCallback {
        void render(const TerminalCell* input, RenderCell* output, u16 count, u8 lineAttribute) const override {
            ++calls;
            for (u16 index = 0; index < count; ++index) {
                output[index].uc_pt = input[index].uc_pt + offset;
                output[index].attributes = (u32)(lineAttribute) << 24;
                output[index].fg = {};
                output[index].bg = {};
                output[index].underline_color = {};
                output[index].hyperlink = 0;
                output[index].grapheme = 0;
                output[index].semantic = 0;
            }
        }

        mutable u32 calls = 0;
        u32 offset = 0;
    };

    void configureInput(TerminalCell* input, u16 count) {
        for (u16 index = 0; index < count; ++index) {
            input[index] = {};
            input[index].uc_pt = 'a' + index;
        }
    }
}

STD_TEST_SUITE(RenderCache) {
    STD_TEST(ReusesRenderedSpan) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        TerminalCell input[8];
        RenderCell firstScratch[8];
        RenderCell secondScratch[8];
        TestRenderer renderer;
        configureInput(input, 8);

        composer.renderCache->beginFrame(8, 1);
        const RenderCell* first = composer.renderCache->render(input, 8, 0, firstScratch, renderer);
        STD_INSIST(first != firstScratch);
        STD_INSIST(first[0].uc_pt == 'a');
        STD_INSIST(renderer.calls == 1);

        composer.renderCache->beginFrame(8, 1);
        const RenderCell* second = composer.renderCache->render(input, 8, 0, secondScratch, renderer);
        STD_INSIST(second == first);
        STD_INSIST(renderer.calls == 1);
    }

    STD_TEST(RendersShortSpansInScratch) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        TerminalCell input[7];
        RenderCell scratch[7];
        TestRenderer renderer;
        configureInput(input, 7);

        composer.renderCache->beginFrame(7, 1);
        const RenderCell* output = composer.renderCache->render(input, 7, 0, scratch, renderer);

        STD_INSIST(output == scratch);
        STD_INSIST(output[6].uc_pt == 'g');
        STD_INSIST(renderer.calls == 1);
    }

    STD_TEST(IncludesLineAttributeInKey) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        TerminalCell input[8];
        RenderCell scratch[16];
        TestRenderer renderer;
        configureInput(input, 8);

        composer.renderCache->beginFrame(8, 1);
        const RenderCell* first = composer.renderCache->render(input, 8, 1, scratch, renderer);
        const RenderCell* second = composer.renderCache->render(input, 8, 2, scratch + 8, renderer);

        STD_INSIST(first != second);
        STD_INSIST(first[0].line_attr == 1);
        STD_INSIST(second[0].line_attr == 2);
        STD_INSIST(renderer.calls == 2);
    }

    STD_TEST(InvalidatesChangedContext) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        TerminalCell input[8];
        RenderCell scratch[8];
        TestRenderer renderer;
        configureInput(input, 8);

        composer.renderCache->beginFrame(8, 1);
        const RenderCell* first = composer.renderCache->render(input, 8, 0, scratch, renderer);
        STD_INSIST(first[0].uc_pt == 'a');
        STD_INSIST(renderer.calls == 1);

        renderer.offset = 1;
        composer.renderCache->beginFrame(8, 2);
        const RenderCell* second = composer.renderCache->render(input, 8, 0, scratch, renderer);
        STD_INSIST(second[0].uc_pt == 'b');
        STD_INSIST(renderer.calls == 2);
    }
}
