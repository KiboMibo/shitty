/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include <std/str/view.h>

#include <stddef.h>

namespace stl {
    class Buffer;
}

// The sidebar tab list's shape and the words on its rows, shared by the two
// windows that draw it: AppKit's (ui_sidebar_tabs.mm) and the one this
// program draws itself on Wayland (ui_wayland_chrome.cpp). Plain functions
// of plain types, so drawing and clicking on either platform answer from
// the same arithmetic, and a test reaches it without a window.
namespace SidebarMetrics {
    // A row: its padding and its three lines - what runs, where, which branch.
    inline constexpr double rowPad = 7;
    inline constexpr double titleLine = 15;
    inline constexpr double subLine = 13;
    inline constexpr double rowHeight = rowPad * 2 + titleLine + subLine * 2;
    // A folder's label row.
    inline constexpr double labelRowHeight = 24;
    // Below the top the list is given.
    inline constexpr double listTop = 6;
    // The pill in from the panel's sides, and the text in from the pill.
    inline constexpr double pillInset = 6;
    inline constexpr double textInset = pillInset + 10;
    // The column for the tab's number, or a bookmark's glyph, or the pin.
    inline constexpr double numberGutter = 18;
    // The folder and branch lines' icons.
    inline constexpr double iconColumn = 18;
}

stl::StringView sidebarTabsShortTitle(stl::StringView title);
stl::StringView sidebarTabsGitDirLink(stl::StringView contents);
stl::StringView sidebarTabsHeadBranch(stl::StringView head);
bool sidebarTabsBranch(stl::StringView directory, stl::Buffer& out);
double sidebarTabsLineTop(size_t line);
double sidebarTabsLineHeight(size_t line);
double sidebarTabsRowHeight();
double sidebarTabsListTop();
double sidebarTabsLineLeft(size_t line, double textLeft, bool iconsAvailable);
double sidebarTabsRowOffset(const double* heights, size_t count, size_t at);
long long sidebarTabsRowAtHeights(double panelHeight, double offsetFromTop, const double* heights, size_t count, double topInset);
long long sidebarTabsRowAt(double panelHeight, double offsetFromTop, size_t count, double topInset);
bool sidebarTabsPinAt(double offsetFromLeft);
