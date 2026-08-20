/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "ui_sidebar_tabs.h"

#include "composer.h"
#include "input_bindings.h"
#include "listener.h"
#include "options.h"

#include <plt/platform.h>
#include <plt/platform_headless.h>
#include <plt/window.h>

#include <std/lib/buffer.h>
#include <std/lib/list.h>
#include <std/mem/obj_pool.h>
#include <std/str/builder.h>
#include <std/str/view.h>
#include <std/tst/ut.h>

#include <fcntl.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace stl;

// createSidebarTabsUi() is macOS-only chrome and only enters the build
// on darwin (build.py); off it there is nothing to link against. The
// drawing itself is AppKit's and wants a human; what is testable here is
// the half that decides how much of the window the terminal gets - the
// half a mistake in is invisible until the text is already under the
// panel - and the two decisions V2 hoisted out of the AppKit half so
// they could be checked at all.
#if defined(__APPLE__)

// What a row shows instead of the raw window title, and the row a click
// lands in, both defined in ui_sidebar_tabs.mm. Declared here rather
// than in ui_sidebar_tabs.h because they are the module's own business
// and nobody else's; the pattern is F4's csdTabsChromeAlpha().
StringView sidebarTabsShortTitle(StringView title);
long long sidebarTabsRowAt(double panelHeight, double offsetFromTop, size_t count);

// The third line of a row, and the two pure halves of working it out.
// Declared here for the same reason: a decision reachable only through
// the filesystem is a decision no test pins down.
StringView sidebarTabsGitDirLink(StringView contents);
StringView sidebarTabsHeadBranch(StringView head);
bool sidebarTabsBranch(StringView directory, stl::Buffer& out);
bool sidebarTabsDirectory(pid_t pid, stl::Buffer& out);

namespace {
    // Everything AppKit in this module is deferred to the main queue,
    // which no unit test drains, and the deferred work is the only
    // thing that touches an NSWindow. Leaving composer.sessions null
    // keeps it that way: the projection returns before it schedules
    // anything, so nothing is left pointing at a pool this test is
    // about to drop.
    Composer& sidebarComposer(ObjPool& pool, Options& options) {
        Composer& composer = *pool.make<Composer>(&pool);
        options.border = 0;
        options.sidebarTabs = true;
        options.sidebarWidth = 200;
        composer.opts = &options;
        plt::Platform* const platform = plt::createHeadlessPlatform(pool);
        composer.window = platform->createWindow(pool, {});
        composer.setGlyphSize(8, 16);
        return composer;
    }

    // Mirrors quick_frame_store_ut.cpp's makeTempDir(): a mkdtemp()
    // directory this process owns, torn down by the caller.
    void makeTempDir(StringBuilder& dir) {
        const char* const directory = getenv("TMPDIR");
        dir << StringView(directory != nullptr ? directory : "/tmp") << StringView(u8"/ui_sidebar_tabs_ut.XXXXXX");
        STD_INSIST(mkdtemp(dir.cStr()) != nullptr);
    }

    void makeDir(StringView path) {
        Buffer buf{path};
        STD_INSIST(::mkdir(buf.cStr(), 0755) == 0);
    }

