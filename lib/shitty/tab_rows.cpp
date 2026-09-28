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
    Vector<PanePlacement> placements;
    // Laid out in a box of this many units a side and divided back down:
    // fine enough that a share's rounding does not show in a map a couple
    // of dozen points wide.
    const u16 unit = 1000;
    for (size_t tab = 0; tab < count; ++tab) {
        panes.clear();
        sessions.panes(tab, panes);
        placements.clear();
        sessions.paneLayout(tab, PixelRect{0, 0, unit, unit}, placements);
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
            for (const PanePlacement& placement : placements) {
                if (placement.pane == row.pane) {
                    row.left = (float)(placement.area.x) / unit;
                    row.top = (float)(placement.area.y) / unit;
                    row.width = (float)(placement.area.width) / unit;
                    row.height = (float)(placement.area.height) / unit;
                    break;
                }
            }
            out.pushBack(row);
        }
    }
}
