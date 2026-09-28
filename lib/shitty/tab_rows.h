/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include <std/lib/vector.h>
#include <std/sys/types.h>

#include <stddef.h>

struct SessionSet;

// Split groups: the rows a tab list draws, one per pane rather than one
// per tab. A tab holding one pane is one plain row, exactly the row the
// list always drew; a split tab is a group, its panes' rows in visual
// order with the first and the last marked so the list can draw one frame
// round the whole run.
//
// Portable and free of AppKit on purpose: which row is which, and which
// of them is selected, is the part of the sidebar a headless test can
// reach. The strings a row shows stay the sidebar's own - they need the
// process's directory and the branch above it - and are asked by the row's
// pane.
struct TabRow {
    size_t tab = 0;
    u64 pane = 0;
    // The row belongs to a tab of two panes or more.
    bool grouped = false;
    bool groupFirst = false;
    bool groupLast = false;
    // The tab's focused pane - the row a tab's number and the selection
    // belong to.
    bool focused = false;
    // The tab is the window's active one.
    bool activeTab = false;
    // Where the pane sits in its tab, as fractions of the tab's box, 0..1
    // on each axis: the cell this row is in the group's map of its split.
    // The whole box for a tab of one pane.
    float left = 0;
    float top = 0;
    float width = 1;
    float height = 1;
};

// Every tab's rows, tabs in order; replaces what `out` held.
void tabRows(const SessionSet& sessions, stl::Vector<TabRow>& out);
