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
    // R2-test, Z5: this file is one a user edits by hand, and a key with
    // spaces around it used to lose its line silently - after which the
    // file parsed as incomplete and was discarded whole.
    STD_TEST(SpacesAroundTheSeparatorDoNotSpoilTheFile) {
        StringBuilder dir;
        makeTempDir(dir);
        StringBuilder path;
        path << StringView(dir) << StringView(u8"/frame");
        writeRawFile(StringView(path), StringView(u8"x-points = 10\n  y-points =  20  \nwidth-points= 30\nheight-points =40\n"));

        QuickFrame read;
        STD_INSIST(loadQuickFrame(StringView(path), read));
        STD_INSIST(read.x == 10);
        STD_INSIST(read.y == 20);
        STD_INSIST(read.width == 30);
        STD_INSIST(read.height == 40);

        unlink(path.cStr());
        rmdir(dir.cStr());
    }

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

// R2-qa round 3, B6 and B7: which frames get a screen picked for them at
// all, and which results are the user's to keep. Both defects were pure
// arithmetic wearing an NSScreen costume - a screen chosen by one corner,
// and a flag that only one branch could ever clear - so this is where
// they are pinned down: no window, no screen, no notification.
STD_TEST_SUITE(QuickFrameScreenCoverage) {
    STD_TEST(OverlapOfSeparateRectanglesIsZero) {
        STD_INSIST(quickFrameOverlap({.x = 0, .y = 0, .width = 100, .height = 100}, {.x = 200, .y = 0, .width = 100, .height = 100}) == 0);
        // Touching along an edge is not overlapping either.
        STD_INSIST(quickFrameOverlap({.x = 0, .y = 0, .width = 100, .height = 100}, {.x = 100, .y = 0, .width = 100, .height = 100}) == 0);
    }

    STD_TEST(OverlapIsTheAreaOfTheSharedPart) {
        STD_INSIST(quickFrameOverlap({.x = 0, .y = 0, .width = 100, .height = 100}, {.x = 50, .y = 25, .width = 100, .height = 100}) == 50 * 75);
    }

    STD_TEST(AFrameWithinOneScreenFits) {
        const QuickFrameRect screens[] = {{.x = 0, .y = 0, .width = 1920, .height = 1080}};

        STD_INSIST(quickFrameFitsScreens({.x = 100, .y = 50, .width = 640, .height = 512}, screens, 1));
    }

    // B7, with the arrangement and the frame it was measured on: a
    // built-in 2x panel below and to the right of a 1x external monitor,
    // and a window the user dragged across the seam - its lower half on
    // the panel, its upper half on the monitor. Every point of it is on a
    // display, so nothing gets to move it. Picking either screen and
    // clamping into that one dragged the whole window off the other and
    // then wrote the result over the saved frame.
    STD_TEST(AFrameStraddlingTwoScreensFits) {
        const QuickFrameRect screens[] = {
            {.x = 0, .y = 0, .width = 3840, .height = 1080},
            {.x = 1015, .y = -1117, .width = 1728, .height = 1117},
        };
        const QuickFrameRect straddling{.x = 1300, .y = -100, .width = 1000, .height = 600};

        STD_INSIST(quickFrameFitsScreens(straddling, screens, 2));
        // What the old origin-in-screen lookup did instead: the origin is
        // on the panel, so the frame was clamped into the panel's visible
        // area and left it 520 points from where the user put it.
        const QuickFrameRect clamped = quickFrameTarget({.x = 1300, .y = -100, .width = 568, .height = 568}, {.x = 1015, .y = -1117, .width = 1728, .height = 1085}, 32);
        STD_INSIST(clamped.y != straddling.y);
    }

    // The same seam seen from one display: a frame flush against the top
    // of the screen reaches into the strip the menu bar occupies, which
    // visibleFrame excludes and frame does not. Reaching into a menu-bar
    // strip is what a straddling frame does by construction, and it is
    // not a reason to move the window.
    STD_TEST(AFrameReachingIntoTheMenuBarStripFits) {
        const QuickFrameRect screens[] = {{.x = 0, .y = 0, .width = 1728, .height = 1117}};

        STD_INSIST(quickFrameFitsScreens({.x = 200, .y = 685, .width = 800, .height = 432}, screens, 1));
    }

    // B4: the display the frame was saved on is not attached any more, so
    // part of it - all of it, here - is nowhere. This is the one case
    // that does need a screen picked for it and a clamp applied.
    STD_TEST(AFrameOnNoAttachedDisplayDoesNotFit) {
        const QuickFrameRect screens[] = {
            {.x = 0, .y = 0, .width = 3840, .height = 1080},
            {.x = 1015, .y = -1117, .width = 1728, .height = 1117},
        };

        STD_INSIST(!quickFrameFitsScreens({.x = -9000, .y = -9000, .width = 900, .height = 432}, screens, 2));
    }

    STD_TEST(AFrameHangingOffTheOnlyScreenDoesNotFit) {
        const QuickFrameRect screens[] = {{.x = 0, .y = 0, .width = 1920, .height = 1080}};

        STD_INSIST(!quickFrameFitsScreens({.x = 1900, .y = 100, .width = 640, .height = 512}, screens, 1));
    }

    // Mirrored displays are the one arrangement that reports the same
    // rect twice. Adding both shares would call a window half off the
    // display fully covered and hand it back unclamped.
    STD_TEST(MirroredScreensAreNotCountedTwice) {
        const QuickFrameRect screens[] = {
            {.x = 0, .y = 0, .width = 1000, .height = 1000},
            {.x = 0, .y = 0, .width = 1000, .height = 1000},
        };

        STD_INSIST(!quickFrameFitsScreens({.x = 500, .y = 0, .width = 1000, .height = 1000}, screens, 2));
    }

    STD_TEST(NothingFitsOnNoScreensAndNothingIsAFrame) {
        STD_INSIST(!quickFrameFitsScreens({.x = 0, .y = 0, .width = 640, .height = 480}, nullptr, 0));

        const QuickFrameRect screens[] = {{.x = 0, .y = 0, .width = 1920, .height = 1080}};
        STD_INSIST(!quickFrameFitsScreens({.x = 0, .y = 0, .width = 0, .height = 480}, screens, 1));
    }
}

