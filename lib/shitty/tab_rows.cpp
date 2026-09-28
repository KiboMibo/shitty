/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "tab_rows.h"

#include "session.h"

using namespace stl;

void tabRows(const SessionSet& sessions, Vector<TabRow>& out) {
    out.clear();
    const size_t count = sessions.count();
    const size_t active = sessions.activeIndex();
    Vector<u64> panes;
    for (size_t tab = 0; tab < count; ++tab) {
        panes.clear();
        sessions.panes(tab, panes);
        const u64 focused = sessions.focusedPane(tab);
        const size_t n = panes.length();
        for (size_t at = 0; at < n; ++at) {
            TabRow row;
            row.tab = tab;
            row.pane = panes[at];
            row.grouped = n > 1;
            row.groupFirst = row.grouped && at == 0;
            row.groupLast = row.grouped && at + 1 == n;
            row.focused = panes[at] == focused;
            row.activeTab = tab == active;
            out.pushBack(row);
        }
    }
}
