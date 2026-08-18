/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "quick_frame_store.h"

#include <std/ios/fs_utils.h>
#include <std/lib/buffer.h>
#include <std/str/builder.h>
#include <std/str/view.h>
#include <std/tst/ut.h>

#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace stl;

namespace {

    // Mirrors quick_companion_ut.cpp's makeTempFile(): a mkdtemp()
    // directory this process actually owns, torn down by the caller with
    // rmdir() plus whatever files it created inside.
    void makeTempDir(StringBuilder& dir) {
        const char* const directory = getenv("TMPDIR");
        dir << StringView(directory != nullptr ? directory : "/tmp") << StringView(u8"/quick_frame_store_ut.XXXXXX");
        STD_INSIST(mkdtemp(dir.cStr()) != nullptr);
    }

    void writeRawFile(StringView path, StringView content) {
        Buffer pathBuf{path};
        const int fd = ::open(pathBuf.cStr(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        STD_INSIST(fd >= 0);
        STD_INSIST(::write(fd, content.data(), content.length()) == (ssize_t)(content.length()));
        ::close(fd);
    }
}

STD_TEST_SUITE(QuickFrameStore) {
    STD_TEST(RoundTripWritesAndReadsAllFourFields) {
        StringBuilder dir;
        makeTempDir(dir);
        StringBuilder path;
        path << StringView(dir) << StringView(u8"/frame");

        const QuickFrame written{.x = 100, .y = 50, .width = 800, .height = 600};
        STD_INSIST(saveQuickFrame(StringView(path), written));

        QuickFrame read;
        STD_INSIST(loadQuickFrame(StringView(path), read));
        STD_INSIST(read.x == 100);
        STD_INSIST(read.y == 50);
        STD_INSIST(read.width == 800);
        STD_INSIST(read.height == 600);

        unlink(path.cStr());
        rmdir(dir.cStr());
    }

    // Off-screen and otherwise nonsensical values are not this module's
    // problem: it stores and returns exactly what it is given, negative
    // origin included. Clamping against a live screen is T3's job.
    STD_TEST(OffScreenAndOversizedValuesRoundTripUnchanged) {
        StringBuilder dir;
        makeTempDir(dir);
        StringBuilder path;
        path << StringView(dir) << StringView(u8"/frame");

        const QuickFrame written{.x = -12000, .y = -9000, .width = 999999, .height = 1};
        STD_INSIST(saveQuickFrame(StringView(path), written));

        QuickFrame read;
        STD_INSIST(loadQuickFrame(StringView(path), read));
        STD_INSIST(read.x == -12000);
        STD_INSIST(read.y == -9000);
        STD_INSIST(read.width == 999999);
        STD_INSIST(read.height == 1);

        unlink(path.cStr());
        rmdir(dir.cStr());
    }

    STD_TEST(MissingFileIsTreatedAsAbsent) {
        QuickFrame out{.x = 7};

        STD_INSIST(!loadQuickFrame(StringView(u8"/nonexistent/quick_frame_store_ut_missing"), out));
        STD_INSIST(out.x == 7);
    }

    STD_TEST(MissingDirectoryIsTreatedAsAbsent) {
        QuickFrame out{.x = 7};

        STD_INSIST(!loadQuickFrame(StringView(u8"/nonexistent/directory/frame"), out));
        STD_INSIST(out.x == 7);
    }

    STD_TEST(CorruptFileIsTreatedAsAbsent) {
        StringBuilder dir;
        makeTempDir(dir);
        StringBuilder path;
        path << StringView(dir) << StringView(u8"/frame");
        writeRawFile(StringView(path), StringView(u8"\x01\x02not a frame at all\xff\xfe"));

        QuickFrame out{.x = 7};
        STD_INSIST(!loadQuickFrame(StringView(path), out));
        STD_INSIST(out.x == 7);

        unlink(path.cStr());
        rmdir(dir.cStr());
    }

    // A file missing even one of the four fields is rejected wholesale -
    // no partial application of a half-written or hand-edited frame.
    STD_TEST(PartialFileIsTreatedAsAbsent) {
        StringBuilder dir;
        makeTempDir(dir);
        StringBuilder path;
        path << StringView(dir) << StringView(u8"/frame");
        writeRawFile(StringView(path), StringView(u8"x-points=100\ny-points=50\n"));

        QuickFrame out{.x = 7};
        STD_INSIST(!loadQuickFrame(StringView(path), out));
        STD_INSIST(out.x == 7);

        unlink(path.cStr());
        rmdir(dir.cStr());
    }

    // R2-qa round 2, B4: the previous generation stored sizes in backing
    // pixels under bare "width"/"height" keys, which meant a different
    // window on every display. Its units cannot be recovered without the
    // display it was written on, so it is retired rather than migrated -
    // and retired through the mechanism that already exists, by naming
    // no key this parser knows.
    STD_TEST(PreviousGenerationFileIsTreatedAsAbsent) {
        StringBuilder dir;
        makeTempDir(dir);
        StringBuilder path;
        path << StringView(dir) << StringView(u8"/frame");
        writeRawFile(StringView(path), StringView(u8"x=100\ny=50\nwidth=800\nheight=600\n"));

        QuickFrame out{.x = 7};
        STD_INSIST(!loadQuickFrame(StringView(path), out));
        STD_INSIST(out.x == 7);

        unlink(path.cStr());
        rmdir(dir.cStr());
    }

    // The unit is part of the key, not a convention someone has to
    // remember: this is what keeps a future rename from quietly reviving
    // the pixel-based generation under a name this parser accepts.
    STD_TEST(SavedFileNamesItsUnitInEveryKey) {
        StringBuilder dir;
        makeTempDir(dir);
        StringBuilder path;
        path << StringView(dir) << StringView(u8"/frame");

        STD_INSIST(saveQuickFrame(StringView(path), {.x = 1, .y = 2, .width = 3, .height = 4}));

        Buffer pathBuf{StringView(path)};
        Buffer text;
        readFileContent(pathBuf, text);
        STD_INSIST(StringView(text) == StringView(u8"x-points=1\ny-points=2\nwidth-points=3\nheight-points=4\n"));

        unlink(path.cStr());
        rmdir(dir.cStr());
    }

    // Proof by substitution, not reasoning: making the directory
    // unwritable mid-sequence forces the write to fail before rename()
    // ever runs, and the test checks the filesystem afterwards rather
    // than trusting that rename() alone would have been enough.
    STD_TEST(FailedWriteLeavesThePreviousFrameIntactAndNoTempFileBehind) {
        StringBuilder dir;
        makeTempDir(dir);
        StringBuilder path;
        path << StringView(dir) << StringView(u8"/frame");

        const QuickFrame original{.x = 1, .y = 2, .width = 3, .height = 4};
        STD_INSIST(saveQuickFrame(StringView(path), original));

        STD_INSIST(chmod(dir.cStr(), 0500) == 0);
        const QuickFrame attempted{.x = 9, .y = 9, .width = 9, .height = 9};
        const bool saved = saveQuickFrame(StringView(path), attempted);
        chmod(dir.cStr(), 0700);
        STD_INSIST(!saved);

        QuickFrame read;
        STD_INSIST(loadQuickFrame(StringView(path), read));
        STD_INSIST(read.x == 1);
        STD_INSIST(read.y == 2);
        STD_INSIST(read.width == 3);
        STD_INSIST(read.height == 4);

        StringBuilder tmpPath;
        tmpPath << StringView(path) << StringView(u8".tmp.") << (i64)(getpid());
        STD_INSIST(access(tmpPath.cStr(), F_OK) != 0);

        unlink(path.cStr());
        rmdir(dir.cStr());
    }

    STD_TEST(DefaultPathReplacesTheConfigExtensionWithTheSuffix) {
        StringBuilder out;

        STD_INSIST(defaultQuickFramePath(StringView(u8"/home/x/.config/shitty/shitty.toml"), out));
        STD_INSIST(StringView(out) == StringView(u8"/home/x/.config/shitty/shitty-quick-frame"));
    }

    STD_TEST(DefaultPathWithoutAnExtensionAppendsTheSuffix) {
        StringBuilder out;

        STD_INSIST(defaultQuickFramePath(StringView(u8"/home/x/.config/shitty/shitty"), out));
        STD_INSIST(StringView(out) == StringView(u8"/home/x/.config/shitty/shitty-quick-frame"));
    }

    // A dot in a directory component must not be mistaken for the file's
    // own extension - only the last '.' after the last '/' counts.
    STD_TEST(DefaultPathIgnoresDotsInDirectoryComponents) {
        StringBuilder out;

        STD_INSIST(defaultQuickFramePath(StringView(u8"/home/x.y/.config/shitty/shitty.toml"), out));
        STD_INSIST(StringView(out) == StringView(u8"/home/x.y/.config/shitty/shitty-quick-frame"));
    }

    STD_TEST(EmptyConfigPathIsRejected) {
        StringBuilder out;
        out << StringView(u8"untouched");

        STD_INSIST(!defaultQuickFramePath(StringView(), out));
        STD_INSIST(StringView(out) == StringView(u8"untouched"));
    }
}

// R2-qa round 2, I8: the clamp used to exist twice - once portable and
// tested, once inside applyQuickFrameToWindow() over a live NSWindow and
// tested by nobody, which is where B4 lived. quickFrameTarget() is now
// the only implementation, and this is where it is pinned down: no
// window, no screen, just the arithmetic both callers run.
STD_TEST_SUITE(QuickFrameTargetResolution) {
    STD_TEST(TargetLeavesAFittingFrameAlone) {
        const QuickFrameRect target = quickFrameTarget({.x = 100, .y = 50, .width = 640, .height = 480}, {.x = 0, .y = 0, .width = 1920, .height = 1080}, 32);

        STD_INSIST(target.x == 100);
        STD_INSIST(target.y == 50);
        STD_INSIST(target.width == 640);
        // The saved size is the content's; the frame around it is a
        // titlebar taller.
        STD_INSIST(target.height == 512);
    }

    // R2-qa round 2, B4, with the numbers it was measured with: a
    // 1000x468-point content frame on the user's built-in 2x panel,
    // whose visibleFrame sits at (1015, -1117) in the global point
    // space. Resolved against the screen it was saved on, it comes back
    // untouched - no doubling, no clamp, nothing to write back.
    STD_TEST(TargetKeepsAFrameSavedOnASecondaryScreenExactly) {
        const QuickFrameRect target = quickFrameTarget({.x = 1300, .y = -820, .width = 1000, .height = 468}, {.x = 1015, .y = -1117, .width = 1728, .height = 1085}, 32);

        STD_INSIST(target.x == 1300);
        STD_INSIST(target.y == -820);
        STD_INSIST(target.width == 1000);
        STD_INSIST(target.height == 500);
    }

    // The other half of B4: the same saved frame resolved against the
    // wrong screen - the 1x external monitor the pointer happened to be
    // on. It is pulled onto that screen at its saved size rather than
    // reinterpreted at that screen's scale, which is what the old code
    // did (2000x968 instead of 1000x500). Landing on the wrong screen at
    // all is what applyQuickFrameToWindow()'s screen lookup prevents;
    // this pins down that the arithmetic here does not make it worse.
    STD_TEST(TargetPullsAFrameFromAnotherScreenOntoThisOne) {
        const QuickFrameRect target = quickFrameTarget({.x = 1300, .y = -820, .width = 1000, .height = 468}, {.x = 0, .y = 0, .width = 3840, .height = 1050}, 32);

        STD_INSIST(target.y == 0);
        STD_INSIST(target.width == 1000);
        STD_INSIST(target.height == 500);
    }

    // R2-qa round 2, Z2: it is the whole frame that has to fit the
    // screen, titlebar included. Clamping the content first and adding
    // the titlebar afterwards produced a 1112-point frame inside a
    // 1080-point screen, which AppKit then parked against the bottom
    // edge with its titlebar sticking out above the visible area.
    STD_TEST(TargetClampsTheFrameRatherThanTheContentAlone) {
        const QuickFrameRect target = quickFrameTarget({.x = 0, .y = 500, .width = 1000, .height = 1080}, {.x = 0, .y = 0, .width = 1920, .height = 1080}, 32);

        STD_INSIST(target.height == 1080);
        STD_INSIST(target.y == 0);
        STD_INSIST(target.y + target.height == 1080);
    }

    // A frame far outside the screen still lands on it, at the corner
    // nearest to where it was asked to be - the behavior A6 specified
    // and the four ToggleQuickWindow tests exercise end to end.
    STD_TEST(TargetPullsAnOffScreenFrameBackOntoTheScreen) {
        const QuickFrameRect target = quickFrameTarget({.x = 5000, .y = -500, .width = 5000, .height = 5000}, {.x = 0, .y = 0, .width = 1920, .height = 1080}, 0);

        STD_INSIST(target.x == 0);
        STD_INSIST(target.y == 0);
        STD_INSIST(target.width == 1920);
        STD_INSIST(target.height == 1080);
    }
}
