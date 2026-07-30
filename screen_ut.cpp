/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "screen.h"

#include "cell_extra_store.h"
#include "composer.h"
#include "vterm.h"

#include <std/mem/obj_pool.h>
#include <std/str/view.h>
#include <std/tst/ut.h>

#include <cstring>

using namespace stl;

namespace {
    struct DamageCanvas {
        Vector<TerminalCell> cells;
        Vector<u8> lineAttributes;
        u16 columns = 0;
        u16 rows = 0;
        const TerminalColors* colors = nullptr;
        u32 viewOffset = 0;
        u32 historyRows = 0;
        TerminalCursor cursor;
        Rect selection;
        Rect snappedSelection;
        Color selectionForeground;
        Color selectionBackground;
        u8 selectionColorMask = 0;
        bool screenReverse = false;
        bool blinkVisible = true;
        bool cursorBlink = false;
    };

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

    bool equalRect(const Rect& left, const Rect& right) {
        return left.tl == right.tl && left.br == right.br && left.rectangular == right.rectangular;
    }

    void clearCanvas(DamageCanvas& canvas, u16 columns, u16 rows) {
        const size_t cellCount = (size_t)(columns)*rows;
        canvas.cells.grow(cellCount);
        canvas.lineAttributes.grow(rows);
        memset(canvas.cells.mutData(), 0, cellCount * sizeof(TerminalCell));
        memset(canvas.lineAttributes.mutData(), 0, rows);
        canvas.columns = columns;
        canvas.rows = rows;
        canvas.colors = nullptr;
        canvas.viewOffset = 0;
        canvas.historyRows = 0;
        canvas.cursor = {};
        canvas.selection = {};
        canvas.snappedSelection = {};
        canvas.selectionForeground = {};
        canvas.selectionBackground = {};
        canvas.selectionColorMask = 0;
        canvas.screenReverse = false;
        canvas.blinkVisible = true;
        canvas.cursorBlink = false;
    }

    TerminalUpdate takeUpdate(Screen& screen, const TerminalColors& colors, Vector<TerminalCellSpan>& spans) {
        const ScreenInfo info = screen.info();
        const size_t maximumSpans = (size_t)(info.rows) * ((info.columns + 1u) / 2u);
        spans.grow(maximumSpans);
        const ScreenFrame frame = screen.captureFrame(spans.mutData());
        const TerminalCellBatch batch = frame.damage;
        STD_INSIST(batch.spanCount <= maximumSpans);

        size_t cellCount = 0;
        u32 previousEnd = 0;
        Vector<u8> touched((size_t)(info.columns) * info.rows);
        touched.zero((size_t)(info.columns) * info.rows);
        for (size_t index = 0; index < batch.spanCount; ++index) {
            const TerminalCellSpan& span = spans[index];
            STD_INSIST(span.count != 0);
            STD_INSIST(span.cells != nullptr);
            STD_INSIST(span.index >= previousEnd);
            STD_INSIST(span.index + span.count <= (u32)(info.columns) * info.rows);
            STD_INSIST(span.index / info.columns == (span.index + span.count - 1) / info.columns);
            for (u32 offset = 0; offset < span.count; ++offset) {
                const u32 cellIndex = span.index + offset;
                STD_INSIST(touched[cellIndex] == 0);
                touched.mut(cellIndex) = 1;
                STD_INSIST(span.cells[offset] == screen.testCell(cellIndex / info.columns, cellIndex % info.columns));
            }
            previousEnd = span.index + span.count;
            cellCount += span.count;
        }
        STD_INSIST(cellCount == batch.cellCount);

        TerminalUpdate update{};
        update.spans = spans.data();
        update.spanCount = batch.spanCount;
        update.colors = &colors;
        update.viewOffset = frame.viewOffset;
        update.historyRows = frame.historyRows;
        update.selection = frame.selection;
        update.snappedSelection = frame.snappedSelection;
        return update;
    }

    bool hasDamage(Screen& screen) {
        const ScreenInfo info = screen.info();
        Vector<TerminalCellSpan> spans;
        spans.grow((size_t)(info.rows) * ((info.columns + 1u) / 2u));
        return screen.captureFrame(spans.mutData()).damage.spanCount != 0;
    }

    void applyUpdate(DamageCanvas& canvas, const TerminalUpdate& update) {
        STD_INSIST(update.colors != nullptr);
        for (size_t index = 0; index < update.spanCount; ++index) {
            const TerminalCellSpan& span = update.spans[index];
            STD_INSIST(span.index + span.count <= (u32)(canvas.columns) * canvas.rows);
            const u16 row = span.index / canvas.columns;
            memcpy(canvas.cells.mutData() + span.index, span.cells, span.count * sizeof(TerminalCell));
            canvas.lineAttributes.mut(row) = span.lineAttribute;
        }
        canvas.colors = update.colors;
        canvas.viewOffset = update.viewOffset;
        canvas.historyRows = update.historyRows;
        canvas.cursor = update.cursor;
        canvas.selection = update.selection;
        canvas.snappedSelection = update.snappedSelection;
        canvas.selectionForeground = update.selectionForeground;
        canvas.selectionBackground = update.selectionBackground;
        canvas.selectionColorMask = update.selectionColorMask;
        canvas.screenReverse = update.screenReverse;
        canvas.blinkVisible = update.blinkVisible;
        canvas.cursorBlink = update.cursorBlink;
    }

    bool equalCanvas(const DamageCanvas& left, const DamageCanvas& right) {
        if (left.columns != right.columns || left.rows != right.rows || left.colors != right.colors || left.viewOffset != right.viewOffset || left.historyRows != right.historyRows) {
            return false;
        }
        if (memcmp(left.cells.data(), right.cells.data(), (size_t)(left.columns) * left.rows * sizeof(TerminalCell)) != 0 || memcmp(left.lineAttributes.data(), right.lineAttributes.data(), left.rows) != 0) {
            return false;
        }
        if (left.cursor.posX != right.cursor.posX || left.cursor.posY != right.cursor.posY || left.cursor.style != right.cursor.style || left.cursor.color != right.cursor.color) {
            return false;
        }
        return equalRect(left.selection, right.selection) && equalRect(left.snappedSelection, right.snappedSelection) && left.selectionForeground == right.selectionForeground && left.selectionBackground == right.selectionBackground && left.selectionColorMask == right.selectionColorMask && left.screenReverse == right.screenReverse && left.blinkVisible == right.blinkVisible && left.cursorBlink == right.cursorBlink;
    }

    void renderFull(Screen& screen, const TerminalColors& colors, DamageCanvas& canvas) {
        const ScreenInfo info = screen.info();
        clearCanvas(canvas, info.columns, info.rows);
        screen.expose();
        Vector<TerminalCellSpan> spans;
        const TerminalUpdate update = takeUpdate(screen, colors, spans);
        applyUpdate(canvas, update);
        screen.resetDamage();
        STD_INSIST(!hasDamage(screen));
    }

