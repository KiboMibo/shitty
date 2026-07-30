# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

import unittest

from harness import Shitty


class GhosttyGraphemeTest(unittest.TestCase):
    def assert_streaming_grapheme(self, text, width):
        with Shitty(columns=20, rows=2) as terminal:
            terminal.write_chunks(*(character.encode() for character in text))
            snapshot = terminal.model_snapshot()
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (width, 0))
            self.assertEqual(
                snapshot.cell(0, 0).grapheme,
                tuple(map(ord, text)),
            )
            self.assertEqual(snapshot.cell(0, 0).double_width, width == 2)
            if width == 2:
                self.assertTrue(
                    snapshot.cell(1, 0).double_width_continuation
                )

    def test_zwj_sequences_are_one_wide_streaming_grapheme(self):
        for text in ("👨‍👩‍👧", "🏴‍☠️"):
            with self.subTest(text=text):
                self.assert_streaming_grapheme(text, 2)

    def test_variation_selectors_change_streaming_cluster_width(self):
        self.assert_streaming_grapheme("#︎", 1)
        self.assert_streaming_grapheme("#️", 2)

    def test_valid_emoji_modifier_stays_with_its_base(self):
        self.assert_streaming_grapheme("👋🏿", 2)

    def test_emoji_modifier_after_non_emoji_follows_unicode_17(self):
        # Current UAX #29 classifies Emoji_Modifier as Extend, so GB9 keeps it
        # with the preceding quote. Ghostty retains the pre-Unicode-11 GB10
        # exception and is the outlier here; Kitty and utf8proc follow GB9.
        with Shitty(columns=8, rows=2) as terminal:
            terminal.write_chunks(*("\"🏿\""[index].encode() for index in range(3)))
            snapshot = terminal.model_snapshot()
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (3, 0))
            self.assertEqual(
                snapshot.cell(0, 0).grapheme,
                (ord('"'), 0x1F3FF),
            )
            self.assertTrue(snapshot.cell(0, 0).double_width)
            self.assertTrue(snapshot.cell(1, 0).double_width_continuation)
            self.assertEqual(snapshot.cell(2, 0).char, '"')


if __name__ == "__main__":
    unittest.main()
