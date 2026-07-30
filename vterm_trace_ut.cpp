/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "vterm_trace.h"

#include "composer.h"

#include <std/mem/obj_pool.h>
#include <std/tst/ut.h>

using namespace stl;

STD_TEST_SUITE(VtermTrace) {
    STD_TEST(MergesAdjacentTextAndKeepsControlsSeparate) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        VtermTrace* trace = VtermTrace::create(composer);
        const u8 first[] = {'a', 'b'};
        const u8 second[] = {'c'};

        trace->text(first, sizeof(first));
        trace->text(second, sizeof(second));
        trace->control(0x0a);

        STD_INSIST(trace->drain() == "text 616263\ncontrol 0a\n");
    }

    STD_TEST(WithholdsIncompleteEscapeUntilItEnds) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        VtermTrace* trace = VtermTrace::create(composer);
        trace->control(0x07);
        trace->escapeBegin();
        trace->escapeByte('(');

        STD_INSIST(trace->drain() == "control 07\n");
        STD_INSIST(trace->drain().empty());

        trace->escapeByte('B');
        trace->escapeEnd();
        STD_INSIST(trace->drain() == "escape 2842\n");
    }

    STD_TEST(CancelDropsIncompleteEscape) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        VtermTrace* trace = VtermTrace::create(composer);
        trace->escapeBegin();
        trace->escapeByte('[');
        trace->escapeCancel();

        STD_INSIST(trace->drain().empty());
    }

    STD_TEST(SerializesCsiParametersAndSeparators) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        VtermTrace* trace = VtermTrace::create(composer);
        const u32 parameters[] = {1, 2, 3};
        const unsigned char separators[] = {0, ';', ':'};

        trace->csi('p', "?", "$", parameters, separators, 3, true);

        STD_INSIST(trace->drain() == "csi 3f313b323a332470\n");
    }

    STD_TEST(RecordsCompletedControlStrings) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        VtermTrace* trace = VtermTrace::create(composer);
        const u8 data[] = {'t', 'i', 't', 'l', 'e'};

        trace->stringBegin(VtermTraceString::Osc);
        trace->stringData(data, sizeof(data));
        STD_INSIST(trace->drain().empty());
        trace->stringEnd();

        STD_INSIST(trace->drain() == "osc 7469746c65\n");
    }

    STD_TEST(CancelDropsControlString) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        VtermTrace* trace = VtermTrace::create(composer);
        const u8 data[] = {'x'};
        trace->stringBegin(VtermTraceString::Apc);
        trace->stringData(data, sizeof(data));
        trace->stringCancel();

        STD_INSIST(trace->drain().empty());
    }

    STD_TEST(ClearDropsCompleteAndPendingEvents) {
        auto pool = ObjPool::fromMemory();
        Composer& composer = *pool->make<Composer>(pool.mutPtr());
        VtermTrace* trace = VtermTrace::create(composer);
        trace->control(0x07);
        trace->escapeBegin();
        trace->escapeByte('[');

        trace->clear();

        STD_INSIST(trace->drain().empty());
        trace->escapeEnd();
        STD_INSIST(trace->drain().empty());
    }
}
