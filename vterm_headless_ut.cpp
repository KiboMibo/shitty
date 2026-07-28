/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "vterm_headless.h"

#include "composer.h"
#include "input_sink.h"
#include "vterm.h"

#include <std/mem/obj_pool.h>
#include <std/tst/ut.h>

using namespace stl;

namespace {
    const TerminalCell* findUpdateCell(const TerminalUpdate& update, u32 index) {
        for (size_t spanIndex = 0; spanIndex < update.spanCount; ++spanIndex) {
            const TerminalCellSpan& span = update.spans[spanIndex];
            if (index >= span.index && index < span.index + span.count) {
                return span.cells + index - span.index;
            }
        }
        return nullptr;
    }

    void consumeInitialDamage(Vterm& terminal) {
        terminal.expose();
        if (terminal.output() != nullptr) {
            terminal.consume();
        }
    }
}

STD_TEST_SUITE(VtermHeadless) {
    STD_TEST(PublishedCheckpointSurvivesWorkingStateAdvance) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        VtermHeadless::create(composer);
        Vterm& terminal = *composer.vterm;
        consumeInitialDamage(terminal);

        terminal.feedPty(StringView(u8"A"));
        terminal.feedPty(StringView(u8"\x1b[?2026h\rB"));

        const TerminalUpdate* const update = terminal.output();
        STD_INSIST(update != nullptr);
        const TerminalCell* const cell = findUpdateCell(*update, 0);
        STD_INSIST(cell != nullptr);
        STD_INSIST(cell->uc_pt == 'A');
    }

    STD_TEST(ReturnedUpdateCellsRemainImmutableUntilConsume) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        VtermHeadless::create(composer);
        Vterm& terminal = *composer.vterm;
        consumeInitialDamage(terminal);

        terminal.feedPty(StringView(u8"A"));
        const TerminalUpdate* const update = terminal.output();
        STD_INSIST(update != nullptr);
        const TerminalCell* const cell = findUpdateCell(*update, 0);
        STD_INSIST(cell != nullptr);
        STD_INSIST(cell->uc_pt == 'A');

        terminal.feedPty(StringView(u8"\rB"));

        STD_INSIST(cell->uc_pt == 'A');
    }

    STD_TEST(SynchronizedEndPublishesInsideSingleFeed) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        VtermHeadless::create(composer);
        Vterm& terminal = *composer.vterm;
        consumeInitialDamage(terminal);

        terminal.feedPty(StringView(u8"\x1b[?2026hA\x1b[?2026l\x1b[?2026hB"));

        const TerminalUpdate* const update = terminal.output();
        STD_INSIST(update != nullptr);
        const TerminalCell* const first = findUpdateCell(*update, 0);
        STD_INSIST(first != nullptr);
        STD_INSIST(first->uc_pt == 'A');
        const TerminalCell* const second = findUpdateCell(*update, 1);
        STD_INSIST(second == nullptr || second->uc_pt != 'B');
    }

    STD_TEST(PendingCheckpointExcludesFollowingSynchronizedOutput) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        VtermHeadless::create(composer);
        Vterm& terminal = *composer.vterm;
        consumeInitialDamage(terminal);

        terminal.feedPty(StringView(u8"A"));
        terminal.feedPty(StringView(u8"\x1b[?2026hB"));

        const TerminalUpdate* const update = terminal.output();
        STD_INSIST(update != nullptr);
        const TerminalCell* const first = findUpdateCell(*update, 0);
        STD_INSIST(first != nullptr);
        STD_INSIST(first->uc_pt == 'A');
        const TerminalCell* const second = findUpdateCell(*update, 1);
        STD_INSIST(second == nullptr || second->uc_pt != 'B');
    }

    STD_TEST(SynchronizedOutputDoesNotBlockLocalFocusUpdate) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        VtermHeadless::create(composer);
        Vterm& terminal = *composer.vterm;
        consumeInitialDamage(terminal);

        terminal.feedPty(StringView(u8"\x1b[?2026h"));
        composer.input->focus(true);

        STD_INSIST(terminal.output() != nullptr);
    }

    STD_TEST(PtyAndTerminalOutputsAreConsumedIndependently) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        VtermHeadless::create(composer);
        Vterm& terminal = *composer.vterm;
        if (terminal.output() != nullptr) {
            terminal.consume();
        }
        const u8 input[] = {'a', 0x1b, '[', 'c'};

        terminal.feedPty(StringView(input, sizeof(input)));

        const StringView pty = terminal.ptyOutput();
        STD_INSIST(!pty.empty());
        STD_INSIST(terminal.output() != nullptr);
        terminal.consumePtyOutput(pty.length());
        STD_INSIST(terminal.ptyOutput().empty());
        STD_INSIST(terminal.output() != nullptr);
        terminal.consume();
        STD_INSIST(terminal.output() == nullptr);
    }

    STD_TEST(FeedConsumesTerminalAndPtyOutput) {
        auto pool = ObjPool::fromMemory();
        Composer composer(pool.mutPtr());
        VtermHeadless* const headless = VtermHeadless::create(composer);
        const u8 input[] = {'a', 0x1b, '[', 'c'};

        headless->feed(input, sizeof(input));

        STD_INSIST(composer.vterm->ptyOutput().empty());
        STD_INSIST(composer.vterm->output() == nullptr);

        headless->feed(input, sizeof(input));

        STD_INSIST(composer.vterm->ptyOutput().empty());
        STD_INSIST(composer.vterm->output() == nullptr);
    }
}