// R2-qa round 3, B6: the half of the write-back guard that decides
// whether a hide persists anything. It used to be a flag that only a
// later restore could clear, which is how one show onto a guessed screen
// turned quickRememberFrame off until the process was restarted.
STD_TEST_SUITE(QuickFrameWriteBack) {
    STD_TEST(SavesAFrameThisShowDidNotComputeAtAll) {
        STD_INSIST(quickFrameShouldSave(false, {}, {.x = 400, .y = 300, .width = 1000, .height = 532}));
    }

    STD_TEST(SkipsTheVeryFrameThisShowComputed) {
        const QuickFrameRect computed{.x = 0, .y = 0, .width = 1728, .height = 1085};

        STD_INSIST(!quickFrameShouldSave(true, computed, computed));
    }

    // The way out of B6's dead end, and the reason the guard compares
    // frames instead of trusting the flag: a clamped show does not
    // persist itself, but the moment the user drags the window the frame
    // stops matching and their placement is saved again.
    STD_TEST(SavesOnceTheUserMovesAComputedFrame) {
        const QuickFrameRect computed{.x = 0, .y = 0, .width = 1728, .height = 1085};

        STD_INSIST(quickFrameShouldSave(true, computed, {.x = 400, .y = 300, .width = 1728, .height = 1085}));
    }

    STD_TEST(SavesOnceTheUserResizesAComputedFrame) {
        const QuickFrameRect computed{.x = 0, .y = 0, .width = 1728, .height = 1085};

        STD_INSIST(quickFrameShouldSave(true, computed, {.x = 0, .y = 0, .width = 1000, .height = 532}));
    }
}

// R2-test, I13: the grid recompute F2c added after a cross-scale restore.
// It measured the defect live - a restored 1000x500 coming back 980x490 -
// but the code sits in a branch no headless test reaches, so a mutation
// setting the ratio to 1 killed nothing. The arithmetic is out here now.
STD_TEST_SUITE(QuickFrameRegrid) {
    STD_TEST(SameScaleLeavesTheExtentAlone) {
        STD_INSIST(quickFrameRegridExtent(1000, 2.0f, 2.0f) == 1000);
        STD_INSIST(quickFrameRegridExtent(1000, 1.0f, 1.0f) == 1000);
    }

    // The window came back on the 2x panel while the composer still
    // carries the 1x monitor it was shown on: half as many points.
    STD_TEST(AWindowRestoredOnAFinerScreenShrinksInComposerTerms) {
        STD_INSIST(quickFrameRegridExtent(1000, 2.0f, 1.0f) == 500);
    }

    STD_TEST(AWindowRestoredOnACoarserScreenGrowsInComposerTerms) {
        STD_INSIST(quickFrameRegridExtent(1000, 1.0f, 2.0f) == 2000);
    }

    // WindowInfo hands back 0 when it has no scale to report, and
    // dividing by it is the one arithmetic accident here.
    STD_TEST(AnAbsentRestoredScaleIsTreatedAsOne) {
        STD_INSIST(quickFrameRegridExtent(1000, 0.0f, 2.0f) == 2000);
        STD_INSIST(quickFrameRegridExtent(1000, -1.0f, 1.0f) == 1000);
    }

    // Composer::resize takes a u16.
    STD_TEST(AnOversizedExtentIsClampedToWhatResizeTakes) {
        STD_INSIST(quickFrameRegridExtent(60000, 1.0f, 4.0f) == 65535);
    }
}
