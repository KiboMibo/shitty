/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "quick_frame_store.h"

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
        writeRawFile(StringView(path), StringView(u8"x=100\ny=50\n"));

        QuickFrame out{.x = 7};
        STD_INSIST(!loadQuickFrame(StringView(path), out));
        STD_INSIST(out.x == 7);

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
