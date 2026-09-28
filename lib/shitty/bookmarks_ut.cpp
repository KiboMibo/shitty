/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "bookmarks.h"

#include <std/lib/vector.h>
#include <std/mem/obj_pool.h>
#include <std/str/builder.h>
#include <std/str/view.h>
#include <std/tst/ut.h>
#include <std/ios/fs_utils.h>
#include <std/lib/buffer.h>

#include <stdlib.h>
#include <unistd.h>

using namespace stl;

namespace {
    // A mkdtemp() directory of this test's own, as quick_frame_store_ut
    // makes one: TMPDIR when the runner gives one, which is inside the
    // tree under ./build.
    void makeTempDir(StringBuilder& dir) {
        const char* const directory = getenv("TMPDIR");
        dir << StringView(directory != nullptr ? directory : "/tmp") << StringView(u8"/bookmarks_ut.XXXXXX");
        STD_INSIST(mkdtemp(dir.cStr()) != nullptr);
    }

    void readAll(StringView path, Buffer& out) {
        Buffer pathBuf{path};
        readFileContent(pathBuf, out);
    }

    void parse(StringView text, Vector<Bookmark>& out, u64& nextId) {
        static ObjPool::Ref pool = ObjPool::fromMemory();
        parseBookmarks(text, StringView(u8"shitty"), StringView(u8"bookmarks.toml"), *pool, nextId, out);
    }
}

