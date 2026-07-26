/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "render_cache.h"

#include "cell_extra_store.h"
#include "composer.h"

#include <std/mem/obj_pool.h>
#include <std/tst/ut.h>

using namespace stl;

namespace {
    void configureColors(TerminalColors& colors) {
        colors.defaultForeground = {1, 2, 3};
        colors.defaultBackground = {4, 5, 6};
    }

    TerminalCell attributes() {
        TerminalCell cell{};
        cell.setForeground(CellColor::defaultForeground());
        cell.setBackground(CellColor::defaultBackground());
        return cell;
    }
}

STD_TEST_SUITE(RenderCache) {
    STD_TEST(ReusesMaterializedSpan) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        CellExtraStore::create(composer, 8);
        TerminalColors colors;
        configureColors(colors);
        TerminalCell input[8];
        RenderCell firstScratch[8];
        RenderCell secondScratch[8];
        const TerminalCell cell = attributes();
        for (TerminalCell& value : input) {
            value = cell;
        }

        composer.renderCache->beginFrame(8, colors);
        const RenderCell* first = composer.renderCache->render(input, 8, 0, firstScratch);
        STD_INSIST(first != firstScratch);
        STD_INSIST((first[0].fg == Color{1, 2, 3}));

        composer.renderCache->beginFrame(8, colors);
        const RenderCell* second = composer.renderCache->render(input, 8, 0, secondScratch);
        STD_INSIST(second == first);
    }

    STD_TEST(MaterializesShortSpansInScratch) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        CellExtraStore::create(composer, 7);
        TerminalColors colors;
        configureColors(colors);
        TerminalCell input[7];
        RenderCell scratch[7];
        const TerminalCell cell = attributes();
        for (TerminalCell& value : input) {
            value = cell;
        }

        composer.renderCache->beginFrame(7, colors);
        const RenderCell* output = composer.renderCache->render(input, 7, 0, scratch);

        STD_INSIST(output == scratch);
        STD_INSIST((output[6].bg == Color{4, 5, 6}));
    }

    STD_TEST(IncludesLineAttributeInKey) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        CellExtraStore::create(composer, 8);
        TerminalColors colors;
        configureColors(colors);
        TerminalCell input[8];
        RenderCell scratch[16];
        const TerminalCell cell = attributes();
        for (TerminalCell& value : input) {
            value = cell;
        }

        composer.renderCache->beginFrame(8, colors);
        const RenderCell* first = composer.renderCache->render(input, 8, 1, scratch);
        const RenderCell* second = composer.renderCache->render(input, 8, 2, scratch + 8);

        STD_INSIST(first != second);
        STD_INSIST(first[0].line_attr == 1);
        STD_INSIST(second[0].line_attr == 2);
    }

    STD_TEST(InvalidatesResolvedColors) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        CellExtraStore::create(composer, 8);
        TerminalColors colors;
        configureColors(colors);
        TerminalCell input[8];
        RenderCell scratch[8];
        const TerminalCell cell = attributes();
        for (TerminalCell& value : input) {
            value = cell;
        }

        composer.renderCache->beginFrame(8, colors);
        const RenderCell* first = composer.renderCache->render(input, 8, 0, scratch);
        STD_INSIST((first[0].fg == Color{1, 2, 3}));

        colors.defaultForeground = {7, 8, 9};
        colors.changed();
        composer.renderCache->beginFrame(8, colors);
        const RenderCell* second = composer.renderCache->render(input, 8, 0, scratch);
        STD_INSIST((second[0].fg == Color{7, 8, 9}));
    }
}
