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
        RenderCellSpan firstSpan[1];
        RenderCellSpan secondSpan[1];
        TestRenderer renderer;
        configureInput(input, 8);

        composer.renderCache->beginFrame(1);
        RenderCacheResult result = composer.renderCache->render(input, 8, 0, 0, firstScratch, firstSpan, renderer);
        STD_INSIST(result.scratch == firstScratch);
        STD_INSIST(result.spans == firstSpan + 1);
        STD_INSIST(firstSpan[0].cells != firstScratch);
        STD_INSIST(firstSpan[0].cells[0].uc_pt == 'a');
        STD_INSIST(renderer.calls == 1);

        composer.renderCache->beginFrame(1);
        result = composer.renderCache->render(input, 8, 0, 0, secondScratch, secondSpan, renderer);
        STD_INSIST(result.scratch == secondScratch);
        STD_INSIST(secondSpan[0].cells == firstSpan[0].cells);
        STD_INSIST(renderer.calls == 1);
    }

    STD_TEST(CachesEverySubmittedSpan) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        TerminalCell input[7];
        RenderCell firstScratch[7];
        RenderCell secondScratch[7];
        RenderCellSpan firstSpan[1];
        RenderCellSpan secondSpan[1];
        TestRenderer renderer;
        configureInput(input, 7);

        composer.renderCache->beginFrame(1);
        composer.renderCache->render(input, 7, 0, 0, firstScratch, firstSpan, renderer);
        composer.renderCache->beginFrame(1);
        composer.renderCache->render(input, 7, 0, 0, secondScratch, secondSpan, renderer);

        STD_INSIST(firstSpan[0].cells != firstScratch);
        STD_INSIST(firstSpan[0].cells[6].uc_pt == 'g');
        STD_INSIST(secondSpan[0].cells == firstSpan[0].cells);
        STD_INSIST(renderer.calls == 1);
    }

    STD_TEST(IncludesLineAttributeInKey) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        TerminalCell input[8];
        RenderCell scratch[16];
        RenderCellSpan spans[2];
        TestRenderer renderer;
        configureInput(input, 8);

        composer.renderCache->beginFrame(1);
        composer.renderCache->render(input, 8, 1, 0, scratch, spans, renderer);
        composer.renderCache->render(input, 8, 2, 0, scratch, spans + 1, renderer);

        STD_INSIST(spans[0].cells != spans[1].cells);
        STD_INSIST(spans[0].cells[0].line_attr == 1);
        STD_INSIST(spans[1].cells[0].line_attr == 2);
        STD_INSIST(renderer.calls == 2);
    }

    STD_TEST(InvalidatesChangedContext) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        TerminalCell input[8];
        RenderCell scratch[8];
        RenderCellSpan span[1];
        TestRenderer renderer;
        configureInput(input, 8);

        composer.renderCache->beginFrame(1);
        composer.renderCache->render(input, 8, 0, 0, scratch, span, renderer);
        STD_INSIST(span[0].cells[0].uc_pt == 'a');
        STD_INSIST(renderer.calls == 1);

        renderer.offset = 1;
        composer.renderCache->beginFrame(2);
        composer.renderCache->render(input, 8, 0, 0, scratch, span, renderer);
        STD_INSIST(span[0].cells[0].uc_pt == 'b');
        STD_INSIST(renderer.calls == 2);
    }

    STD_TEST(SplitsLongSpansWithoutCopying) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        TerminalCell input[65];
        RenderCell scratch[65];
        RenderCellSpan spans[3];
        TestRenderer renderer;
        configureInput(input, 65);

        composer.renderCache->beginFrame(1);
        const RenderCacheResult result = composer.renderCache->render(input, 65, 0, 100, scratch, spans, renderer);

        STD_INSIST(result.scratch == scratch);
        STD_INSIST(result.spans == spans + 3);
        STD_INSIST(spans[0].index == 100);
        STD_INSIST(spans[0].count == 32);
        STD_INSIST(spans[1].index == 132);
        STD_INSIST(spans[1].count == 32);
        STD_INSIST(spans[2].index == 164);
        STD_INSIST(spans[2].count == 1);
        STD_INSIST(spans[0].cells != scratch);
        STD_INSIST(spans[1].cells != scratch);
        STD_INSIST(spans[2].cells != scratch);
        STD_INSIST(renderer.calls == 3);
    }

    STD_TEST(ReportsWorstCaseSpanCapacity) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());

        STD_INSIST(composer.renderCache->spanCapacity(1, 1) == 1);
        STD_INSIST(composer.renderCache->spanCapacity(32, 2) == 2);
        STD_INSIST(composer.renderCache->spanCapacity(33, 2) == 4);
    }

    STD_TEST(DoesNotReuseRetiredBlocksWithinFrame) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        TerminalCell input[32];
        RenderCell scratch[32];
        RenderCellSpan span[1];
        TestRenderer renderer;
        configureInput(input, 32);

        composer.renderCache->beginFrame(1);
        composer.renderCache->render(input, 32, 0, 0, scratch, span, renderer);
        const RenderCell* const first = span[0].cells;
        const u32 firstCodepoint = first[0].uc_pt;
        for (u32 iteration = 1; iteration < 5000; ++iteration) {
            input[0].uc_pt = iteration;
            composer.renderCache->render(input, 32, 0, 0, scratch, span, renderer);
        }

        STD_INSIST(first[0].uc_pt == firstCodepoint);
    }
}