    void fillDamagePattern(Screen& screen, Composer& composer) {
        const ScreenInfo info = screen.info();
        Vector<u8> text(info.columns);
        for (u16 row = 0; row < info.rows; ++row) {
            for (u16 column = 0; column < info.columns; ++column) {
                text.mut(column) = (u8)(33 + ((u32)(row) * 17 + column) % 90);
            }
            TerminalCell attrs = attributes();
            attrs.setForeground(CellColor::indexed((u8)(row + 1)));
            attrs.setBackground(CellColor::indexed((u8)(row + 17)));
            attrs.bold = row & 1;
            attrs.italic = (row & 2) != 0;
            screen.writeAsciiRun(row, 0, text.data(), info.columns, attrs, 0, row & 3, TerminalCell{});
            screen.setLineAttribute(row, row % 3);
        }

        TerminalCell protectedAttrs = attributes();
        protectedAttrs.protected_char = TerminalCell::isoProtection;
        screen.writeCodepoint(2, 3, 'P', false, protectedAttrs, 0, 1, TerminalCell{});
        screen.writeCodepoint(1, 2, 0x4e00, true, attributes(), 0, 2, TerminalCell{});
        screen.writeCodepoint(3, info.columns - 3, 0x4e01, true, attributes(), 0, 3, TerminalCell{});
        const u32 hyperlink = composer.cellExtras->getOrCreateHyperlink(StringView(u8"damage"), StringView(u8"https://damage.test"), 7);
        screen.writeCodepoint(4, 4, 'H', false, attributes(), hyperlink, 2, TerminalCell{});
        screen.setWrapped(0, info.columns - 1);
    }

    void prepareHistory(Screen& screen) {
        const ScreenInfo info = screen.info();
        screen.scrollRows(0, info.rows, -2, TerminalCell{});
        const u8 first[] = {'n', 'e', 'w', '1'};
        const u8 second[] = {'n', 'e', 'w', '2'};
        screen.writeAsciiRun(info.rows - 2, 0, first, sizeof(first), attributes(), 0, 0, TerminalCell{});
        screen.writeAsciiRun(info.rows - 1, 0, second, sizeof(second), attributes(), 0, 0, TerminalCell{});
    }

    template <typename Setup, typename Operation>
    void verifyDamageGeometry(u16 columns, u16 rows, Setup setup, Operation operation, bool expectsDamage) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        composer.setCellExtras(CellExtraStore::create(composer, (size_t)(columns)*rows * 2));
        TerminalColors colors;
        configureColors(colors);
        Screen* screen = Screen::createPrimary(composer, *pool, columns, rows, &colors, 8);
        fillDamagePattern(*screen, composer);
        setup(*screen);

        DamageCanvas incremental;
        renderFull(*screen, colors, incremental);
        operation(*screen);
        STD_INSIST(hasDamage(*screen) == expectsDamage);
        const ScreenInfo info = screen->info();
        if (incremental.columns != info.columns || incremental.rows != info.rows) {
            clearCanvas(incremental, info.columns, info.rows);
        }

        Vector<TerminalCellSpan> spans;
        const TerminalUpdate update = takeUpdate(*screen, colors, spans);
        STD_INSIST((update.spanCount != 0) == expectsDamage);
        applyUpdate(incremental, update);

        DamageCanvas expected;
        renderFull(*screen, colors, expected);
        STD_INSIST(equalCanvas(incremental, expected));
    }

    template <typename Setup, typename Operation>
    void verifyDamage(Setup setup, Operation operation, bool expectsDamage = true) {
        verifyDamageGeometry(8, 5, setup, operation, expectsDamage);
        verifyDamageGeometry(260, 5, setup, operation, expectsDamage);
    }

    template <typename Operation>
    void verifyDamage(Operation operation, bool expectsDamage = true) {
        verifyDamage([](Screen&) {}, operation, expectsDamage);
    }

    void verifyResizeDamageGeometry(u16 columns, bool primary) {
        auto composerPool = ObjPool::fromMemory();
        auto sourcePool = ObjPool::fromMemory();
        auto destinationPool = ObjPool::fromMemory();
        Composer composer(composerPool.mutPtr());
        composer.setCellExtras(CellExtraStore::create(composer, (size_t)(columns) * 10));
        TerminalColors colors;
        configureColors(colors);
        Screen* source = primary ? Screen::createPrimary(composer, *sourcePool, columns, 5, &colors, 8) : Screen::createAlternate(composer, *sourcePool, columns, 5, &colors);
        fillDamagePattern(*source, composer);

        DamageCanvas incremental;
        renderFull(*source, colors, incremental);
        Screen::Cursor cursor;
        cursor.position = Point(columns - 2, 3);
        const u16 destinationColumns = primary ? columns - 2 : columns + 2;
        Screen* const destination = source->resized(*destinationPool, destinationColumns, 6, cursor);
        STD_INSIST(hasDamage(*destination));
        const ScreenInfo destinationInfo = destination->info();
        clearCanvas(incremental, destinationInfo.columns, destinationInfo.rows);

        Vector<TerminalCellSpan> spans;
        const TerminalUpdate update = takeUpdate(*destination, colors, spans);
        applyUpdate(incremental, update);

        DamageCanvas expected;
        renderFull(*destination, colors, expected);
        STD_INSIST(equalCanvas(incremental, expected));
    }

    void verifyResizeDamage(bool primary) {
        verifyResizeDamageGeometry(8, primary);
        verifyResizeDamageGeometry(260, primary);
    }
}

