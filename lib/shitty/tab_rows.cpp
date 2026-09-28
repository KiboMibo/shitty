/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "tab_rows.h"

#include "session.h"
#include "bookmarks.h"

using namespace stl;

namespace {
    void appendTab(const SessionSet& sessions, size_t tab, u64 bookmark, Vector<u64>& panes, Vector<PanePlacement>& placements, Vector<TabRow>& out) {
        // Laid out in a box of this many units a side and divided back
        // down: fine enough that a share's rounding does not show in a map
        // a couple of dozen points wide.
        const u16 unit = 1000;
        panes.clear();
        sessions.panes(tab, panes);
        placements.clear();
        sessions.paneLayout(tab, PixelRect{0, 0, unit, unit}, placements);
        const u64 focused = sessions.focusedPane(tab);
        const size_t active = sessions.activeIndex();
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
            row.bookmark = bookmark;
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

void tabRows(const SessionSet& sessions, const BookmarkShelf* shelf, Vector<TabRow>& out) {
    out.clear();
    const size_t count = sessions.count();
    Vector<u64> panes;
    Vector<PanePlacement> placements;
    Vector<bool> listed;
    for (size_t tab = 0; tab < count; ++tab) {
        listed.pushBack(false);
    }
    if (shelf != nullptr) {
        for (const Bookmark& bookmark : shelf->items) {
            size_t tab = 0;
            while (tab < count && sessions.tabBookmark(tab) != bookmark.id) {
                ++tab;
            }
            if (tab < count) {
                appendTab(sessions, tab, bookmark.id, panes, placements, out);
                listed.mut(tab) = true;
            } else {
                TabRow row;
                row.bookmark = bookmark.id;
                row.closed = true;
                out.pushBack(row);
            }
        }
    }
    const size_t bookmarkRows = out.length();
    for (size_t tab = 0; tab < count; ++tab) {
        if (listed[tab]) {
            continue;
        }
        // A tab whose bookmark has left the shelf is an ordinary tab now.
        const size_t first = out.length();
        appendTab(sessions, tab, 0, panes, placements, out);
        if (first == bookmarkRows && bookmarkRows != 0 && first < out.length()) {
            out.mut(first).afterBookmarks = true;
        }
    }
}
