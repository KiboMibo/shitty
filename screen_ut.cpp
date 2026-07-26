/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "screen.h"

#include "cell_extra_store.h"
#include "composer.h"

#include <std/mem/obj_pool.h>
#include <std/str/view.h>
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

STD_TEST_SUITE(Screen) {
    STD_TEST(InitializesGeometryCapacityAndDamage) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        CellExtraStore::create(composer, 32);
        TerminalColors colors;
        configureColors(colors);

        Screen* screen = Screen::create(composer, *pool, 4, 3, &colors, 5);

        STD_INSIST(screen->active());
        STD_INSIST(screen->columns() == 4);
        STD_INSIST(screen->rows() == 3);
        STD_INSIST(screen->cellCapacity() == 32);
        STD_INSIST(!screen->hasDamage());

        screen->expose();
        STD_INSIST(screen->hasDamage());
        screen->resetDamage();
        STD_INSIST(!screen->hasDamage());
    }

    STD_TEST(ExpandsScrollbackToPowerOfTwoRing) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        CellExtraStore::create(composer, 32);
        TerminalColors colors;
        configureColors(colors);

        Screen* screen = Screen::create(composer, *pool, 2, 3, &colors, 2);

        STD_INSIST(screen->cellCapacity() == 16);
        for (u16 index = 0; index < 6; ++index) {
            screen->scrollUp(0, 3, 1);
        }
        STD_INSIST(screen->getHistoryRows() == 5);
    }

    STD_TEST(KeepsZeroScrollbackDisabled) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        CellExtraStore::create(composer, 8);
        TerminalColors colors;
        configureColors(colors);

        Screen* screen = Screen::create(composer, *pool, 2, 3, &colors);
        screen->scrollUp(0, 3, 1);

        STD_INSIST(screen->cellCapacity() == 6);
        STD_INSIST(screen->getHistoryRows() == 0);
    }

    STD_TEST(WritesAsciiAndMaterializesOnlyDamagedCells) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        CellExtraStore::create(composer, 8);
        TerminalColors colors;
        configureColors(colors);
        Screen* screen = Screen::create(composer, *pool, 4, 2, &colors);
        TerminalCell attrs = attributes();
        attrs.bold = true;
        const u8 text[] = {'a', 'b'};
        RenderCellUpdate rendered[8];

        screen->expose();
        size_t count = screen->copyDamagedCells(rendered);
        STD_INSIST(count == 8);
        screen->resetDamage();
        screen->writeAsciiRun(1, 1, text, 2, attrs, 0, 3, TerminalCell{});
        count = screen->copyDamagedCells(rendered);

        STD_INSIST(count == 2);
        STD_INSIST(rendered[0].index == 5);
        STD_INSIST(rendered[1].index == 6);
        STD_INSIST(rendered[0].cell.uc_pt == 'a');
        STD_INSIST(rendered[1].cell.uc_pt == 'b');
        STD_INSIST(rendered[0].cell.bold);
        STD_INSIST(rendered[0].cell.semantic == 3);
        STD_INSIST((rendered[0].cell.fg == Color{1, 2, 3}));
        STD_INSIST((rendered[0].cell.bg == Color{4, 5, 6}));
    }

    STD_TEST(StoresLineAttributesInRowMetadata) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        CellExtraStore::create(composer, 4);
        TerminalColors colors;
        configureColors(colors);
        Screen* screen = Screen::create(composer, *pool, 4, 1, &colors);
        RenderCellUpdate rendered[4];

        screen->setLineAttribute(0, 2);
        STD_INSIST(screen->lineAttribute(0) == 2);
        const size_t count = screen->copyDamagedCells(rendered);
        STD_INSIST(count == 4);
        for (size_t index = 0; index < count; ++index) {
            STD_INSIST(rendered[index].cell.line_attr == 2);
        }
        screen->setLineAttribute(0, 0);
        STD_INSIST(screen->lineAttribute(0) == 0);
    }

    STD_TEST(TracksProtectedCellsInRowMetadata) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        CellExtraStore::create(composer, 4);
        TerminalColors colors;
        configureColors(colors);
        Screen* screen = Screen::create(composer, *pool, 4, 1, &colors);
        TerminalCell attrs = attributes();
        attrs.protected_char = TerminalCell::isoProtection;
        const u8 text[] = {'x'};

        screen->writeAsciiRun(0, 1, text, 1, attrs, 0, 0, TerminalCell{});
        STD_INSIST(screen->hasProtection(0, TerminalCell::isoProtection));
        screen->selectiveEraseCells(0, 0, 4, TerminalCell{}, TerminalCell::isoProtection);
        STD_INSIST(screen->testCell(0, 1).uc_pt == 'x');
        screen->eraseCells(0, 0, 4, TerminalCell{});
        STD_INSIST(!screen->hasProtection(0, TerminalCell::isoProtection));
    }

    STD_TEST(ScrollsPartialRectanglesAsOneOperation) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        CellExtraStore::create(composer, 15);
        TerminalColors colors;
        configureColors(colors);
        Screen* screen = Screen::create(composer, *pool, 5, 3, &colors);
        const TerminalCell attrs = attributes();
        const u8 first[] = {'A', 'B', 'C', 'D', 'E'};
        const u8 second[] = {'F', 'G', 'H', 'I', 'J'};
        const u8 third[] = {'K', 'L', 'M', 'N', 'O'};
        screen->writeAsciiRun(0, 0, first, 5, attrs, 0, 0, TerminalCell{});
        screen->writeAsciiRun(1, 0, second, 5, attrs, 0, 0, TerminalCell{});
        screen->writeAsciiRun(2, 0, third, 5, attrs, 0, 0, TerminalCell{});

        screen->scrollRectangleUp(0, 1, 3, 4, 1, TerminalCell{});

        STD_INSIST(screen->testCell(0, 0).uc_pt == 'A');
        STD_INSIST(screen->testCell(0, 1).uc_pt == 'G');
        STD_INSIST(screen->testCell(0, 3).uc_pt == 'I');
        STD_INSIST(screen->testCell(0, 4).uc_pt == 'E');
        STD_INSIST(screen->testCell(1, 1).uc_pt == 'L');
        STD_INSIST(screen->testCell(1, 3).uc_pt == 'N');
        STD_INSIST(screen->testCell(2, 0).uc_pt == 'K');
        STD_INSIST(screen->testCell(2, 1).uc_pt == 0);
        STD_INSIST(screen->testCell(2, 3).uc_pt == 0);
        STD_INSIST(screen->testCell(2, 4).uc_pt == 'O');
    }

    STD_TEST(RotatesMultipleRowsInOnePass) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        CellExtraStore::create(composer, 5);
        TerminalColors colors;
        configureColors(colors);
        Screen* screen = Screen::create(composer, *pool, 1, 5, &colors);
        const TerminalCell attrs = attributes();
        for (u16 row = 0; row < 5; ++row) {
            const u8 value = (u8)('A' + row);
            screen->writeAsciiRun(row, 0, &value, 1, attrs, 0, 0, TerminalCell{});
        }

        screen->rotateRowsUp(0, 5, 2);
        for (u16 row = 0; row < 5; ++row) {
            STD_INSIST(screen->testCell(row, 0).uc_pt == (u32)("CDEAB"[row]));
        }

        screen->rotateRowsDown(0, 5, 2);
        for (u16 row = 0; row < 5; ++row) {
            STD_INSIST(screen->testCell(row, 0).uc_pt == (u32)("ABCDE"[row]));
        }
    }

    STD_TEST(InsertsAsciiRunsWithOneRowShift) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        CellExtraStore::create(composer, 5);
        TerminalColors colors;
        configureColors(colors);
        Screen* screen = Screen::create(composer, *pool, 5, 1, &colors);
        const TerminalCell attrs = attributes();
        const u8 initial[] = {'a', 'b', 'c', 'd', 'e'};
        const u8 inserted[] = {'X', 'Y'};
        screen->writeAsciiRun(0, 0, initial, 5, attrs, 0, 0, TerminalCell{});

        screen->writeAsciiRunInsert(0, 1, 5, inserted, 2, attrs, 0, 0, TerminalCell{});

        const u8 expected[] = {'a', 'X', 'Y', 'b', 'c'};
        for (u16 column = 0; column < 5; ++column) {
            STD_INSIST(screen->testCell(0, column).uc_pt == expected[column]);
        }
    }

    STD_TEST(OverwritingWideContinuationClearsItsLead) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        CellExtraStore::create(composer, 4);
        TerminalColors colors;
        configureColors(colors);
        Screen* screen = Screen::create(composer, *pool, 4, 1, &colors);
        const TerminalCell attrs = attributes();
        const u32 wide[] = {0x4e00};
        const u8 replacement[] = {'x'};

        screen->writeGrapheme(0, 1, wide, 1, true, attrs, 0, 0, TerminalCell{});
        STD_INSIST(screen->testCell(0, 1).dwidth);
        STD_INSIST(screen->testCell(0, 2).dwidth_cont);

        screen->writeAsciiRun(0, 2, replacement, 1, attrs, 0, 0, TerminalCell{});

        STD_INSIST(!screen->testCell(0, 1).dwidth);
        STD_INSIST(screen->testCell(0, 1).uc_pt == 0);
        STD_INSIST(!screen->testCell(0, 2).dwidth_cont);
        STD_INSIST(screen->testCell(0, 2).uc_pt == 'x');
    }

    STD_TEST(InsertAndDeleteCellsPreserveWideGlyph) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        CellExtraStore::create(composer, 16);
        TerminalColors colors;
        configureColors(colors);
        Screen* screen = Screen::create(composer, *pool, 8, 1, &colors);
        const TerminalCell attrs = attributes();
        const u32 wide[] = {0x4e00};
        const u8 text[] = {'a', 'b'};
        screen->writeAsciiRun(0, 0, text, 2, attrs, 0, 0, TerminalCell{});
        screen->writeGrapheme(0, 2, wide, 1, true, attrs, 0, 0, TerminalCell{});

        screen->insertCells(0, 1, 8, 1, TerminalCell{});

        STD_INSIST(screen->testCell(0, 0).uc_pt == 'a');
        STD_INSIST(screen->testCell(0, 1).uc_pt == 0);
        STD_INSIST(screen->testCell(0, 2).uc_pt == 'b');
        STD_INSIST(screen->testCell(0, 3).dwidth);
        STD_INSIST(screen->testCell(0, 4).dwidth_cont);

        screen->deleteCells(0, 1, 8, 1, TerminalCell{});

        STD_INSIST(screen->testCell(0, 1).uc_pt == 'b');
        STD_INSIST(screen->testCell(0, 2).dwidth);
        STD_INSIST(screen->testCell(0, 3).dwidth_cont);
    }

    STD_TEST(InsertInsideWideGlyphRemovesBothHalves) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        CellExtraStore::create(composer, 8);
        TerminalColors colors;
        configureColors(colors);
        Screen* screen = Screen::create(composer, *pool, 4, 1, &colors);
        const TerminalCell attrs = attributes();
        const u32 wide[] = {0x4e00};
        screen->writeGrapheme(0, 1, wide, 1, true, attrs, 0, 0, TerminalCell{});

        screen->insertCells(0, 2, 4, 1, TerminalCell{});

        for (u16 column = 0; column < 4; ++column) {
            const TerminalCell cell = screen->testCell(0, column);
            STD_INSIST(!cell.dwidth);
            STD_INSIST(!cell.dwidth_cont);
        }
    }

    STD_TEST(ScrollbackRetainsRowsAndChangesView) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        CellExtraStore::create(composer, 8);
        TerminalColors colors;
        configureColors(colors);
        Screen* screen = Screen::create(composer, *pool, 2, 3, &colors, 1);
        const TerminalCell attrs = attributes();
        const u8 first[] = {'A'};
        const u8 second[] = {'B'};
        const u8 third[] = {'C'};
        screen->writeAsciiRun(0, 0, first, 1, attrs, 0, 0, TerminalCell{});
        screen->writeAsciiRun(1, 0, second, 1, attrs, 0, 0, TerminalCell{});
        screen->writeAsciiRun(2, 0, third, 1, attrs, 0, 0, TerminalCell{});

        screen->scrollUp(0, 3, 1);

        STD_INSIST(screen->getHistoryRows() == 1);
        STD_INSIST(screen->testCell(0, 0).uc_pt == 'B');
        STD_INSIST(screen->testCell(1, 0).uc_pt == 'C');

        screen->pageUp(1);
        STD_INSIST(screen->getViewOffset() == 1);
        STD_INSIST(screen->testCell(0, 0).uc_pt == 'A');
        STD_INSIST(screen->testCell(1, 0).uc_pt == 'B');
        STD_INSIST(screen->testCell(2, 0).uc_pt == 'C');

        STD_INSIST(screen->pageToBottom());
        STD_INSIST(screen->getViewOffset() == 0);
        STD_INSIST(!screen->pageToBottom());
    }

    STD_TEST(FullHistoryRingKeepsNewestRowsAndRestoresThem) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        CellExtraStore::create(composer, 8);
        TerminalColors colors;
        configureColors(colors);
        Screen* screen = Screen::create(composer, *pool, 2, 2, &colors, 2);
        const TerminalCell attrs = attributes();
        const u8 first[] = {'A'};
        const u8 second[] = {'B'};
        const u8 third[] = {'C'};
        const u8 fourth[] = {'D'};
        const u8 fifth[] = {'E'};
        screen->writeAsciiRun(0, 0, first, 1, attrs, 0, 0, TerminalCell{});
        screen->writeAsciiRun(1, 0, second, 1, attrs, 0, 0, TerminalCell{});

        screen->scrollUp(0, 2, 1);
        screen->eraseCells(1, 0, 2, TerminalCell{});
        screen->writeAsciiRun(1, 0, third, 1, attrs, 0, 0, TerminalCell{});
        screen->scrollUp(0, 2, 1);
        screen->eraseCells(1, 0, 2, TerminalCell{});
        screen->writeAsciiRun(1, 0, fourth, 1, attrs, 0, 0, TerminalCell{});
        screen->scrollUp(0, 2, 1);
        screen->eraseCells(1, 0, 2, TerminalCell{});
        screen->writeAsciiRun(1, 0, fifth, 1, attrs, 0, 0, TerminalCell{});

        STD_INSIST(screen->getHistoryRows() == 2);
        screen->pageUp(2);
        STD_INSIST(screen->testCell(0, 0).uc_pt == 'B');
        STD_INSIST(screen->testCell(1, 0).uc_pt == 'C');
        screen->pageToBottom();
        screen->restoreHistory(1);
        STD_INSIST(screen->getHistoryRows() == 1);
        STD_INSIST(screen->testCell(0, 0).uc_pt == 'C');
        STD_INSIST(screen->testCell(1, 0).uc_pt == 'D');
    }

    STD_TEST(TopAnchoredPartialScrollPreservesRowsBelowRegion) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        CellExtraStore::create(composer, 8);
        TerminalColors colors;
        configureColors(colors);
        Screen* screen = Screen::create(composer, *pool, 2, 4, &colors, 2);
        const TerminalCell attrs = attributes();
        const u8 first[] = {'A'};
        const u8 second[] = {'B'};
        const u8 third[] = {'C'};
        const u8 fourth[] = {'D'};
        screen->writeAsciiRun(0, 0, first, 1, attrs, 0, 0, TerminalCell{});
        screen->writeAsciiRun(1, 0, second, 1, attrs, 0, 0, TerminalCell{});
        screen->writeAsciiRun(2, 0, third, 1, attrs, 0, 0, TerminalCell{});
        screen->writeAsciiRun(3, 0, fourth, 1, attrs, 0, 0, TerminalCell{});

        screen->scrollUp(0, 3, 1);
        screen->eraseCells(2, 0, 2, TerminalCell{});

        STD_INSIST(screen->getHistoryRows() == 1);
        STD_INSIST(screen->testCell(0, 0).uc_pt == 'B');
        STD_INSIST(screen->testCell(1, 0).uc_pt == 'C');
        STD_INSIST(screen->testCell(2, 0).uc_pt == 0);
        STD_INSIST(screen->testCell(3, 0).uc_pt == 'D');
        screen->pageUp(1);
        STD_INSIST(screen->testCell(0, 0).uc_pt == 'A');
        STD_INSIST(screen->testCell(1, 0).uc_pt == 'B');
        STD_INSIST(screen->testCell(2, 0).uc_pt == 'C');
    }

    STD_TEST(ReturnsExplicitAndDetectedHyperlinks) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        CellExtraStore::create(composer, 64);
        TerminalColors colors;
        configureColors(colors);
        Screen* screen = Screen::create(composer, *pool, 32, 2, &colors);
        const TerminalCell attrs = attributes();
        const u32 link = composer.cellExtras->getOrCreateHyperlink(StringView(u8"id"), StringView(u8"https://explicit.test"), 17);
        const u8 explicitText[] = {'x'};
        const u8 detected[] = {'s', 'e', 'e', ' ', 'h', 't', 't', 'p', 's', ':', '/', '/', 'e', 'x', 'a', 'm', 'p', 'l', 'e', '.', 't', 'e', 's', 't', ',', ' ', 'n', 'o', 'w'};
        screen->writeAsciiRun(0, 0, explicitText, 1, attrs, link, 0, TerminalCell{});
        screen->writeAsciiRun(1, 0, detected, sizeof(detected), attrs, 0, 0, TerminalCell{});

        const ScreenHyperlink explicitLink = screen->hyperlinkAt(0, 0);
        const ScreenHyperlink detectedLink = screen->hyperlinkAt(1, 8);

        STD_INSIST(explicitLink.displayId == 17);
        STD_INSIST(explicitLink.payload == StringView(u8"https://explicit.test"));
        STD_INSIST(detectedLink.displayId == 0);
        STD_INSIST(detectedLink.payload == StringView(u8"https://example.test"));
        STD_INSIST(detectedLink.scheme == StringView(u8"https"));
        STD_INSIST(detectedLink.begin == 36);
        STD_INSIST(detectedLink.end == 56);
    }

    STD_TEST(CollectsSentinelEncodedExtraCells) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        CellExtraStore::create(composer, 16);
        TerminalColors colors;
        configureColors(colors);
        Screen* screen = Screen::create(composer, *pool, 4, 1, &colors);
        TerminalCell ordinary = attributes();
        ordinary.setInlineUnderlineColor(CellColor::direct({TerminalCell::extraRefSentinel, 1, 2}));
        const u8 filler[] = {'a', 'b', 'c', 'd'};
        screen->writeAsciiRun(0, 0, filler, 4, ordinary, 0, 0, TerminalCell{});
        const u32 link = composer.cellExtras->getOrCreateHyperlink(StringView(u8"id"), StringView(u8"https://sentinel.test"), 1);
        const u8 first[] = {'x'};
        const u8 last[] = {'y'};
        screen->writeAsciiRun(0, 0, first, 1, attributes(), link, 0, TerminalCell{});
        screen->writeAsciiRun(0, 3, last, 1, attributes(), link, 0, TerminalCell{});
        Vector<TerminalCell*> cells;

        screen->collectExtraCells(cells);

        STD_INSIST(cells.length() == 2);
        STD_INSIST(cells[0]->uc_pt == 'x');
        STD_INSIST(cells[1]->uc_pt == 'y');
        STD_INSIST(cells[0]->extraRef() == link);
        STD_INSIST(cells[1]->extraRef() == link);
    }

    STD_TEST(MoveIntoTransfersContentToReplacement) {
        auto composerPool = ObjPool::fromMemory();
        auto sourcePool = ObjPool::fromMemory();
        auto destinationPool = ObjPool::fromMemory();
        Composer composer(composerPool.mutPtr());
        CellExtraStore::create(composer, 8);
        TerminalColors colors;
        configureColors(colors);
        Screen* screen = Screen::create(composer, *sourcePool, 4, 2, &colors);
        const TerminalCell attrs = attributes();
        const u8 text[] = {'a', 'b', 'c'};
        screen->writeAsciiRun(0, 0, text, 3, attrs, 0, 0, TerminalCell{});

        ResizeState* state = screen->moveInto();
        Screen::Cursor cursor;
        Screen* replacement = Screen::create(composer, *destinationPool, *state, 4, 2, &colors, false, &cursor);

        STD_INSIST(!screen->active());
        STD_INSIST(replacement->active());
        STD_INSIST(replacement->testCell(0, 0).uc_pt == 'a');
        STD_INSIST(replacement->testCell(0, 1).uc_pt == 'b');
        STD_INSIST(replacement->testCell(0, 2).uc_pt == 'c');
    }

    STD_TEST(TracksCursorAndPresentationState) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        CellExtraStore::create(composer, 4);
        TerminalColors colors;
        configureColors(colors);
        Screen* screen = Screen::create(composer, *pool, 2, 2, &colors);

        screen->setCursorPos(1, 1);
        screen->setCursorStyle(TerminalCursor::Style::bar);
        screen->setCursorColor({7, 8, 9});
        screen->setBlinkState(false, true);
        screen->setScreenReverseVideo(true);
        screen->setSelectionColor(true, {10, 11, 12}, true);
        screen->setSelectionColor(false, {13, 14, 15}, true);

        const TerminalCursor cursor = screen->getCursor();
        STD_INSIST(cursor.posX == 1);
        STD_INSIST(cursor.posY == 1);
        STD_INSIST(cursor.style == TerminalCursor::Style::bar);
        STD_INSIST((cursor.color == Color{7, 8, 9}));
        STD_INSIST(!screen->getBlinkVisible());
        STD_INSIST(screen->getCursorBlink());
        STD_INSIST(screen->getScreenReverseVideo());
        STD_INSIST(screen->getSelectionColorMask() == 3);
        STD_INSIST((screen->getSelectionForeground() == Color{10, 11, 12}));
        STD_INSIST((screen->getSelectionBackground() == Color{13, 14, 15}));
    }
}