STD_TEST_SUITE(Screen) {
    STD_TEST(InitializesGeometryCapacityAndDamage) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        composer.setCellExtras(CellExtraStore::create(composer, 32));
        TerminalColors colors;
        configureColors(colors);

        Screen* screen = Screen::createPrimary(composer, *pool, 4, 3, &colors, 5);
        const ScreenInfo info = screen->info();

        STD_INSIST(info.columns == 4);
        STD_INSIST(info.rows == 3);
        STD_INSIST(info.cellCapacity == 32);
        STD_INSIST(!hasDamage(*screen));

        screen->expose();
        STD_INSIST(hasDamage(*screen));
        screen->resetDamage();
        STD_INSIST(!hasDamage(*screen));
    }

    STD_TEST(RevisionTracksVisibleState) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        composer.setCellExtras(CellExtraStore::create(composer, 32));
        TerminalColors colors;
        configureColors(colors);

        Screen* screen = Screen::createPrimary(composer, *pool, 4, 3, &colors, 5);
        const u32 initial = screen->info().revision;
        screen->resetDamage();
        STD_INSIST(screen->info().revision == initial);

        screen->expose();
        const u32 exposed = screen->info().revision;
        STD_INSIST(exposed != initial);
        screen->resetDamage();
        STD_INSIST(screen->info().revision == exposed);

        const u8 text[] = {'x'};
        screen->writeAsciiRun(0, 0, text, 1, attributes(), 0, 0, TerminalCell{});
        const u32 written = screen->info().revision;
        STD_INSIST(written != exposed);

        screen->beginSelection(Point(0, 0));
        STD_INSIST(screen->info().revision != written);
    }

    STD_TEST(ExpandsScrollbackToPowerOfTwoRing) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        composer.setCellExtras(CellExtraStore::create(composer, 32));
        TerminalColors colors;
        configureColors(colors);

        Screen* screen = Screen::createPrimary(composer, *pool, 2, 3, &colors, 2);

        STD_INSIST(screen->info().cellCapacity == 16);
        for (u16 index = 0; index < 6; ++index) {
            screen->scrollRows(0, 3, -1, TerminalCell{});
        }
        STD_INSIST(screen->info().historyRows == 5);
    }

    STD_TEST(KeepsZeroScrollbackDisabled) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        composer.setCellExtras(CellExtraStore::create(composer, 8));
        TerminalColors colors;
        configureColors(colors);

        Screen* screen = Screen::createAlternate(composer, *pool, 2, 3, &colors);
        screen->scrollRows(0, 3, -1, TerminalCell{});

        STD_INSIST(screen->info().cellCapacity == 6);
        STD_INSIST(screen->info().historyRows == 0);
    }

    STD_TEST(AlternateResizeKeepsTopRows) {
        auto composerPool = ObjPool::fromMemory();
        auto sourcePool = ObjPool::fromMemory();
        auto destinationPool = ObjPool::fromMemory();
        Composer composer(composerPool.mutPtr());
        composer.setCellExtras(CellExtraStore::create(composer, 8));
        TerminalColors colors;
        configureColors(colors);
        Screen* screen = Screen::createAlternate(composer, *sourcePool, 1, 4, &colors);
        const u8 text[] = {'A', 'B', 'C', 'D'};
        for (u16 row = 0; row < 4; ++row) {
            screen->writeAsciiRun(row, 0, text + row, 1, attributes(), 0, 0, TerminalCell{});
        }
        Screen::Cursor cursor{Point(0, 3), false};

        Screen* resized = screen->resized(*destinationPool, 1, 3, cursor);

        STD_INSIST(resized->testCell(0, 0).uc_pt == 'A');
        STD_INSIST(resized->testCell(1, 0).uc_pt == 'B');
        STD_INSIST(resized->testCell(2, 0).uc_pt == 'C');
        STD_INSIST(cursor.position.y == 2);
        STD_INSIST(resized->info().historyRows == 0);
    }

    STD_TEST(PrimaryResizeKeepsCursorRowsAndCapturesHistory) {
        auto composerPool = ObjPool::fromMemory();
        auto sourcePool = ObjPool::fromMemory();
        auto destinationPool = ObjPool::fromMemory();
        Composer composer(composerPool.mutPtr());
        composer.setCellExtras(CellExtraStore::create(composer, 8));
        TerminalColors colors;
        configureColors(colors);
        Screen* screen = Screen::createPrimary(composer, *sourcePool, 1, 4, &colors, 4);
        const u8 text[] = {'A', 'B', 'C', 'D'};
        for (u16 row = 0; row < 4; ++row) {
            screen->writeAsciiRun(row, 0, text + row, 1, attributes(), 0, 0, TerminalCell{});
        }
        Screen::Cursor cursor{Point(0, 3), false};

        Screen* resized = screen->resized(*destinationPool, 1, 3, cursor);

        STD_INSIST(resized->testCell(0, 0).uc_pt == 'B');
        STD_INSIST(resized->testCell(1, 0).uc_pt == 'C');
        STD_INSIST(resized->testCell(2, 0).uc_pt == 'D');
        STD_INSIST(cursor.position.y == 2);
        STD_INSIST(resized->info().historyRows == 1);
    }

    STD_TEST(WritesAsciiAndExposesOnlyDamagedCells) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        composer.setCellExtras(CellExtraStore::create(composer, 8));
        TerminalColors colors;
        configureColors(colors);
        Screen* screen = Screen::createAlternate(composer, *pool, 4, 2, &colors);
        TerminalCell attrs = attributes();
        attrs.bold = true;
        const u8 text[] = {'a', 'b'};
        TerminalCellSpan spans[2];

        screen->expose();
        TerminalCellBatch batch = screen->captureFrame(spans).damage;
        STD_INSIST(batch.cellCount == 8);
        STD_INSIST(batch.spanCount == 2);
        STD_INSIST(spans[0].index == 0);
        STD_INSIST(spans[0].count == 4);
        STD_INSIST(spans[1].index == 4);
        STD_INSIST(spans[1].count == 4);
        screen->resetDamage();
        screen->writeAsciiRun(1, 1, text, 2, attrs, 0, 3, TerminalCell{});
        batch = screen->captureFrame(spans).damage;

        STD_INSIST(batch.cellCount == 2);
        STD_INSIST(batch.spanCount == 1);
        STD_INSIST(spans[0].index == 5);
        STD_INSIST(spans[0].count == 2);
        STD_INSIST(spans[0].cells[0].uc_pt == 'a');
        STD_INSIST(spans[0].cells[1].uc_pt == 'b');
        STD_INSIST(spans[0].cells[0].bold);
        STD_INSIST(spans[0].cells[0].semantic == 3);
    }

    STD_TEST(PreservesDisjointDamageWithinOneRow) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        composer.setCellExtras(CellExtraStore::create(composer, 8));
        TerminalColors colors;
        configureColors(colors);
        Screen* screen = Screen::createAlternate(composer, *pool, 8, 2, &colors);
        const TerminalCell attrs = attributes();
        const u8 left[] = {'L'};
        const u8 right[] = {'R'};
        TerminalCellSpan spans[2];

        screen->writeAsciiRun(1, 6, right, 1, attrs, 0, 0, TerminalCell{});
        screen->writeAsciiRun(1, 1, left, 1, attrs, 0, 0, TerminalCell{});
        screen->writeAsciiRun(1, 6, right, 1, attrs, 0, 0, TerminalCell{});

        TerminalCellBatch batch = screen->captureFrame(spans).damage;
        STD_INSIST(batch.cellCount == 2);
        STD_INSIST(batch.spanCount == 2);
        STD_INSIST(spans[0].index == 9);
        STD_INSIST(spans[0].count == 1);
        STD_INSIST(spans[0].cells[0].uc_pt == 'L');
        STD_INSIST(spans[1].index == 14);
        STD_INSIST(spans[1].count == 1);
        STD_INSIST(spans[1].cells[0].uc_pt == 'R');

        screen->resetDamage();
        screen->writeAsciiRun(1, 3, left, 1, attrs, 0, 0, TerminalCell{});
        batch = screen->captureFrame(spans).damage;
        STD_INSIST(batch.cellCount == 1);
        STD_INSIST(batch.spanCount == 1);
        STD_INSIST(spans[0].index == 11);
    }

    STD_TEST(WritesAsciiLinesAndRecyclesFullHistory) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        composer.setCellExtras(CellExtraStore::create(composer, 16));
        TerminalColors colors;
        configureColors(colors);
        Screen* screen = Screen::createPrimary(composer, *pool, 3, 2, &colors, 2);
        const TerminalCell attrs = attributes();
        const u8 text[] = {'A', 'B', '\r', '\n', 'C', '\r', '\n', 'D', 'E', '\r', '\n', 'F', '\r', '\n'};
        const u16 lengths[] = {2, 1, 2, 1};

        screen->writeAsciiLines(0, text, lengths, 4, attrs, 0, 0, TerminalCell{});

        STD_INSIST(screen->info().historyRows == 2);
        STD_INSIST(screen->testCell(0, 0).uc_pt == 'F');
        STD_INSIST(screen->testCell(0, 1).uc_pt == 0);
        STD_INSIST(screen->testCell(1, 0).uc_pt == 0);
        screen->scrollView(2);
        STD_INSIST(screen->testCell(0, 0).uc_pt == 'C');
        STD_INSIST(screen->testCell(1, 0).uc_pt == 'D');
        STD_INSIST(screen->testCell(1, 1).uc_pt == 'E');
    }

    STD_TEST(WritesAsciiLinesIntoClearedRowsWithEraseAttributes) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        composer.setCellExtras(CellExtraStore::create(composer, 16));
        TerminalColors colors;
        configureColors(colors);
        Screen* screen = Screen::createAlternate(composer, *pool, 4, 2, &colors);
        const TerminalCell attrs = attributes();
        TerminalCell eraseAttrs{};
        eraseAttrs.bold = true;
        const u8 text[] = {'A', '\r', '\n', 'B', '\r', '\n', 'C', '\r', '\n'};
        const u16 lengths[] = {1, 1, 1};

        screen->writeAsciiLines(0, text, lengths, 3, attrs, 0, 0, eraseAttrs);

        STD_INSIST(screen->testCell(0, 0).uc_pt == 'C');
        STD_INSIST(!screen->testCell(0, 0).bold);
        STD_INSIST(screen->testCell(0, 1).uc_pt == 0);
        STD_INSIST(screen->testCell(0, 1).bold);
        STD_INSIST(screen->testCell(1, 0).uc_pt == 0);
        STD_INSIST(screen->testCell(1, 0).bold);
    }

    STD_TEST(WritesAsciiLinesWithoutTouchingOtherRows) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        composer.setCellExtras(CellExtraStore::create(composer, 16));
        TerminalColors colors;
        configureColors(colors);
        Screen* screen = Screen::createAlternate(composer, *pool, 3, 4, &colors);
        const TerminalCell attrs = attributes();
        const u8 original[] = {'x', 'y', 'z'};
        const u8 text[] = {'A', '\r', '\n', 'B', '\r', '\n'};
        const u16 lengths[] = {1, 1};
        screen->writeAsciiRun(3, 0, original, 3, attrs, 0, 0, TerminalCell{});
        screen->resetDamage();

        screen->writeAsciiLines(0, text, lengths, 2, attrs, 0, 0, TerminalCell{});

        TerminalCellSpan spans[4];
        const TerminalCellBatch batch = screen->captureFrame(spans).damage;
        STD_INSIST(batch.spanCount == 2);
        STD_INSIST(batch.cellCount == 2);
        STD_INSIST(screen->testCell(2, 0).uc_pt == 0);
        STD_INSIST(screen->testCell(3, 0).uc_pt == 'x');
        STD_INSIST(screen->testCell(3, 2).uc_pt == 'z');
    }

    STD_TEST(StoresLineAttributesInRowMetadata) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        composer.setCellExtras(CellExtraStore::create(composer, 4));
        TerminalColors colors;
        configureColors(colors);
        Screen* screen = Screen::createAlternate(composer, *pool, 4, 1, &colors);
        TerminalCellSpan spans[1];

        screen->setLineAttribute(0, 2);
        STD_INSIST(screen->lineAttribute(0) == 2);
        const TerminalCellBatch batch = screen->captureFrame(spans).damage;
        STD_INSIST(batch.cellCount == 4);
        STD_INSIST(spans[0].lineAttribute == 2);
        screen->setLineAttribute(0, 0);
        STD_INSIST(screen->lineAttribute(0) == 0);
    }

    STD_TEST(TracksProtectedCellsInRowMetadata) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        composer.setCellExtras(CellExtraStore::create(composer, 4));
        TerminalColors colors;
        configureColors(colors);
        Screen* screen = Screen::createAlternate(composer, *pool, 4, 1, &colors);
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
        composer.setCellExtras(CellExtraStore::create(composer, 15));
        TerminalColors colors;
        configureColors(colors);
        Screen* screen = Screen::createAlternate(composer, *pool, 5, 3, &colors);
        const TerminalCell attrs = attributes();
        const u8 first[] = {'A', 'B', 'C', 'D', 'E'};
        const u8 second[] = {'F', 'G', 'H', 'I', 'J'};
        const u8 third[] = {'K', 'L', 'M', 'N', 'O'};
        screen->writeAsciiRun(0, 0, first, 5, attrs, 0, 0, TerminalCell{});
        screen->writeAsciiRun(1, 0, second, 5, attrs, 0, 0, TerminalCell{});
        screen->writeAsciiRun(2, 0, third, 5, attrs, 0, 0, TerminalCell{});

        screen->scrollRectangle(0, 1, 3, 4, -1, TerminalCell{});

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

    STD_TEST(PartialScrollUpClearsWideGlyphsAtBothBoundaries) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        composer.setCellExtras(CellExtraStore::create(composer, 14));
        TerminalColors colors;
        configureColors(colors);
        Screen* screen = Screen::createAlternate(composer, *pool, 7, 2, &colors);
        const TerminalCell attrs = attributes();
        constexpr u32 wide = 0x4e00;
        const u8 middle[] = {'x'};

        screen->writeCodepoint(0, 1, wide, true, attrs, 0, 0, TerminalCell{});
        screen->writeCodepoint(0, 4, wide, true, attrs, 0, 0, TerminalCell{});
        screen->writeCodepoint(1, 1, wide, true, attrs, 0, 0, TerminalCell{});
        screen->writeCodepoint(1, 4, wide, true, attrs, 0, 0, TerminalCell{});
        screen->writeAsciiRun(1, 3, middle, 1, attrs, 0, 0, TerminalCell{});

        screen->scrollRectangle(0, 2, 2, 5, -1, TerminalCell{});

        STD_INSIST(screen->testCell(0, 1) == TerminalCell{});
        STD_INSIST(screen->testCell(0, 2) == TerminalCell{});
        STD_INSIST(screen->testCell(0, 3).uc_pt == 'x');
        STD_INSIST(screen->testCell(0, 4) == TerminalCell{});
        STD_INSIST(screen->testCell(0, 5) == TerminalCell{});
    }

    STD_TEST(PartialScrollDownClearsWideGlyphsAtBothBoundaries) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        composer.setCellExtras(CellExtraStore::create(composer, 14));
        TerminalColors colors;
        configureColors(colors);
        Screen* screen = Screen::createAlternate(composer, *pool, 7, 2, &colors);
        const TerminalCell attrs = attributes();
        constexpr u32 wide = 0x4e00;
        const u8 middle[] = {'x'};

        screen->writeCodepoint(0, 1, wide, true, attrs, 0, 0, TerminalCell{});
        screen->writeCodepoint(0, 4, wide, true, attrs, 0, 0, TerminalCell{});
        screen->writeAsciiRun(0, 3, middle, 1, attrs, 0, 0, TerminalCell{});
        screen->writeCodepoint(1, 1, wide, true, attrs, 0, 0, TerminalCell{});
        screen->writeCodepoint(1, 4, wide, true, attrs, 0, 0, TerminalCell{});

        screen->scrollRectangle(0, 2, 2, 5, 1, TerminalCell{});

        STD_INSIST(screen->testCell(1, 1) == TerminalCell{});
        STD_INSIST(screen->testCell(1, 2) == TerminalCell{});
        STD_INSIST(screen->testCell(1, 3).uc_pt == 'x');
        STD_INSIST(screen->testCell(1, 4) == TerminalCell{});
        STD_INSIST(screen->testCell(1, 5) == TerminalCell{});
    }

    STD_TEST(RotatesMultipleRowsInOnePass) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        composer.setCellExtras(CellExtraStore::create(composer, 5));
        TerminalColors colors;
        configureColors(colors);
        Screen* screen = Screen::createAlternate(composer, *pool, 1, 5, &colors);
        const TerminalCell attrs = attributes();
        for (u16 row = 0; row < 5; ++row) {
            const u8 value = (u8)('A' + row);
            screen->writeAsciiRun(row, 0, &value, 1, attrs, 0, 0, TerminalCell{});
        }

        screen->rotateRows(0, 5, -2);
        for (u16 row = 0; row < 5; ++row) {
            STD_INSIST(screen->testCell(row, 0).uc_pt == (u32)("CDEAB"[row]));
        }

        screen->rotateRows(0, 5, 2);
        for (u16 row = 0; row < 5; ++row) {
            STD_INSIST(screen->testCell(row, 0).uc_pt == (u32)("ABCDE"[row]));
        }
    }

    STD_TEST(InsertsAsciiRunsWithOneRowShift) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        composer.setCellExtras(CellExtraStore::create(composer, 5));
        TerminalColors colors;
        configureColors(colors);
        Screen* screen = Screen::createAlternate(composer, *pool, 5, 1, &colors);
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
        composer.setCellExtras(CellExtraStore::create(composer, 4));
        TerminalColors colors;
        configureColors(colors);
        Screen* screen = Screen::createAlternate(composer, *pool, 4, 1, &colors);
        const TerminalCell attrs = attributes();
        constexpr u32 wide = 0x4e00;
        const u8 replacement[] = {'x'};

        screen->writeCodepoint(0, 1, wide, true, attrs, 0, 0, TerminalCell{});
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
        composer.setCellExtras(CellExtraStore::create(composer, 16));
        TerminalColors colors;
        configureColors(colors);
        Screen* screen = Screen::createAlternate(composer, *pool, 8, 1, &colors);
        const TerminalCell attrs = attributes();
        constexpr u32 wide = 0x4e00;
        const u8 text[] = {'a', 'b'};
        screen->writeAsciiRun(0, 0, text, 2, attrs, 0, 0, TerminalCell{});
        screen->writeCodepoint(0, 2, wide, true, attrs, 0, 0, TerminalCell{});

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
        composer.setCellExtras(CellExtraStore::create(composer, 8));
        TerminalColors colors;
        configureColors(colors);
        Screen* screen = Screen::createAlternate(composer, *pool, 4, 1, &colors);
        const TerminalCell attrs = attributes();
        constexpr u32 wide = 0x4e00;
        screen->writeCodepoint(0, 1, wide, true, attrs, 0, 0, TerminalCell{});

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
        composer.setCellExtras(CellExtraStore::create(composer, 8));
        TerminalColors colors;
        configureColors(colors);
        Screen* screen = Screen::createPrimary(composer, *pool, 2, 3, &colors, 1);
        const TerminalCell attrs = attributes();
        const u8 first[] = {'A'};
        const u8 second[] = {'B'};
        const u8 third[] = {'C'};
        screen->writeAsciiRun(0, 0, first, 1, attrs, 0, 0, TerminalCell{});
        screen->writeAsciiRun(1, 0, second, 1, attrs, 0, 0, TerminalCell{});
        screen->writeAsciiRun(2, 0, third, 1, attrs, 0, 0, TerminalCell{});

        screen->scrollRows(0, 3, -1, TerminalCell{});

        STD_INSIST(screen->info().historyRows == 1);
        STD_INSIST(screen->testCell(0, 0).uc_pt == 'B');
        STD_INSIST(screen->testCell(1, 0).uc_pt == 'C');

        screen->scrollView(1);
        STD_INSIST(screen->info().viewOffset == 1);
        STD_INSIST(screen->testCell(0, 0).uc_pt == 'A');
        STD_INSIST(screen->testCell(1, 0).uc_pt == 'B');
        STD_INSIST(screen->testCell(2, 0).uc_pt == 'C');

        STD_INSIST(screen->scrollView(-0x7fffffff));
        STD_INSIST(screen->info().viewOffset == 0);
        STD_INSIST(!screen->scrollView(-0x7fffffff));
    }

    STD_TEST(FullHistoryRingKeepsNewestRowsAndRestoresThem) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        composer.setCellExtras(CellExtraStore::create(composer, 8));
        TerminalColors colors;
        configureColors(colors);
        Screen* screen = Screen::createPrimary(composer, *pool, 2, 2, &colors, 2);
        const TerminalCell attrs = attributes();
        const u8 first[] = {'A'};
        const u8 second[] = {'B'};
        const u8 third[] = {'C'};
        const u8 fourth[] = {'D'};
        const u8 fifth[] = {'E'};
        screen->writeAsciiRun(0, 0, first, 1, attrs, 0, 0, TerminalCell{});
        screen->writeAsciiRun(1, 0, second, 1, attrs, 0, 0, TerminalCell{});

        screen->scrollRows(0, 2, -1, TerminalCell{});
        screen->eraseCells(1, 0, 2, TerminalCell{});
        screen->writeAsciiRun(1, 0, third, 1, attrs, 0, 0, TerminalCell{});
        screen->scrollRows(0, 2, -1, TerminalCell{});
        screen->eraseCells(1, 0, 2, TerminalCell{});
        screen->writeAsciiRun(1, 0, fourth, 1, attrs, 0, 0, TerminalCell{});
        screen->scrollRows(0, 2, -1, TerminalCell{});
        screen->eraseCells(1, 0, 2, TerminalCell{});
        screen->writeAsciiRun(1, 0, fifth, 1, attrs, 0, 0, TerminalCell{});

        STD_INSIST(screen->info().historyRows == 2);
        screen->scrollView(2);
        STD_INSIST(screen->testCell(0, 0).uc_pt == 'B');
        STD_INSIST(screen->testCell(1, 0).uc_pt == 'C');
        STD_INSIST(screen->scrollView(-0x7fffffff));
        STD_INSIST(screen->info().historyRows == 2);
        STD_INSIST(screen->testCell(0, 0).uc_pt == 'D');
        STD_INSIST(screen->testCell(1, 0).uc_pt == 'E');
    }

    STD_TEST(TopAnchoredPartialScrollPreservesRowsBelowRegion) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        composer.setCellExtras(CellExtraStore::create(composer, 8));
        TerminalColors colors;
        configureColors(colors);
        Screen* screen = Screen::createPrimary(composer, *pool, 2, 4, &colors, 2);
        const TerminalCell attrs = attributes();
        const u8 first[] = {'A'};
        const u8 second[] = {'B'};
        const u8 third[] = {'C'};
        const u8 fourth[] = {'D'};
        screen->writeAsciiRun(0, 0, first, 1, attrs, 0, 0, TerminalCell{});
        screen->writeAsciiRun(1, 0, second, 1, attrs, 0, 0, TerminalCell{});
        screen->writeAsciiRun(2, 0, third, 1, attrs, 0, 0, TerminalCell{});
        screen->writeAsciiRun(3, 0, fourth, 1, attrs, 0, 0, TerminalCell{});

        screen->scrollRows(0, 3, -1, TerminalCell{});
        screen->eraseCells(2, 0, 2, TerminalCell{});

        STD_INSIST(screen->info().historyRows == 1);
        STD_INSIST(screen->testCell(0, 0).uc_pt == 'B');
        STD_INSIST(screen->testCell(1, 0).uc_pt == 'C');
        STD_INSIST(screen->testCell(2, 0).uc_pt == 0);
        STD_INSIST(screen->testCell(3, 0).uc_pt == 'D');
        screen->scrollView(1);
        STD_INSIST(screen->testCell(0, 0).uc_pt == 'A');
        STD_INSIST(screen->testCell(1, 0).uc_pt == 'B');
        STD_INSIST(screen->testCell(2, 0).uc_pt == 'C');
    }

    STD_TEST(ReturnsExplicitAndDetectedHyperlinks) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        composer.setCellExtras(CellExtraStore::create(composer, 64));
        TerminalColors colors;
        configureColors(colors);
        Screen* screen = Screen::createAlternate(composer, *pool, 32, 2, &colors);
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
        composer.setCellExtras(CellExtraStore::create(composer, 16));
        TerminalColors colors;
        configureColors(colors);
        Screen* screen = Screen::createAlternate(composer, *pool, 4, 1, &colors);
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

    STD_TEST(ResizeTransfersContentToReplacement) {
        auto composerPool = ObjPool::fromMemory();
        auto sourcePool = ObjPool::fromMemory();
        auto destinationPool = ObjPool::fromMemory();
        Composer composer(composerPool.mutPtr());
        composer.setCellExtras(CellExtraStore::create(composer, 8));
        TerminalColors colors;
        configureColors(colors);
        Screen* screen = Screen::createAlternate(composer, *sourcePool, 4, 2, &colors);
        const TerminalCell attrs = attributes();
        const u8 text[] = {'a', 'b', 'c'};
        screen->writeAsciiRun(0, 0, text, 3, attrs, 0, 0, TerminalCell{});

        Screen::Cursor cursor;
        Screen* replacement = screen->resized(*destinationPool, 4, 2, cursor);

        STD_INSIST(replacement->testCell(0, 0).uc_pt == 'a');
        STD_INSIST(replacement->testCell(0, 1).uc_pt == 'b');
        STD_INSIST(replacement->testCell(0, 2).uc_pt == 'c');
    }

    STD_TEST(FindsBlinkingTextInVisibleCells) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        composer.setCellExtras(CellExtraStore::create(composer, 4));
        TerminalColors colors;
        configureColors(colors);
        Screen* screen = Screen::createAlternate(composer, *pool, 2, 2, &colors);
        TerminalCell attrs = attributes();

        STD_INSIST(!screen->hasBlinkingText());
        attrs.blink = true;
        screen->writeCodepoint(1, 1, 'x', false, attrs, 0, 0, TerminalCell{});
        STD_INSIST(screen->hasBlinkingText());
        screen->eraseCells(1, 1, 1, TerminalCell{});
        STD_INSIST(!screen->hasBlinkingText());
    }

    STD_TEST(FillCellsProducesCompleteIncrementalUpdate) {
        verifyDamage([](Screen& screen) {
            TerminalCell attrs = attributes();
            attrs.bold = true;
            screen.fillCells('Z', attrs);
        });
    }

    STD_TEST(SetLineAttributeProducesCompleteIncrementalUpdate) {
        verifyDamage([](Screen& screen) {
            screen.setLineAttribute(2, 7);
        });
    }

    STD_TEST(SetWrappedProducesCompleteIncrementalUpdate) {
        verifyDamage([](Screen& screen) {
            screen.setWrapped(2, 6);
        });
    }

    STD_TEST(WriteCodepointProducesCompleteIncrementalUpdate) {
        verifyDamage([](Screen& screen) {
            screen.writeCodepoint(1, 3, 'x', false, attributes(), 0, 1, TerminalCell{});
        });
    }

    STD_TEST(WriteWideCodepointProducesCompleteIncrementalUpdate) {
        verifyDamage([](Screen& screen) {
            screen.writeCodepoint(2, 5, 0x4e02, true, attributes(), 0, 2, TerminalCell{});
        });
    }

    STD_TEST(WriteGraphemeProducesCompleteIncrementalUpdate) {
        verifyDamage([](Screen& screen) {
            const u32 grapheme[] = {'e', 0x301};
            screen.writeGrapheme(3, 1, grapheme, 2, false, attributes(), 0, 3, TerminalCell{});
        });
    }

    STD_TEST(WriteAsciiRunProducesCompleteIncrementalUpdate) {
        verifyDamage([](Screen& screen) {
            const u8 text[] = {'r', 'u', 'n', '!'};
            screen.writeAsciiRun(1, 1, text, sizeof(text), attributes(), 0, 1, TerminalCell{});
        });
    }

    STD_TEST(WriteAsciiLinesProducesCompleteIncrementalUpdate) {
        verifyDamage([](Screen& screen) {
            const u8 text[] = {'a', 'b', '\r', '\n', 'c', '\r', '\n'};
            const u16 lengths[] = {2, 1};
            screen.writeAsciiLines(1, text, lengths, 2, attributes(), 0, 2, TerminalCell{});
        });
    }

    STD_TEST(WriteAsciiLinesWhileScrolledProducesCompleteIncrementalUpdate) {
        verifyDamage([](Screen& screen) {
            prepareHistory(screen);
            screen.scrollView(2);
        }, [](Screen& screen) {
            const u8 text[] = {'a', 'b', '\r', '\n', 'c', '\r', '\n'};
            const u16 lengths[] = {2, 1};
            screen.writeAsciiLines(1, text, lengths, 2, attributes(), 0, 2, TerminalCell{});
        });
    }

    STD_TEST(WriteAsciiRunInsertProducesCompleteIncrementalUpdate) {
        verifyDamage([](Screen& screen) {
            const u8 text[] = {'i', 'n', 's'};
            screen.writeAsciiRunInsert(1, 1, screen.info().columns, text, sizeof(text), attributes(), 0, 1, TerminalCell{});
        });
    }

    STD_TEST(WriteRunProducesCompleteIncrementalUpdate) {
        verifyDamage([](Screen& screen) {
            const u32 text[] = {'R', 0x4e03, 'N'};
            screen.writeRun(2, 1, text, 3, attributes(), 0, 1, TerminalCell{});
        });
    }

    STD_TEST(WriteRepeatedCodepointProducesCompleteIncrementalUpdate) {
        verifyDamage([](Screen& screen) {
            screen.writeRepeatedCodepoint(2, 1, 4, 'R', attributes(), 0, 1, TerminalCell{});
        });
    }

    STD_TEST(WriteGlyphRunProducesCompleteIncrementalUpdate) {
        verifyDamage([](Screen& screen) {
            const u32 text[] = {'G', 0x4e04, 'R'};
            const u8 widths[] = {1, 2, 1};
            screen.writeGlyphRun(2, 1, text, widths, 3, 4, attributes(), 0, 1, TerminalCell{});
        });
    }

    STD_TEST(FillRectangleProducesCompleteIncrementalUpdate) {
        verifyDamage([](Screen& screen) {
            screen.fillRectangle(1, 1, 4, screen.info().columns - 1, 'F', attributes(), TerminalCell{});
        });
    }

    STD_TEST(CopyRectangleProducesCompleteIncrementalUpdate) {
        verifyDamage([](Screen& screen) {
            screen.copyRectangle(0, 1, 2, 2, 2, 4, TerminalCell{});
        });
    }

    STD_TEST(ChangeRectangleAttributesProducesCompleteIncrementalUpdate) {
        verifyDamage([](Screen& screen) {
            CellAttributeChange change;
            change.toggle(CellAttributeChange::Bold | CellAttributeChange::Inverse);
            screen.changeRectangleAttributes(1, 1, 4, screen.info().columns - 1, change);
        });
    }

    STD_TEST(ChangeRectangleAttributesAppliesFullSgrState) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        composer.setCellExtras(CellExtraStore::create(composer, 12));
        TerminalColors colors;
        configureColors(colors);
        Screen* screen = Screen::createPrimary(composer, *pool, 4, 3, &colors, 0);
        TerminalCell initial = attributes();
        initial.faint = true;
        initial.setInlineUnderlineColor(CellColor::indexed(6));
        const u8 text[] = {'a', 'b', 'c', 'd'};
        for (u16 row = 0; row < 3; ++row) {
            screen->writeAsciiRun(row, 0, text, sizeof(text), initial, 0, 0, TerminalCell{});
        }

        CellAttributeChange change;
        change.set(CellAttributeChange::Bold | CellAttributeChange::Italic | CellAttributeChange::Strike | CellAttributeChange::Overline, true);
        change.set(CellAttributeChange::Faint, false);
        change.setUnderline(3);
        change.setForeground(CellColor::indexed(10));
        change.setBackground(CellColor::direct({1, 2, 3}));
        change.setUnderlineColor(CellColor::direct({4, 5, 6}));
        screen->changeRectangleAttributes(0, 0, 3, 4, change);

        for (u16 row = 0; row < 3; ++row) {
            for (u16 column = 0; column < 4; ++column) {
                const TerminalCell& cell = screen->testCell(row, column);
                STD_INSIST(cell.bold);
                STD_INSIST(!cell.faint);
                STD_INSIST(cell.italic);
                STD_INSIST(cell.underline_style == 3);
                STD_INSIST(cell.strike);
                STD_INSIST(cell.overline);
                STD_INSIST(cell.foreground() == CellColor::indexed(10));
                STD_INSIST(cell.background() == CellColor::direct({1, 2, 3}));
                STD_INSIST(composer.cellExtras->underlineColor(cell) == CellColor::direct({4, 5, 6}));
            }
        }
    }

    STD_TEST(ChangeRectangleAttributesCanResetEveryVisualAttribute) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        composer.setCellExtras(CellExtraStore::create(composer, 1));
        TerminalColors colors;
        configureColors(colors);
        Screen* screen = Screen::createPrimary(composer, *pool, 1, 1, &colors, 0);
        TerminalCell initial = attributes();
        initial.bold = true;
        initial.faint = true;
        initial.italic = true;
        initial.underline_style = 5;
        initial.blink = true;
        initial.inverse = true;
        initial.conceal = true;
        initial.strike = true;
        initial.overline = true;
        initial.setForeground(CellColor::indexed(3));
        initial.setBackground(CellColor::indexed(4));
        initial.setInlineUnderlineColor(CellColor::indexed(5));
        screen->writeCodepoint(0, 0, 'x', false, initial, 0, 0, TerminalCell{});

        CellAttributeChange change;
        constexpr u16 allAttributes = CellAttributeChange::Bold | CellAttributeChange::Faint | CellAttributeChange::Italic | CellAttributeChange::Underline | CellAttributeChange::Blink | CellAttributeChange::Inverse | CellAttributeChange::Conceal | CellAttributeChange::Strike | CellAttributeChange::Overline;
        change.set(allAttributes, false);
        change.setUnderline(0);
        change.setForeground(CellColor::defaultForeground());
        change.setBackground(CellColor::defaultBackground());
        change.setUnderlineFromForeground();
        screen->changeRectangleAttributes(0, 0, 1, 1, change);

        const TerminalCell& cell = screen->testCell(0, 0);
        STD_INSIST(!(cell.bold || cell.faint || cell.italic || cell.underline_style || cell.blink || cell.inverse || cell.conceal || cell.strike || cell.overline));
        STD_INSIST(cell.foreground() == CellColor::defaultForeground());
        STD_INSIST(cell.background() == CellColor::defaultBackground());
        STD_INSIST(composer.cellExtras->underlineColor(cell) == CellColor::defaultForeground());
    }

    STD_TEST(CollectExtraCellsProducesValidIncrementalUpdate) {
        verifyDamage([](Screen& screen) {
            Vector<TerminalCell*> cells;
            screen.collectExtraCells(cells);
        });
    }

    STD_TEST(EraseCellsProducesCompleteIncrementalUpdate) {
        verifyDamage([](Screen& screen) {
            screen.eraseCells(1, 2, 4, TerminalCell{});
        });
    }

    STD_TEST(SelectiveEraseCellsProducesCompleteIncrementalUpdate) {
        verifyDamage([](Screen& screen) {
            screen.selectiveEraseCells(2, 1, 5, TerminalCell{}, TerminalCell::isoProtection);
        });
    }

    STD_TEST(InsertCellsProducesCompleteIncrementalUpdate) {
        verifyDamage([](Screen& screen) {
            screen.insertCells(1, 2, screen.info().columns - 1, 2, TerminalCell{});
        });
    }

    STD_TEST(DeleteCellsProducesCompleteIncrementalUpdate) {
        verifyDamage([](Screen& screen) {
            screen.deleteCells(1, 2, screen.info().columns - 1, 2, TerminalCell{});
        });
    }

    STD_TEST(CopyRowProducesCompleteIncrementalUpdate) {
        verifyDamage([](Screen& screen) {
            screen.copyRow(4, 1, 0, screen.info().columns, TerminalCell{});
        });
    }

    STD_TEST(ScrollPartialRectangleUpProducesCompleteIncrementalUpdate) {
        verifyDamage([](Screen& screen) {
            screen.scrollRectangle(0, 1, 5, screen.info().columns - 1, -1, TerminalCell{});
        });
    }

    STD_TEST(ScrollFullRectangleUpProducesCompleteIncrementalUpdate) {
        verifyDamage([](Screen& screen) {
            screen.scrollRectangle(0, 0, 5, screen.info().columns, -2, TerminalCell{});
        });
    }

    STD_TEST(ScrollPartialRectangleDownProducesCompleteIncrementalUpdate) {
        verifyDamage([](Screen& screen) {
            screen.scrollRectangle(0, 1, 5, screen.info().columns - 1, 1, TerminalCell{});
        });
    }

    STD_TEST(ScrollFullRectangleDownProducesCompleteIncrementalUpdate) {
        verifyDamage([](Screen& screen) {
            screen.scrollRectangle(0, 0, 5, screen.info().columns, 2, TerminalCell{});
        });
    }

    STD_TEST(RotateRowsUpProducesCompleteIncrementalUpdate) {
        verifyDamage([](Screen& screen) {
            screen.rotateRows(0, 5, -2);
        });
    }

    STD_TEST(RotateRowsDownProducesCompleteIncrementalUpdate) {
        verifyDamage([](Screen& screen) {
            screen.rotateRows(0, 5, 2);
        });
    }

    STD_TEST(ScrollUpProducesCompleteIncrementalUpdate) {
        verifyDamage([](Screen& screen) {
            screen.scrollRows(0, 5, -2, TerminalCell{});
        });
    }

    STD_TEST(ScrollPartialRegionUpProducesCompleteIncrementalUpdate) {
        verifyDamage([](Screen& screen) {
            screen.scrollRows(1, 4, -1, TerminalCell{});
        });
    }

    STD_TEST(ScrollDownProducesCompleteIncrementalUpdate) {
        verifyDamage([](Screen& screen) {
            screen.scrollRows(0, 5, 2, TerminalCell{});
        });
    }

    STD_TEST(DropScrollbackHistoryProducesCompleteIncrementalUpdate) {
        verifyDamage([](Screen& screen) {
            prepareHistory(screen);
            screen.scrollView(2);
        }, [](Screen& screen) {
            screen.dropHistory();
        });
    }

    STD_TEST(PageUpProducesCompleteIncrementalUpdate) {
        verifyDamage([](Screen& screen) {
            prepareHistory(screen);
        }, [](Screen& screen) {
            screen.scrollView(2);
        });
    }

    STD_TEST(PageDownProducesCompleteIncrementalUpdate) {
        verifyDamage([](Screen& screen) {
            prepareHistory(screen);
            screen.scrollView(2);
        }, [](Screen& screen) {
            screen.scrollView(-1);
        });
    }

    STD_TEST(PageToBottomProducesCompleteIncrementalUpdate) {
        verifyDamage([](Screen& screen) {
            prepareHistory(screen);
            screen.scrollView(2);
        }, [](Screen& screen) {
            STD_INSIST(screen.scrollView(-0x7fffffff));
        });
    }

    STD_TEST(ExposeProducesValidFullUpdate) {
        verifyDamage([](Screen& screen) {
            screen.expose();
        });
    }

    STD_TEST(CycleSelectionSnapProducesCompleteMetadataUpdate) {
        verifyDamage([](Screen& screen) {
            screen.beginSelection(Point(2, 1));
            screen.updateSelection(Rect(2, 1, 4, 2));
        }, [](Screen& screen) {
            screen.cycleSelectionSnap();
        }, false);
    }

    STD_TEST(UpdateSelectionProducesCompleteMetadataUpdate) {
        verifyDamage([](Screen& screen) {
            screen.beginSelection(Point(2, 1));
        }, [](Screen& screen) {
            screen.updateSelection(Rect(2, 1, 4, 2));
        }, false);
    }

    STD_TEST(BeginSelectionProducesCompleteMetadataUpdate) {
        verifyDamage([](Screen& screen) {
            screen.beginSelection(Point(1, 1));
            screen.updateSelection(Rect(1, 1, 5, 3));
        }, false);
    }

    STD_TEST(ResizeWithoutReflowProducesCompleteIncrementalUpdate) {
        verifyResizeDamage(false);
    }

    STD_TEST(ResizeWithReflowProducesCompleteIncrementalUpdate) {
        verifyResizeDamage(true);
    }
}
