/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "quick_companion.h"

#include "quick_frame_store.h"

#include <std/str/builder.h>
#include <std/str/view.h>
#include <std/tst/ut.h>

#include <limits.h>
#include <stdlib.h>
#include <unistd.h>

using namespace stl;

namespace {

    // Mirrors options_ut.cpp's writeTempConfig(): an mkstemp() file this
    // process actually owns, so realpath() has something real to
    // canonicalize. Content is irrelevant here - only the path matters -
    // so the file is left empty.
    void makeTempFile(StringBuilder& path) {
        const char* const directory = getenv("TMPDIR");
        path << StringView(directory != nullptr ? directory : "/tmp") << StringView(u8"/quick_companion_ut.XXXXXX");
        const int fd = mkstemp(path.cStr());
        STD_INSIST(fd >= 0);
        close(fd);
    }

    // Rewrites ".../name" to "..././name": the same file under a
    // different, non-canonical spelling, the way "~/x" and its expanded
    // $HOME form are the same file under different spellings too.
    void withExtraDot(StringView original, StringBuilder& out) {
        size_t lastSlash = 0;
        for (size_t at = 0; at < original.length(); ++at) {
            if (original[at] == '/') {
                lastSlash = at;
            }
        }
        out << StringView(original.data(), lastSlash + 1) << StringView(u8"./") << StringView(original.data() + lastSlash + 1, original.length() - lastSlash - 1);
    }

}

STD_TEST_SUITE(QuickCompanion) {
    // mkstemp()'s own path is not necessarily its own canonical form -
    // TMPDIR routes through /var on macOS, itself a symlink to /private -
    // so the expected value is realpath()'d exactly like resolved is,
    // rather than compared against the raw mkstemp() text.
    STD_TEST(ExistingFileResolvesWithoutSelfReference) {
        StringBuilder file;
        makeTempFile(file);
        char expected[PATH_MAX];
        STD_INSIST(realpath(file.cStr(), expected) != nullptr);

        StringBuilder resolved;
        bool selfReference = true;
        STD_INSIST(resolveQuickCompanionConfig(StringView(file), StringView(), resolved, selfReference));
        STD_INSIST(!selfReference);
        STD_INSIST(StringView(resolved) == StringView(expected));

        unlink(file.cStr());
    }

    STD_TEST(MissingFileIsRejectedAndOutUntouched) {
        StringBuilder resolved;
        resolved << StringView(u8"untouched");
        bool selfReference = true;

        STD_INSIST(!resolveQuickCompanionConfig(StringView(u8"/nonexistent/quick_companion_ut_missing"), StringView(), resolved, selfReference));
        STD_INSIST(!selfReference);
        STD_INSIST(StringView(resolved) == StringView(u8"untouched"));
    }

    // The canonical-path comparison, not a string comparison: the raw
    // option text and ownConfigPath name the same file through two
    // different, non-normalized spellings.
    STD_TEST(SelfReferenceDetectedThroughDifferentSpelling) {
        StringBuilder file;
        makeTempFile(file);
        StringBuilder spelledDifferently;
        withExtraDot(StringView(file), spelledDifferently);

        StringBuilder resolved;
        bool selfReference = false;
        STD_INSIST(!resolveQuickCompanionConfig(StringView(spelledDifferently), StringView(file), resolved, selfReference));
        STD_INSIST(selfReference);
        STD_INSIST(resolved.used() == 0);

        unlink(file.cStr());
    }

    STD_TEST(DifferentFilesAreNotFlaggedAsSelfReference) {
        StringBuilder first;
        makeTempFile(first);
        StringBuilder second;
        makeTempFile(second);

        StringBuilder resolved;
        bool selfReference = true;
        STD_INSIST(resolveQuickCompanionConfig(StringView(first), StringView(second), resolved, selfReference));
        STD_INSIST(!selfReference);

        unlink(first.cStr());
        unlink(second.cStr());
    }

    STD_TEST(EmptyOwnConfigPathNeverSelfReferences) {
        StringBuilder file;
        makeTempFile(file);

        StringBuilder resolved;
        bool selfReference = true;
        STD_INSIST(resolveQuickCompanionConfig(StringView(file), StringView(), resolved, selfReference));
        STD_INSIST(!selfReference);

        unlink(file.cStr());
    }

    // Where this module meets quick_frame_store: quickRememberFrame
    // derives its state file from the config the process actually
    // resolved (defaultQuickFramePath), and a companion is this same
    // binary re-exec'd with a *different* -config. Two live processes
    // therefore keep two separate frames, and neither can overwrite the
    // other's - which is what makes "the companion remembers its own
    // placement" true rather than a race between two writers of one
    // file. The guarantee is not a convention either side has to
    // remember: the self-reference guard rejects the one configuration
    // that would collapse the two paths into one, so this pins both
    // halves together.
    STD_TEST(CompanionKeepsAFrameStoreSeparateFromItsParent) {
        StringBuilder own;
        makeTempFile(own);
        StringBuilder companion;
        makeTempFile(companion);

        StringBuilder resolved;
        bool selfReference = true;
        STD_INSIST(resolveQuickCompanionConfig(StringView(companion), StringView(own), resolved, selfReference));
        STD_INSIST(!selfReference);

        StringBuilder ownFrame;
        StringBuilder companionFrame;
        STD_INSIST(defaultQuickFramePath(StringView(own), ownFrame));
        STD_INSIST(defaultQuickFramePath(StringView(resolved), companionFrame));
        STD_INSIST(StringView(ownFrame) != StringView(companionFrame));

        // And the configuration that would have given them one shared
        // file is refused outright, rather than left to chance.
        StringBuilder shared;
        bool sharedSelfReference = false;
        STD_INSIST(!resolveQuickCompanionConfig(StringView(own), StringView(own), shared, sharedSelfReference));
        STD_INSIST(sharedSelfReference);

        unlink(own.cStr());
        unlink(companion.cStr());
    }
}