STD_TEST_SUITE(Bookmarks) {
    STD_TEST(EveryFieldOfEveryEntryIsRead) {
        Vector<Bookmark> out;
        u64 nextId = 5;
        parse(StringView(u8"# mine\n"
                         "[[bookmark]]\n"
                         "title = \"prod\"\n"
                         "command = \"ssh prod\"\n"
                         "dir = \"~\"\n"
                         "\n"
                         "[[bookmark]]\n"
                         "title = \"shitty\"\n"
                         "dir = \"~/Projects/shitty\"\n"),
              out, nextId);
        STD_INSIST(out.length() == 2);
        STD_INSIST(out[0].title == StringView(u8"prod"));
        STD_INSIST(out[0].command == StringView(u8"ssh prod"));
        STD_INSIST(out[0].directory == StringView(u8"~"));
        STD_INSIST(out[1].title == StringView(u8"shitty"));
        STD_INSIST(out[1].command.empty());
        STD_INSIST(out[1].directory == StringView(u8"~/Projects/shitty"));
        // Ids count up from where the caller is, distinct and never 0.
        STD_INSIST(out[0].id == 5 && out[1].id == 6 && nextId == 7);
    }

    // A title is optional: the command names the bookmark, else the dir.
    STD_TEST(AnUntitledBookmarkIsNamedByWhatItOpens) {
        Vector<Bookmark> out;
        u64 nextId = 1;
        parse(StringView(u8"[[bookmark]]\ncommand = \"ssh a\"\n[[bookmark]]\ndir = \"/srv\"\n"), out, nextId);
        STD_INSIST(out.length() == 2);
        STD_INSIST(out[0].title == StringView(u8"ssh a"));
        STD_INSIST(out[1].title == StringView(u8"/srv"));
    }

    // What is not understood is left out, and only that: every broken
    // entry sits between two good ones, which must both survive.
    STD_TEST(AnEntryNotUnderstoodIsLeftOutAndOnlyIt) {
        Vector<Bookmark> out;
        u64 nextId = 1;
        parse(StringView(u8"[[bookmark]]\ntitle = \"first\"\ncommand = \"a\"\n"
                         "[[bookmark]]\ntitle = \"nothing to open\"\n"
                         "[[bookmark]]\ntitle = \"number\"\ncommand = 3\n"
                         "[[bookmark]]\ntitle = \"list\"\ncommand = [\"a\", \"b\"]\n"
                         "[bookmark]\ntitle = \"not an array\"\ncommand = \"x\"\n"
                         "[other]\ncommand = \"y\"\n"
                         "[[bookmark]]\ntitle = \"last\"\ndir = \"/\"\nextra = \"ignored\"\n"),
              out, nextId);
        STD_INSIST(out.length() == 2);
        STD_INSIST(out[0].title == StringView(u8"first"));
        STD_INSIST(out[1].title == StringView(u8"last"));
        STD_INSIST(out[1].directory == StringView(u8"/"));
    }

    STD_TEST(KeysOutsideATableAreNotABookmark) {
        Vector<Bookmark> out;
        u64 nextId = 1;
        parse(StringView(u8"command = \"ssh a\"\n"), out, nextId);
        STD_INSIST(out.length() == 0);
        STD_INSIST(nextId == 1);
    }

    // A syntax error keeps what came before it.
    STD_TEST(ASyntaxErrorKeepsTheEntriesBeforeIt) {
        Vector<Bookmark> out;
        u64 nextId = 1;
        parse(StringView(u8"[[bookmark]]\ncommand = \"a\"\n[[bookmark]]\ncommand = \"b\n"), out, nextId);
        STD_INSIST(out.length() == 1);
        STD_INSIST(out[0].command == StringView(u8"a"));
    }

    STD_TEST(AMissingFileIsNoBookmarks) {
        static ObjPool::Ref pool = ObjPool::fromMemory();
        Vector<Bookmark> out;
        u64 nextId = 1;
        loadBookmarks(StringView(u8"/nonexistent/bookmarks.toml"), StringView(u8"shitty"), *pool, nextId, out);
        STD_INSIST(out.length() == 0);
        loadBookmarks(StringView(), StringView(u8"shitty"), *pool, nextId, out);
        STD_INSIST(out.length() == 0);
    }

    // Beside the config, whatever the config file is called: the brand's
    // own name for it must not leak into the bookmarks file's.
    STD_TEST(TheFileSitsBesideTheConfig) {
        StringBuilder shitty;
        STD_INSIST(defaultBookmarksPath(StringView(u8"/home/u/.config/shitty/shitty.toml"), shitty));
        STD_INSIST(StringView(shitty) == StringView(u8"/home/u/.config/shitty/bookmarks.toml"));
        StringBuilder pretty;
        STD_INSIST(defaultBookmarksPath(StringView(u8"/home/u/.config/pretty/pretty.toml"), pretty));
        STD_INSIST(StringView(pretty) == StringView(u8"/home/u/.config/pretty/bookmarks.toml"));
        StringBuilder bare;
        STD_INSIST(defaultBookmarksPath(StringView(u8"cfg.toml"), bare));
        STD_INSIST(StringView(bare) == StringView(u8"bookmarks.toml"));
        StringBuilder none;
        STD_INSIST(!defaultBookmarksPath(StringView(), none));
        STD_INSIST(StringView(none).empty());
    }

    // The shell's own arguments stay - a login shell keeps its dash - and
    // -c goes after them.
    STD_TEST(TheCommandRunsThroughTheShellAsItWasResolved) {
        LaunchCommand shell;
        shell.storage.append("/bin/zsh", 9);
        shell.executableOffset = 0;
        shell.offsets.pushBack((u32)(shell.storage.used()));
        shell.storage.append("-zsh", 5);

        const LaunchCommand command = bookmarkLaunchCommand(shell, StringView(u8"ssh prod"));
        STD_INSIST(StringView(command.executable()) == StringView(u8"/bin/zsh"));
        STD_INSIST(command.offsets.length() == 3);
        STD_INSIST(StringView(command.argument(0)) == StringView(u8"-zsh"));
        STD_INSIST(StringView(command.argument(1)) == StringView(u8"-c"));
        STD_INSIST(StringView(command.argument(2)) == StringView(u8"ssh prod"));

        const LaunchCommand bare = bookmarkLaunchCommand(shell, StringView());
        STD_INSIST(bare.offsets.length() == 1);
        STD_INSIST(StringView(bare.argument(0)) == StringView(u8"-zsh"));
    }

    STD_TEST(TheStatusSaysOpenAndWhatItRuns) {
        const Bookmark ssh{1, StringView(u8"prod"), StringView(u8"ssh prod"), StringView(u8"~")};
        const Bookmark folder{2, StringView(u8"shitty"), StringView(), StringView(u8"~/Projects/shitty")};
        StringBuilder out;
        bookmarkStatus(ssh, true, out);
        STD_INSIST(StringView(out) == StringView(u8"open · ssh prod"));
        bookmarkStatus(ssh, false, out);
        STD_INSIST(StringView(out) == StringView(u8"not open · ssh prod"));
        bookmarkStatus(folder, false, out);
        STD_INSIST(StringView(out) == StringView(u8"not open · ~/Projects/shitty"));
    }

    STD_TEST(TheShelfFindsByIdAndNothingElse) {
        BookmarkShelf shelf;
        shelf.items.pushBack(Bookmark{4, StringView(u8"a"), StringView(u8"x"), StringView()});
        shelf.items.pushBack(Bookmark{9, StringView(u8"b"), StringView(u8"y"), StringView()});
        STD_INSIST(shelf.indexOf(9) == 1);
        STD_INSIST(shelf.find(4) == &shelf.items[0]);
        STD_INSIST(shelf.find(0) == nullptr);
        STD_INSIST(shelf.indexOf(5) == 2);
    }
    // What pinning writes, the parser reads back as it was: quotes,
    // backslashes and a control character included.
    STD_TEST(AWrittenBlockReadsBackAsItWas) {
        const Bookmark odd{0, StringView(u8"say \"hi\" \\ there"), StringView(u8"printf 'a\tb'"), StringView(u8"/tmp/x y")};
        StringBuilder text;
        appendBookmark(StringView(), odd, text);
        Vector<Bookmark> out;
        u64 nextId = 1;
        parse(StringView(text), out, nextId);
        STD_INSIST(out.length() == 1);
        STD_INSIST(sameBookmark(out[0], odd));
        // An empty key is left out rather than written empty.
        const Bookmark folder{0, StringView(u8"f"), StringView(), StringView(u8"/srv")};
        StringBuilder block;
        bookmarkBlock(folder, block);
        STD_INSIST(StringView(block) == StringView(u8"[[bookmark]]\ntitle = \"f\"\ndir = \"/srv\"\n"));
    }

    // Appending keeps every byte already there, adds the missing newline
    // and one blank line, and nothing else.
    STD_TEST(AppendingKeepsTheFileAndAddsOneBlock) {
        const Bookmark b{0, StringView(u8"b"), StringView(u8"ssh b"), StringView()};
        StringBuilder out;
        appendBookmark(StringView(u8"# mine\n[[bookmark]]\ncommand = \"a\""), b, out);
        STD_INSIST(StringView(out) == StringView(u8"# mine\n[[bookmark]]\ncommand = \"a\"\n\n[[bookmark]]\ntitle = \"b\"\ncommand = \"ssh b\"\n"));
    }

    // Removing cuts the one block - the right one of two equal-looking
    // neighbours, found through the parser and not by text - and keeps
    // the comment that introduces the next block.
    STD_TEST(RemovingCutsOneBlockAndKeepsEverythingElse) {
        const StringView text(u8"# top\n"
                              "[[bookmark]]\ntitle = \"a\"\ncommand = \"x\"\n"
                              "\n"
                              "[[bookmark]]\ntitle = \"broken\"\ncommand = 3\n"
                              "\n"
                              "[[bookmark]]\ntitle = \"b\"\ncommand = \"x\"\n"
                              "# about c\n"
                              "[[bookmark]]\ntitle = \"c\"\ncommand = \"y\"\n");
        const Bookmark b{0, StringView(u8"b"), StringView(u8"x"), StringView()};
        StringBuilder out;
        STD_INSIST(removeBookmark(text, b, out));
        STD_INSIST(StringView(out) == StringView(u8"# top\n"
                                                 "[[bookmark]]\ntitle = \"a\"\ncommand = \"x\"\n"
                                                 "\n"
                                                 "[[bookmark]]\ntitle = \"broken\"\ncommand = 3\n"
                                                 "\n"
                                                 "# about c\n"
                                                 "[[bookmark]]\ntitle = \"c\"\ncommand = \"y\"\n"));
        const Bookmark missing{0, StringView(u8"zz"), StringView(u8"x"), StringView()};
        StringBuilder untouched;
        untouched << StringView(u8"keep");
        STD_INSIST(!removeBookmark(text, missing, untouched));
        STD_INSIST(StringView(untouched) == StringView(u8"keep"));
    }

    STD_TEST(ACommandLineIsQuotedOnlyWhereTheShellNeedsIt) {
        static const char ssh[] = "ssh\0-p\0" "2222\0user@prod.example";
        static const char vim[] = "vim\0my file\0it's\0";
        StringBuilder out;
        // sizeof counts the literal's own terminator: the last argument's.
        shellCommandLine(StringView((const u8*)(ssh), sizeof(ssh)), out);
        STD_INSIST(StringView(out) == StringView(u8"ssh -p 2222 user@prod.example"));
        shellCommandLine(StringView((const u8*)(vim), sizeof(vim)), out);
        STD_INSIST(StringView(out) == StringView(u8"vim 'my file' 'it'\\''s' ''"));
    }

    // Pin writes the file and the shelf, unpin takes it out of both, and
    // an entry that was there before keeps its id through both - a tab
    // opened from it has to go on naming it.
    STD_TEST(PinAndUnpinGoThroughTheFileAndKeepTheOtherIds) {
        StringBuilder dir;
        makeTempDir(dir);
        StringBuilder path;
        path << StringView(dir) << StringView(u8"/sub/bookmarks.toml");
        static ObjPool::Ref pool = ObjPool::fromMemory();
        BookmarkShelf shelf;
        shelf.path = pool->intern(StringView(path));

        // No file and no directory yet: pinning makes both.
        const Bookmark a{0, StringView(u8"a"), StringView(u8"ssh a"), StringView()};
        u64 first = 0;
        STD_INSIST(pinBookmark(shelf, *pool, StringView(), a, first));
        STD_INSIST(first != 0 && shelf.items.length() == 1 && shelf.items[0].id == first);

        const Bookmark b{0, StringView(u8"b"), StringView(), StringView(u8"/srv")};
        u64 second = 0;
        STD_INSIST(pinBookmark(shelf, *pool, StringView(), b, second));
        STD_INSIST(second != 0 && second != first);
        STD_INSIST(shelf.items.length() == 2);
        STD_INSIST(shelf.find(first) != nullptr && sameBookmark(*shelf.find(first), a));

        STD_INSIST(unpinBookmark(shelf, *pool, StringView(), first));
        STD_INSIST(shelf.items.length() == 1);
        STD_INSIST(shelf.items[0].id == second);
        Buffer written;
        readAll(StringView(path), written);
        STD_INSIST(StringView(written) == StringView(u8"[[bookmark]]\ntitle = \"b\"\ndir = \"/srv\"\n"));
        STD_INSIST(!unpinBookmark(shelf, *pool, StringView(), first));

        StringBuilder sub;
        sub << StringView(dir) << StringView(u8"/sub");
        Buffer file{StringView(path)};
        Buffer subBuf{StringView(sub)};
        Buffer dirBuf{StringView(dir)};
        STD_INSIST(unlink(file.cStr()) == 0);
        STD_INSIST(rmdir(subBuf.cStr()) == 0);
        STD_INSIST(rmdir(dirBuf.cStr()) == 0);
    }

    STD_TEST(NothingIsPinnedWithoutAFile) {
        static ObjPool::Ref pool = ObjPool::fromMemory();
        BookmarkShelf shelf;
        u64 id = 0;
        STD_INSIST(!pinBookmark(shelf, *pool, StringView(), Bookmark{0, StringView(u8"a"), StringView(u8"a"), StringView()}, id));
        STD_INSIST(shelf.items.length() == 0 && id == 0);
    }
}
