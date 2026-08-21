/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "options.h"

#include "brand.h"

#include <std/mem/obj_pool.h>
#include <std/str/builder.h>
#include <std/sys/throw.h>
#include <std/tst/ut.h>

#include <cstring>
#include <stdlib.h>
#include <unistd.h>

using namespace stl;

namespace {

    // Creates a temp config file with the given TOML body and returns its
    // path, still owned by the StringBuilder so the caller can hand its
    // cStr() straight to argv. Mirrors the mkstemp() pattern already used
    // by std/sys/mem_fd.cpp. The caller must unlink() the path when done.
    void writeTempConfig(StringBuilder& path, StringView text) {
        const char* directory = getenv("TMPDIR");
        path << StringView(directory != nullptr ? directory : "/tmp") << StringView(u8"/options_ut_config.XXXXXX");
        const int fd = mkstemp(path.cStr());
        STD_INSIST(fd >= 0);
        const ssize_t written = ::write(fd, text.data(), text.length());
        STD_INSIST(written == (ssize_t)(text.length()));
        ::close(fd);
    }

}

STD_TEST_SUITE(Options) {
    STD_TEST(CreatedInstancesOwnTheirParsedLists) {
        auto pool = ObjPool::fromMemory();
        char program[] = "st";
        char config[] = "-config";
        char emptyConfig[] = "/dev/null";
        char font[] = "-font";
        char firstName[] = "first-font";
        char secondName[] = "second-font";
        char* firstArgv[] = {program, config, emptyConfig, font, firstName, nullptr};
        char* secondArgv[] = {program, config, emptyConfig, font, secondName, nullptr};

        Options* const first = Options::create(*pool, *Brand::generic(), firstArgv, 5);
        Options* const second = Options::create(*pool, *Brand::generic(), secondArgv, 5);

        STD_INSIST(first->fontnames.length() == 1);
        STD_INSIST(second->fontnames.length() == 1);
        STD_INSIST(first->fontnames[0] == StringView(u8"first-font"));
        STD_INSIST(second->fontnames[0] == StringView(u8"second-font"));
        STD_INSIST(firstArgv[1] == nullptr);
        STD_INSIST(secondArgv[1] == nullptr);
    }

    STD_TEST(QuickTransparentTitlebarAndHotkeyDefaultToDisabled) {
        auto pool = ObjPool::fromMemory();
        char program[] = "st";
        char config[] = "-config";
        char emptyConfig[] = "/dev/null";
        char* argv[] = {program, config, emptyConfig, nullptr};

        Options* const opts = Options::create(*pool, *Brand::generic(), argv, 3);

        STD_INSIST(!opts->quick);
        STD_INSIST(!opts->transparentTitlebar);
        STD_INSIST(opts->quickHotkey == StringView(u8"ctrl+grave"));
    }

    STD_TEST(CommandLineTogglesQuickAndTransparentTitlebar) {
        auto pool = ObjPool::fromMemory();
        char program[] = "st";
        char config[] = "-config";
        char emptyConfig[] = "/dev/null";
        char quickFlag[] = "-quick";
        char titlebarFlag[] = "-transparentTitlebar";
        char* argv[] = {program, config, emptyConfig, quickFlag, titlebarFlag, nullptr};

        Options* const opts = Options::create(*pool, *Brand::generic(), argv, 5);

        STD_INSIST(opts->quick);
        STD_INSIST(opts->transparentTitlebar);
        // Untouched by these two flags; still the hard default.
        STD_INSIST(opts->quickHotkey == StringView(u8"ctrl+grave"));
    }

    // -quickHotkey has no chord grammar of its own yet - T1 only checks
    // non-emptiness (options.cpp), the grammar itself is T3's job (plan
    // W3/T3). This documents that contract: garbage is accepted, not
    // rejected, at this layer.
    STD_TEST(CommandLineQuickHotkeyAcceptsAnyNonEmptyText) {
        auto pool = ObjPool::fromMemory();
        char program[] = "st";
        char config[] = "-config";
        char emptyConfig[] = "/dev/null";
        char hotkeyFlag[] = "-quickHotkey";
        char hotkeyValue[] = "not a real chord!!";
        char* argv[] = {program, config, emptyConfig, hotkeyFlag, hotkeyValue, nullptr};

        Options* const opts = Options::create(*pool, *Brand::generic(), argv, 5);

        STD_INSIST(opts->quickHotkey == StringView(u8"not a real chord!!"));
    }

    STD_TEST(EmptyCommandLineQuickHotkeyIsRejected) {
        auto pool = ObjPool::fromMemory();
        char program[] = "st";
        char config[] = "-config";
        char emptyConfig[] = "/dev/null";
        char hotkeyFlag[] = "-quickHotkey";
        char hotkeyValue[] = "";
        char* argv[] = {program, config, emptyConfig, hotkeyFlag, hotkeyValue, nullptr};

        bool threw = false;
        try {
            // Reload: Startup would call exit() on this error and take
            // the whole test binary down with it.
            Options::create(*pool, *Brand::generic(), argv, 5, OptionsLoad::Reload);
        } catch (Exception& error) {
            threw = true;
            STD_INSIST(error.description().search(StringView(u8"expected a non-empty chord")) != nullptr);
        }
        STD_INSIST(threw);
    }

    // V3: where the tab bar lives is a named placement, and the default
    // is the one this fork shipped with - the title bar. A default that
    // drifted here would move every existing user's tabs on upgrade
    // without anyone asking for it.
    STD_TEST(TabBarPlacementDefaultsToTheTitleBarAndTakesTwoNames) {
        auto pool = ObjPool::fromMemory();
        char program[] = "st";
        char config[] = "-config";
        char emptyConfig[] = "/dev/null";
        char flag[] = "-tabBar";

        {
            char* argv[] = {program, config, emptyConfig, nullptr};
            Options* const opts = Options::create(*pool, *Brand::generic(), argv, 3);
            STD_INSIST(!opts->sidebarTabs);
        }
        {
            char value[] = "top";
            char* argv[] = {program, config, emptyConfig, flag, value, nullptr};
            Options* const opts = Options::create(*pool, *Brand::generic(), argv, 5);
            STD_INSIST(!opts->sidebarTabs);
        }
        {
            char value[] = "sidebar";
            char* argv[] = {program, config, emptyConfig, flag, value, nullptr};
            Options* const opts = Options::create(*pool, *Brand::generic(), argv, 5);
            STD_INSIST(opts->sidebarTabs);
        }
    }

    // And a name that is neither is refused rather than quietly read as
    // one of them - a misspelt placement that silently meant "top" would
    // look exactly like the feature not working.
    STD_TEST(AnUnknownTabBarPlacementIsRejected) {
        auto pool = ObjPool::fromMemory();
        char program[] = "st";
        char config[] = "-config";
        char emptyConfig[] = "/dev/null";
        char flag[] = "-tabBar";
        char value[] = "left";
        char* argv[] = {program, config, emptyConfig, flag, value, nullptr};

        bool threw = false;
        try {
            // Reload: Startup would call exit() on this error and take
            // the whole test binary down with it.
            Options::create(*pool, *Brand::generic(), argv, 5, OptionsLoad::Reload);
        } catch (Exception& error) {
            threw = true;
            STD_INSIST(error.description().search(StringView(u8"expected top or sidebar")) != nullptr);
        }
        STD_INSIST(threw);
    }

    STD_TEST(ConfigFileSetsQuickTransparentTitlebarAndQuickHotkey) {
        auto pool = ObjPool::fromMemory();
        StringBuilder path;
        writeTempConfig(path, StringView(u8"quick = true\ntransparentTitlebar = true\nquickHotkey = \"cmd+space\"\n"));

        char program[] = "st";
        char configFlag[] = "-config";
        char* argv[] = {program, configFlag, path.cStr(), nullptr};

        Options* const opts = Options::create(*pool, *Brand::generic(), argv, 3);

        STD_INSIST(opts->quick);
        STD_INSIST(opts->transparentTitlebar);
        STD_INSIST(opts->quickHotkey == StringView(u8"cmd+space"));

        ::unlink(path.cStr());
    }

    // Priority order lives in OptionsParser::get() (options.cpp): the
    // command line is checked before the config file. '+quick' is an
    // explicit "false" on the command line and must beat a configured
    // "true".
    STD_TEST(CommandLineQuickBeatsConfiguredQuick) {
        auto pool = ObjPool::fromMemory();
        StringBuilder path;
        writeTempConfig(path, StringView(u8"quick = true\n"));

        char program[] = "st";
        char configFlag[] = "-config";
        char plusQuick[] = "+quick";
        char* argv[] = {program, configFlag, path.cStr(), plusQuick, nullptr};

        Options* const opts = Options::create(*pool, *Brand::generic(), argv, 4);

        STD_INSIST(!opts->quick);

        ::unlink(path.cStr());
    }

    // getBool() (options.cpp) only accepts the literal strings "true"
    // and "false"; a config value can carry anything else, unlike the
    // command line's NoArg flags which can only ever produce those two.
    STD_TEST(GarbageBooleanValueInConfigIsRejected) {
        auto pool = ObjPool::fromMemory();
        StringBuilder path;
        writeTempConfig(path, StringView(u8"quick = \"nope\"\n"));

        char program[] = "st";
        char configFlag[] = "-config";
        char* argv[] = {program, configFlag, path.cStr(), nullptr};

        bool threw = false;
        try {
            Options::create(*pool, *Brand::generic(), argv, 3, OptionsLoad::Reload);
        } catch (Exception& error) {
            threw = true;
            STD_INSIST(error.description().search(StringView(u8"-quick: expected true or false")) != nullptr);
        }
        STD_INSIST(threw);

        ::unlink(path.cStr());
    }

    // F9. The divider's two options. The width has a hard default in the
    // table; the colour deliberately has none, so that an unset one is
    // the scheme's rather than a constant - the same shape cr already
    // uses to default to fg.
    STD_TEST(TheDividerDefaultsToOnePixelOfTheSchemesBrightBlack) {
        auto pool = ObjPool::fromMemory();
        char program[] = "st";
        char config[] = "-config";
        char emptyConfig[] = "/dev/null";
        char* argv[] = {program, config, emptyConfig, nullptr};

        Options* const opts = Options::create(*pool, *Brand::generic(), argv, 3);

        // One pixel: the smallest thing that is still a line. Zero was
        // the old A10 default and is what left nothing to see.
        STD_INSIST(opts->paneDividerWidth == 1);
        // Derived, not constant. Asserted against the palette entry it is
        // taken from rather than against a literal colour, so a scheme
        // change moves the seam with it instead of failing here.
        STD_INSIST(opts->paneDividerColor.red == opts->palette[8].red);
        STD_INSIST(opts->paneDividerColor.green == opts->palette[8].green);
        STD_INSIST(opts->paneDividerColor.blue == opts->palette[8].blue);
        // And it is not simply the background, which is the answer a
        // seam that stayed invisible would give.
        const bool sameAsBackground = opts->paneDividerColor.red == opts->bg.red && opts->paneDividerColor.green == opts->bg.green && opts->paneDividerColor.blue == opts->bg.blue;
        STD_INSIST(!sameAsBackground);
    }

    STD_TEST(TheCommandLineSetsTheDividerWidthAndColour) {
        auto pool = ObjPool::fromMemory();
        char program[] = "st";
        char config[] = "-config";
        char emptyConfig[] = "/dev/null";
        char widthFlag[] = "-paneDividerWidth";
        char width[] = "6";
        char colourFlag[] = "-paneDividerColor";
        char colour[] = "#ff0000";
        char* argv[] = {program, config, emptyConfig, widthFlag, width, colourFlag, colour, nullptr};

        Options* const opts = Options::create(*pool, *Brand::generic(), argv, 7);

        STD_INSIST(opts->paneDividerWidth == 6);
        STD_INSIST(opts->paneDividerColor.red == 255);
        STD_INSIST(opts->paneDividerColor.green == 0);
        STD_INSIST(opts->paneDividerColor.blue == 0);
        // The given colour beat the scheme's, which is the whole point of
        // the option and the half a default-only test cannot see.
        STD_INSIST(opts->paneDividerColor.red != opts->palette[8].red || opts->paneDividerColor.green != opts->palette[8].green || opts->paneDividerColor.blue != opts->palette[8].blue);
    }

    STD_TEST(AGarbageDividerWidthIsRejected) {
        auto pool = ObjPool::fromMemory();
        char program[] = "st";
        char config[] = "-config";
        char emptyConfig[] = "/dev/null";
        char widthFlag[] = "-paneDividerWidth";
        char width[] = "wide";
        char* argv[] = {program, config, emptyConfig, widthFlag, width, nullptr};

        bool threw = false;
        try {
            // Reload, for the reason the hotkey test above gives: on this
            // error Startup calls exit() and takes the test binary with
            // it.
            Options::create(*pool, *Brand::generic(), argv, 5, OptionsLoad::Reload);
        } catch (Exception& error) {
            threw = true;
            STD_INSIST(error.description().search(StringView(u8"-paneDividerWidth")) != nullptr);
        }
        STD_INSIST(threw);
    }
}
