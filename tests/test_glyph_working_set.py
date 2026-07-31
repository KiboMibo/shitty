# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

import unittest

from harness import Shitty


class GlyphWorkingSetTest(unittest.TestCase):
    def test_counts_distinct_ids_across_window_and_history(self):
        with Shitty(columns=10, rows=2) as terminal:
            terminal.write(b"ab")
            self.assertEqual(terminal.distinct_glyphs(), 3)

            terminal.write(b"\r\ncd\r\nef\r\n")
            self.assertEqual(terminal.distinct_glyphs(), 7)

    def test_counts_wide_and_cluster_glyphs_once(self):
        with Shitty(columns=10, rows=2) as terminal:
            terminal.write("😀😀".encode())
            self.assertEqual(terminal.distinct_glyphs(), 2)

            terminal.write("👩‍💻".encode())
            self.assertEqual(terminal.distinct_glyphs(), 3)


if __name__ == "__main__":
    unittest.main()