    void writeRawFile(StringView path, StringView content) {
        Buffer buf{path};
        const int fd = ::open(buf.cStr(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        STD_INSIST(fd >= 0);
        STD_INSIST(::write(fd, content.data(), content.length()) == (ssize_t)(content.length()));
        ::close(fd);
    }

    void removePath(StringView path) {
        Buffer buf{path};
        if (::unlink(buf.cStr()) != 0) {
            ::rmdir(buf.cStr());
        }
    }

    StringView branchOf(StringView directory, Buffer& out) {
        return sidebarTabsBranch(directory, out) ? StringView(out) : StringView();
    }

    void publish(IntrusiveList& listeners) {
        for (IntrusiveNode* node = listeners.mutFront(); node != listeners.mutEnd();) {
            Listener* const listener = static_cast<Listener*>(node);
            node = node->next;
            listener->onListen();
        }
    }

    bool pressCmdB(Composer& composer) {
        return composer.inputBindings->key({plt::InputKey::Printable, plt::InputAction::Press, plt::InputSuper, 0, 'b'});
    }
}

STD_TEST_SUITE(SidebarTabsUi) {
    // The reserve is in place before the first resize, not after it:
    // application.cpp builds this object right after the window and
    // before showWindow() sizes the grid, so a panel that only claimed
    // its width once something else asked would show one frame of text
    // underneath itself and then jump.
    STD_TEST(ReservesItsWidthBeforeTheGridIsFirstCounted) {
        auto pool = ObjPool::fromMemory();
        Options options;
        Composer& composer = sidebarComposer(*pool, options);

        createSidebarTabsUi(*pool, composer);

        STD_INSIST(composer.chromeReserve(ChromeSide::Left) == 200);
        STD_INSIST(composer.chromeReserve(ChromeSide::Top) == 0);
        STD_INSIST(composer.chromeReserve(ChromeSide::Bottom) == 0);
        STD_INSIST(composer.chromeReserve(ChromeSide::Right) == 0);

        composer.resize(1600, 800);

        // 200 points wide out of 1600 leaves 1400, which is 175 cells
        // of 8 - and the rows are untouched, the panel being on no
        // vertical edge. The left edge and nothing else: the title-bar
        // strip's own reserve is on top and stays there, and it is the
        // left one ui_csd_tabs.mm reads to know a tab list is up (V2).
        STD_INSIST(composer.columns == 175);
        STD_INSIST(composer.rows == 50);
        STD_INSIST(composer.contentInsets().left == 200);

        // The width is an option in points and stays one: a move to a
        // Retina display doubles what it costs the grid without anybody
        // re-applying anything.
        composer.setContentScale(2.0f);
        composer.resize(1600, 800);

        STD_INSIST(composer.contentInsets().left == 400);
        STD_INSIST(composer.columns == 150);
    }

    // V2's first complaint, and the one a test can pin down: the panel
    // is on the *left*. Every reserve above says so, and this says the
    // sides are not interchangeable - the grid's own left inset is what
    // moves, and the right one never does.
    STD_TEST(TheGridLosesItsLeftEdgeAndKeepsItsRight) {
        auto pool = ObjPool::fromMemory();
        Options options;
        Composer& composer = sidebarComposer(*pool, options);
        createSidebarTabsUi(*pool, composer);
        composer.resize(1600, 800);

        STD_INSIST(composer.contentInsets().left == 200);
        STD_INSIST(composer.contentInsets().right == 0);
        STD_INSIST(composer.contentInsets().top == 0);
        STD_INSIST(composer.contentInsets().bottom == 0);

        STD_INSIST(pressCmdB(composer));

        STD_INSIST(composer.contentInsets().left == 0);
        STD_INSIST(composer.contentInsets().right == 0);
    }

    // V2's third complaint, the readable half of it: a shell sets the
    // window title to the whole of "user@host:~/Projects/github.com/
    // shitty", and a 220pt column showed the user "...ects/github.com/
    // shitty". The last path component is what differs between tabs.
    STD_TEST(ARowShowsThePathsLastComponentAndLeavesACommandAlone) {
        STD_INSIST(sidebarTabsShortTitle(StringView(u8"kibomibo@KiboMBP:~/Projects/github.com/shitty")) == StringView(u8"shitty"));
        STD_INSIST(sidebarTabsShortTitle(StringView(u8"a/b")) == StringView(u8"b"));

        // No slash at all is a command line, or a bare host name, and
        // there is nothing to cut off it.
        STD_INSIST(sidebarTabsShortTitle(StringView(u8"vim")) == StringView(u8"vim"));
        STD_INSIST(sidebarTabsShortTitle(StringView(u8"npm run build")) == StringView(u8"npm run build"));

        // The edges: a trailing slash would leave an empty label, and an
        // empty title never reaches here (project() substitutes the
        // brand name first), but neither may walk off the end.
        STD_INSIST(sidebarTabsShortTitle(StringView(u8"~/Projects/")) == StringView(u8"~/Projects/"));
        STD_INSIST(sidebarTabsShortTitle(StringView(u8"/")) == StringView(u8"/"));
        STD_INSIST(sidebarTabsShortTitle(StringView()).length() == 0);
    }

    // A click lands in the row the eye sees, which is one function's
    // answer shared by drawRect: and mouseDown: - the two disagreeing is
    // the classic way a list like this selects the tab above the one
    // under the pointer. Three tabs, so row 3 is the new-tab row.
    STD_TEST(AClickLandsInTheRowThatWasDrawnThere) {
        // The panel starts with a gap, and a click in it selects nothing
        // rather than the first tab.
        STD_INSIST(sidebarTabsRowAt(800, 0, 3) == -1);
        STD_INSIST(sidebarTabsRowAt(800, 5, 3) == -1);

        STD_INSIST(sidebarTabsRowAt(800, 6, 3) == 0);
        STD_INSIST(sidebarTabsRowAt(800, 35, 3) == 0);
        STD_INSIST(sidebarTabsRowAt(800, 36, 3) == 1);
        STD_INSIST(sidebarTabsRowAt(800, 95, 3) == 2);

        // The new-tab row under the list, and bare panel below it.
        STD_INSIST(sidebarTabsRowAt(800, 96, 3) == 3);
        STD_INSIST(sidebarTabsRowAt(800, 126, 3) == -1);

        // A row that does not fit whole is drawn nowhere, so it answers
        // nothing either: at 76 points tall the panel shows rows 0 and 1
        // and half of row 2, and the half-row is not clickable.
        STD_INSIST(sidebarTabsRowAt(76, 40, 3) == 1);
        STD_INSIST(sidebarTabsRowAt(76, 70, 3) == -1);
    }

    // HEAD says which branch is out, and says it two different ways.
    STD_TEST(AHeadFileNamesTheBranchOrTheDetachedObject) {
        STD_INSIST(sidebarTabsHeadBranch(StringView(u8"ref: refs/heads/main\n")) == StringView(u8"main"));
        // Branch names carry slashes of their own, and the whole tail is
        // the name - cutting at the last slash would show "V2-sidebar-left"
        // for one branch and the same for a "wip/V2-sidebar-left".
        STD_INSIST(sidebarTabsHeadBranch(StringView(u8"ref: refs/heads/feat/V2-sidebar-left\n")) == StringView(u8"feat/V2-sidebar-left"));
        // Written without the trailing newline by some tools.
        STD_INSIST(sidebarTabsHeadBranch(StringView(u8"ref: refs/heads/main")) == StringView(u8"main"));

        // A detached head is the object id, abbreviated the way git
        // abbreviates it - a whole one would not fit the column.
        STD_INSIST(sidebarTabsHeadBranch(StringView(u8"0763f220e1b4c5d6a7f8091a2b3c4d5e6f708192\n")) == StringView(u8"0763f22"));

        // Neither shape: a corrupt or half-written HEAD reads as no
        // repository rather than as a row of garbage.
        STD_INSIST(sidebarTabsHeadBranch(StringView()).length() == 0);
        STD_INSIST(sidebarTabsHeadBranch(StringView(u8"\n")).length() == 0);
        STD_INSIST(sidebarTabsHeadBranch(StringView(u8"not a head at all\n")).length() == 0);
        // Too short to be an object id, hex or not.
        STD_INSIST(sidebarTabsHeadBranch(StringView(u8"abc123\n")).length() == 0);

        // A symbolic ref pointing somewhere other than refs/heads is
        // shown as written rather than guessed at.
        STD_INSIST(sidebarTabsHeadBranch(StringView(u8"ref: refs/tags/v1\n")) == StringView(u8"refs/tags/v1"));
    }

    // In a linked worktree .git is a file rather than a directory, and
    // this panel is being written inside one of half a dozen open on
    // this repository right now.
    STD_TEST(AWorktreesDotGitIsAFileThatPointsElsewhere) {
        STD_INSIST(sidebarTabsGitDirLink(StringView(u8"gitdir: /Users/x/p/.git/worktrees/v2\n")) == StringView(u8"/Users/x/p/.git/worktrees/v2"));
        // Submodules write it relative to the file that holds it.
        STD_INSIST(sidebarTabsGitDirLink(StringView(u8"gitdir: ../.git/modules/sub\n")) == StringView(u8"../.git/modules/sub"));
        // No newline, and a stray trailing space.
        STD_INSIST(sidebarTabsGitDirLink(StringView(u8"gitdir: /a/b ")) == StringView(u8"/a/b"));

        STD_INSIST(sidebarTabsGitDirLink(StringView()).length() == 0);
        STD_INSIST(sidebarTabsGitDirLink(StringView(u8"ref: refs/heads/main\n")).length() == 0);
    }

    // The whole lookup against a real filesystem: from a subdirectory,
    // through a worktree link, through a relative link, and out the far
    // side of a directory that is in no repository at all - which is a
    // different answer from every other one here and the only one the
    // row is allowed to call "no git".
    STD_TEST(TheBranchIsFoundFromASubdirectoryAndThroughEveryKindOfDotGit) {
        StringBuilder root;
        makeTempDir(root);

        StringBuilder repo;
        repo << StringView(root) << StringView(u8"/repo");
        makeDir(StringView(repo));
        StringBuilder gitDir;
        gitDir << StringView(repo) << StringView(u8"/.git");
        makeDir(StringView(gitDir));
        StringBuilder headPath;
        headPath << StringView(gitDir) << StringView(u8"/HEAD");
        writeRawFile(StringView(headPath), StringView(u8"ref: refs/heads/main\n"));

        Buffer out;

        // The repository root itself.
        STD_INSIST(branchOf(StringView(repo), out) == StringView(u8"main"));

        // And from a subdirectory two levels down - the walk up the tree
        // is the whole reason a tab sitting in lib/shitty finds anything.
        StringBuilder sub;
        sub << StringView(repo) << StringView(u8"/lib");
        makeDir(StringView(sub));
        StringBuilder deeper;
        deeper << StringView(sub) << StringView(u8"/shitty");
        makeDir(StringView(deeper));
        STD_INSIST(branchOf(StringView(deeper), out) == StringView(u8"main"));
        // A trailing slash is the same directory.
        StringBuilder deeperSlash;
        deeperSlash << StringView(deeper) << StringView(u8"/");
        STD_INSIST(branchOf(StringView(deeperSlash), out) == StringView(u8"main"));

        // A linked worktree: .git is a file naming an absolute gitdir,
        // and the branch comes from *there*, not from the repository the
        // link points into.
        StringBuilder worktrees;
        worktrees << StringView(gitDir) << StringView(u8"/worktrees");
        makeDir(StringView(worktrees));
        StringBuilder wtGitDir;
        wtGitDir << StringView(worktrees) << StringView(u8"/v2");
        makeDir(StringView(wtGitDir));
        StringBuilder wtHead;
        wtHead << StringView(wtGitDir) << StringView(u8"/HEAD");
        writeRawFile(StringView(wtHead), StringView(u8"ref: refs/heads/feat/V2-sidebar-left\n"));
        StringBuilder worktree;
        worktree << StringView(root) << StringView(u8"/v2");
        makeDir(StringView(worktree));
        StringBuilder wtLink;
        wtLink << StringView(worktree) << StringView(u8"/.git");
        StringBuilder wtLinkText;
        wtLinkText << StringView(u8"gitdir: ") << StringView(wtGitDir) << StringView(u8"\n");
        writeRawFile(StringView(wtLink), StringView(wtLinkText));

        STD_INSIST(branchOf(StringView(worktree), out) == StringView(u8"feat/V2-sidebar-left"));

        // A relative link, the shape a submodule writes: resolved
        // against the directory holding the file, not against this
        // process's own directory.
        StringBuilder module;
        module << StringView(root) << StringView(u8"/module");
        makeDir(StringView(module));
        StringBuilder moduleLink;
        moduleLink << StringView(module) << StringView(u8"/.git");
        writeRawFile(StringView(moduleLink), StringView(u8"gitdir: ../repo/.git/worktrees/v2\n"));

        STD_INSIST(branchOf(StringView(module), out) == StringView(u8"feat/V2-sidebar-left"));

        // A detached head in the same repository.
        writeRawFile(StringView(headPath), StringView(u8"0763f220e1b4c5d6a7f8091a2b3c4d5e6f708192\n"));
        STD_INSIST(branchOf(StringView(deeper), out) == StringView(u8"0763f22"));

        // No repository above the temp root at all. This is the only
        // answer the row may render as "no git", and it has to be
        // reachable or the row would say it about everything.
        STD_INSIST(!sidebarTabsBranch(StringView(root), out));

        // A relative directory is refused outright rather than resolved
        // against this process's own, which is not the tab's and would
        // answer with somebody else's branch. "." is the case that can
        // actually catch a missing guard: the test binary is built and
        // run inside a checkout, so without the guard this walks into
        // *this* repository and comes back with a branch. The assertion
        // holds either way - run the binary outside a repository and the
        // answer is still false - so it is never flaky, it just stops
        // being able to catch anything.
        STD_INSIST(!sidebarTabsBranch(StringView(u8"."), out));
        STD_INSIST(!sidebarTabsBranch(StringView(u8"lib/shitty"), out));
        STD_INSIST(!sidebarTabsBranch(StringView(), out));

        removePath(StringView(moduleLink));
        removePath(StringView(module));
        removePath(StringView(wtLink));
        removePath(StringView(worktree));
        removePath(StringView(wtHead));
        removePath(StringView(wtGitDir));
        removePath(StringView(worktrees));
        removePath(StringView(headPath));
        removePath(StringView(gitDir));
        removePath(StringView(deeper));
        removePath(StringView(sub));
        removePath(StringView(repo));
        removePath(StringView(root));
    }

    // The working directory comes from the shell process, not from the
    // shell's cooperation - OSC 7 is never sent to this terminal at all
    // (only Apple's own zshrc installs the hook, under its own terminal).
    // This process is the one whose directory is known for certain, so it
    // is the one the positive control uses.
    STD_TEST(TheDirectoryOfALiveProcessIsItsOwnWorkingDirectory) {
        Buffer out;
        STD_INSIST(sidebarTabsDirectory(getpid(), out));

        char expected[4096];
        STD_INSIST(getcwd(expected, sizeof(expected)) != nullptr);
        STD_INSIST(StringView(out) == StringView(expected));
    }

    // And the negative control the whole three-line row rests on: "no
    // directory to be had" has to be distinguishable from "a directory
    // with no repository in it", or the second line would go blank for
    // two different reasons and say which one neither time.
    STD_TEST(ADeadProcessHasNoDirectoryAndThatIsNotTheSameAsHavingNoRepository) {
        Buffer out;

        // A pid that certainly named a process and certainly does not
        // any more: forked, exited, reaped. Anything else either might
        // still be alive or might never have existed.
        const pid_t dead = fork();
        STD_INSIST(dead >= 0);
        if (dead == 0) {
            _exit(0);
        }
        int status = 0;
        STD_INSIST(waitpid(dead, &status, 0) == dead);

        STD_INSIST(!sidebarTabsDirectory(dead, out));
        STD_INSIST(out.used() == 0);

        // Not a pid at all.
        STD_INSIST(!sidebarTabsDirectory(0, out));
        STD_INSIST(!sidebarTabsDirectory(-1, out));

        // The other false, and the reason both exist: this process has a
        // directory, and whether that directory sits in a repository is
        // a second, separate question with its own answer. The two are
        // never the same call and never the same bool.
        STD_INSIST(sidebarTabsDirectory(getpid(), out));
        Buffer branch;
        const bool inRepository = sidebarTabsBranch(StringView(out), branch);
        // Either answer is correct here - the test binary may or may not
        // be run from inside a checkout - but the directory was found
        // regardless, which is the whole point being made.
        STD_INSIST(out.used() != 0);
        STD_INSIST(inRepository == (branch.used() != 0));
    }

    // The directory is *that* process's, not the caller's. A lookup that
    // quietly answered with our own working directory would pass every
    // assertion above - this process is a live process with a directory,
    // and the two happen to be the same one - and would put the same
    // folder on all six rows on a live window.
    STD_TEST(TheDirectoryBelongsToThatProcessAndNotToThisOne) {
        int ready[2] = {-1, -1};
        STD_INSIST(::pipe(ready) == 0);

        const pid_t child = fork();
        STD_INSIST(child >= 0);
        if (child == 0) {
            // The child outlives nothing, whatever the parent does. An
            // assertion below aborts this test without reaching the kill,
            // and a child left blocked would hold the inherited stdout
            // open - which hangs any caller reading this binary's output
            // through a pipe, and did.
            ::alarm(20);
            // Only async-signal-safe calls before _exit: this binary may
            // be running its suite on more than one thread.
            if (::chdir("/") != 0) {
                _exit(1);
            }
            const char byte = 'x';
            if (::write(ready[1], &byte, 1) != 1) {
                _exit(1);
            }
            ::pause();
            _exit(0);
        }
        ::close(ready[1]);
        char byte = 0;
        STD_INSIST(::read(ready[0], &byte, 1) == 1);

        // The child is sitting in "/", and this process certainly is not
        // - the suite runs out of the build tree.
        Buffer theirs;
        STD_INSIST(sidebarTabsDirectory(child, theirs));
        STD_INSIST(StringView(theirs) == StringView(u8"/"));

        Buffer ours;
        STD_INSIST(sidebarTabsDirectory(getpid(), ours));
        STD_INSIST(StringView(ours) != StringView(u8"/"));

        STD_INSIST(::kill(child, SIGKILL) == 0);
        int status = 0;
        STD_INSIST(waitpid(child, &status, 0) == child);
        ::close(ready[0]);
    }

    // cmd+b, and the one thing about it that is not like the hover strip
    // T6 builds: this legitimately re-counts the grid (A7). The columns
    // the panel held come back, and the resize that publishes is what
    // carries the new size to the shell.
    STD_TEST(CmdBGivesTheColumnsBackAndTakesThemAgain) {
        auto pool = ObjPool::fromMemory();
        Options options;
        Composer& composer = sidebarComposer(*pool, options);
        createSidebarTabsUi(*pool, composer);
        composer.resize(1600, 800);

        STD_INSIST(composer.columns == 175);

        STD_INSIST(pressCmdB(composer));

        STD_INSIST(composer.chromeReserve(ChromeSide::Left) == 0);
        STD_INSIST(composer.contentInsets().left == 0);
        STD_INSIST(composer.columns == 200);
        STD_INSIST(composer.rows == 50);
        // The window itself did not move; only the share of it the
        // terminal gets did.
        STD_INSIST(composer.pixelWidth == 1600);

        STD_INSIST(pressCmdB(composer));

        STD_INSIST(composer.chromeReserve(ChromeSide::Left) == 200);
        STD_INSIST(composer.columns == 175);
    }

    // Without -sidebarTabs the window is the one it always was: nothing
    // is reserved, cmd+b is not a way to make a panel appear that the
    // user never asked for, and the chord is not even consumed - it
    // goes on reaching the program inside the terminal, which under the
    // kitty keyboard protocol is told about it.
    STD_TEST(WithoutTheOptionNothingIsReservedAndCmdBIsNotEvenTaken) {
        auto pool = ObjPool::fromMemory();
        Options options;
        Composer& composer = sidebarComposer(*pool, options);
        options.sidebarTabs = false;

        createSidebarTabsUi(*pool, composer);
        composer.resize(1600, 800);

        STD_INSIST(composer.chromeReserve(ChromeSide::Left) == 0);
        STD_INSIST(composer.columns == 200);

        STD_INSIST(!pressCmdB(composer));

        STD_INSIST(composer.chromeReserve(ChromeSide::Left) == 0);
        STD_INSIST(composer.columns == 200);
    }

    // A reload that turns the option off has to hand the columns back
    // as well: a reserve nobody draws in is the grid paying rent for a
    // panel that is not there. The same path picks up a new width.
    STD_TEST(AReloadThatDropsTheOptionDropsTheReserve) {
        auto pool = ObjPool::fromMemory();
        Options options;
        Composer& composer = sidebarComposer(*pool, options);
        createSidebarTabsUi(*pool, composer);
        composer.resize(1600, 800);

        STD_INSIST(composer.columns == 175);

        options.sidebarTabs = false;
        publish(composer.configChangedListeners);

        STD_INSIST(composer.chromeReserve(ChromeSide::Left) == 0);
        STD_INSIST(composer.columns == 200);

        options.sidebarTabs = true;
        options.sidebarWidth = 400;
        publish(composer.configChangedListeners);

        STD_INSIST(composer.chromeReserve(ChromeSide::Left) == 400);
        STD_INSIST(composer.columns == 150);
    }
}

#endif
