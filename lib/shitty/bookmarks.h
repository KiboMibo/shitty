/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#pragma once

#include "startup.h"

#include <std/lib/vector.h>
#include <std/str/view.h>
#include <std/sys/types.h>

namespace stl {
    class ObjPool;
    class StringBuilder;
}

// Bookmarks: tabs the user keeps - an ssh host, a project directory -
// listed at the top of the sidebar whether or not they are open, and
// opened by a click.
//
// They live in a file of their own beside the config, bookmarks.toml,
// holding nothing but [[bookmark]] tables:
//
//     [[bookmark]]
//     title = "prod"
//     command = "ssh prod"
//     dir = "~"
//
// A file of their own, and not keys in the config or a file the config
// imports, because the sidebar writes to it (pinning a tab appends a
// block): a file the program rewrites must not be one that also holds
// the user's other settings and their comments.
//
// Portable and free of AppKit, so everything but the drawing is in reach
// of a headless test.
struct Bookmark {
    // Stable for the life of the process and never 0, so a tab can name
    // the bookmark it was opened from and 0 can mean "none".
    u64 id = 0;
    stl::StringView title;
    // Run by the user's shell (`$SHELL -c command`); empty is the shell
    // itself, which is what a directory bookmark is.
    stl::StringView command;
    // Where the child starts; `~` and `~/` stand for home. Empty starts
    // wherever a new tab would.
    stl::StringView directory;
    // Which [[bookmark]] header of its file this entry came from, counted
    // from 0 over every header, the ones left out included: the block
    // unpinning cuts.
    u32 block = 0;
};

// bookmarks.toml in the directory of the config file this process
// resolved (Options::configPath), -config override included - the same
// derivation, for the same reason, as defaultQuickFramePath(). False, out
// untouched, when configPath is empty.
bool defaultBookmarksPath(stl::StringView configPath, stl::StringBuilder& out);

// Appends the [[bookmark]] entries of one document to `out`, their
// strings interned in `pool` and their ids counted up from `nextId`.
// Like the config, a problem never stops the terminal: an entry that is
// not understood is warned about on stderr, prefixed with `identifier`
// and `path` (nothing is said when `identifier` is empty), and left out,
// and a syntax error keeps the entries before it.
void parseBookmarks(stl::StringView text, stl::StringView identifier, stl::StringView path, stl::ObjPool& pool, u64& nextId, stl::Vector<Bookmark>& out);

// parseBookmarks() over a file. A missing or unreadable file is no
// bookmarks and no warning: most users have none.
void loadBookmarks(stl::StringView path, stl::StringView identifier, stl::ObjPool& pool, u64& nextId, stl::Vector<Bookmark>& out);

// What a bookmark's tab runs: `shell` - the user's shell as the process
// resolved it at startup, login or not as the `login` option said - with
// `-c command` after its arguments when there is a command. A login
// shell reads the profile first, so `ssh` finds the agent and the PATH it
// would from an ordinary tab. Pure: the shell was resolved (and SHELL
// set) once, before any thread existed, and is not resolved again here.
LaunchCommand bookmarkLaunchCommand(const LaunchCommand& shell, stl::StringView command);

// The window's bookmarks and where they came from - what the sidebar
// lists and what a tab opened from one names by id. One per process,
// on the composer, loaded at startup.
struct BookmarkShelf {
    stl::Vector<Bookmark> items;
    // The file the items were read from and pins go to; empty when no
    // path could be computed (no config path at all).
    stl::StringView path;
    u64 nextId = 1;

    // The bookmark with this id, or null.
    const Bookmark* find(u64 id) const;
    // Its position in items, or items.length() when there is none.
    size_t indexOf(u64 id) const;
};

// The line under a bookmark's title in the sidebar: whether it is open,
// then what it runs, or where when it runs only the shell - "open · ssh
// prod", "not open · ~/Projects/shitty". Replaces what `out` held.
void bookmarkStatus(const Bookmark& bookmark, bool open, stl::StringBuilder& out);

// Pinning and unpinning: the file is changed a whole block at a time and
// everything else in it - comments, blank lines, entries this process
// did not understand - is kept byte for byte.
//
// The block for one bookmark, TOML-escaped, ending in a newline; a key
// with nothing in it is left out.
void bookmarkBlock(const Bookmark& bookmark, stl::StringBuilder& out);
// `text` with the bookmark's block added at the end, a blank line before
// it. Replaces what `out` held.
void appendBookmark(stl::StringView text, const Bookmark& bookmark, stl::StringBuilder& out);
// `text` without the block of the first entry equal to `bookmark` (by
// title, command and dir): from its [[bookmark]] line to the next table
// header or the end. Comments inside that span go with it. False, out
// untouched, when no such entry is in the text.
bool removeBookmark(stl::StringView text, const Bookmark& bookmark, stl::StringBuilder& out);
// Same title, command and dir.
bool sameBookmark(const Bookmark& a, const Bookmark& b);

// A command line as a shell would need it typed: the NUL-separated
// arguments processCommandLine() reads, joined by spaces, each one that
// the shell would split or expand in single quotes. Replaces what `out`
// held.
void shellCommandLine(stl::StringView arguments, stl::StringBuilder& out);

// The shelf against its file again: entries equal to ones it holds keep
// their ids, so a tab opened from one keeps naming it; the rest are new.
void reloadBookmarks(BookmarkShelf& shelf, stl::ObjPool& pool, stl::StringView identifier);
// Adds a bookmark to the shelf's file and the shelf; its new id in `id`.
// False when there is no file to write or it could not be written, and
// then nothing changed.
bool pinBookmark(BookmarkShelf& shelf, stl::ObjPool& pool, stl::StringView identifier, const Bookmark& bookmark, u64& id);
// Takes the bookmark with this id out of the file and the shelf. False
// when it is not on the shelf, or the file no longer holds it, or the
// file could not be written; then nothing changed.
bool unpinBookmark(BookmarkShelf& shelf, stl::ObjPool& pool, stl::StringView identifier, u64 id);
