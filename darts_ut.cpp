/*
 * Copyright (C) 2026 Shitty team
 * MIT licensed
 * See the file LICENSE.MIT for the full license.
 */

#include "darts.h"

#include <std/tst/ut.h>

using namespace stl;

STD_TEST_SUITE(Darts) {
    STD_TEST(ExactMatch) {
        static const StringView keys[] = {
            StringView(u8"font"),
            StringView(u8"fontsize"),
            StringView(u8"fg"),
        };

        Darts trie;

        trie.build(keys, 3);
        STD_INSIST(trie.find(StringView(u8"font")) == 0);
        STD_INSIST(trie.find(StringView(u8"fontsize")) == 1);
        STD_INSIST(trie.find(StringView(u8"fg")) == 2);
        STD_INSIST(trie.find(StringView(u8"fon")) == Darts::missing);
        STD_INSIST(trie.find(StringView(u8"fontsizes")) == Darts::missing);
        STD_INSIST(trie.find(StringView(u8"x")) == Darts::missing);
        STD_INSIST(trie.find(StringView()) == Darts::missing);
    }

    STD_TEST(PrefixResolution) {
        static const StringView keys[] = {
            StringView(u8"font"),
            StringView(u8"fontsize"),
            StringView(u8"fg"),
            StringView(u8"verbose"),
            StringView(u8"version"),
            StringView(u8"v"),
        };

        Darts trie;

        trie.build(keys, 6);
        STD_INSIST(trie.resolve(StringView(u8"font")) == 0);
        STD_INSIST(trie.resolve(StringView(u8"fonts")) == 1);
        STD_INSIST(trie.resolve(StringView(u8"f")) == Darts::ambiguous);
        STD_INSIST(trie.resolve(StringView(u8"fg")) == 2);
        STD_INSIST(trie.resolve(StringView(u8"v")) == 5);
        STD_INSIST(trie.resolve(StringView(u8"ve")) == Darts::ambiguous);
        STD_INSIST(trie.resolve(StringView(u8"verb")) == 3);
        STD_INSIST(trie.resolve(StringView(u8"vers")) == 4);
        STD_INSIST(trie.resolve(StringView(u8"x")) == Darts::missing);
        STD_INSIST(trie.resolve(StringView(u8"fontsizes")) == Darts::missing);
        STD_INSIST(trie.resolve(StringView()) == Darts::ambiguous);
    }

    STD_TEST(EmptyTrie) {
        Darts trie;

        STD_INSIST(trie.find(StringView(u8"a")) == Darts::missing);
        STD_INSIST(trie.resolve(StringView(u8"a")) == Darts::missing);
        trie.build(nullptr, 0);
        STD_INSIST(trie.find(StringView()) == Darts::missing);
        STD_INSIST(trie.resolve(StringView(u8"a")) == Darts::missing);
    }

    STD_TEST(EmptyKey) {
        static const StringView keys[] = {
            StringView(),
            StringView(u8"a"),
        };

        Darts trie;

        trie.build(keys, 2);
        STD_INSIST(trie.find(StringView()) == 0);
        STD_INSIST(trie.find(StringView(u8"a")) == 1);
        STD_INSIST(trie.resolve(StringView()) == 0);
        STD_INSIST(trie.resolve(StringView(u8"a")) == 1);
    }
}
