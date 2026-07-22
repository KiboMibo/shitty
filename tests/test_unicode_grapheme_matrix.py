import unittest

from harness import Zutty


# Representative lines from Unicode 17 GraphemeBreakTest.txt.  Each rule in
# UAX #29 section 3.1.1 is represented, including the Unicode 17 GB9c rule.
OFFICIAL_VECTORS = (
    "÷ 000D × 000A ÷",                         # GB3: CR LF
    "÷ 000D ÷ 0308 ÷ 000A ÷",                # GB4/GB5 beat GB9
    "÷ 0061 × 0308 × 0300 ÷",                # GB9: Extend
    "÷ 0061 × 200D ÷ 0062 ÷",                # GB9: ZWJ, GB999
    "÷ 0061 × 0903 ÷",                       # GB9a: SpacingMark
    "÷ 06DD × 0061 ÷",                       # GB9b: Prepend
    "÷ 1100 × 1100 × 1160 × 11A8 × 11A8 ÷", # GB6-GB8
    "÷ AC00 × 1160 × 11A8 ÷",                # LV V T
    "÷ AC01 × 11A8 ÷",                       # LVT T
    "÷ 0915 × 094D × 0937 ÷",                # GB9c: Indic linker
    "÷ 0915 × 093C × 094D × 200D × 0937 ÷",  # GB9c with Extend/ZWJ
    "÷ 1F469 × 1F3FD ÷",                     # emoji modifier
    "÷ 1F469 × 0308 × 200D × 1F680 ÷",       # GB11: emoji ZWJ
    "÷ 200D ÷ 1F680 ÷",                      # GB11 needs ExtPict left
    "÷ 1F1E6 × 1F1E7 ÷ 1F1E8 × 1F1E9 ÷",    # GB12/GB13: RI parity
    "÷ 0023 × FE0F × 20E3 ÷",                # keycap
    "÷ 0308 × 0300 ÷ 0061 ÷",                # orphan Extend
    "÷ 0061 × FE0F ÷ 0062 ÷",                # variation selector
)


def parse_vector(vector):
    fields = vector.split()
    codepoints = tuple(int(value, 16) for value in fields[1::2])
    boundaries = tuple(value == "÷" for value in fields[0::2][:-1])
    return codepoints, boundaries


class UnicodeGraphemeMatrixTest(unittest.TestCase):
    def test_unicode_17_official_grapheme_break_vectors(self):
        with Zutty(columns=8, rows=2) as terminal:
            for vector in OFFICIAL_VECTORS:
                with self.subTest(vector=vector):
                    codepoints, expected = parse_vector(vector)
                    self.assertEqual(
                        terminal.grapheme_breaks(*codepoints), expected
                    )

    def test_prepend_spacing_mark_and_indic_linker_are_wide_clusters(self):
        samples = ("\u06ddA", "कः", "क्ष")
        for sample in samples:
            with self.subTest(sample=sample):
                with Zutty(columns=8, rows=2) as terminal:
                    terminal.write((sample + "X").encode())
                    snapshot = terminal.snapshot()
                    self.assertEqual(snapshot.cursor_x, 3)
                    self.assertTrue(snapshot.cell(0, 0).double_width)
                    self.assertTrue(snapshot.cell(1, 0).double_width_continuation)
                    terminal.select_start(0, 0)
                    terminal.select_update(2, 0)
                    self.assertEqual(terminal.select_finish(), sample.encode())

    def test_regional_indicators_pair_by_parity(self):
        flags = "🇦🇧🇨🇩"
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write((flags + "X").encode())
            self.assertEqual(terminal.snapshot().cursor_x, 5)
            terminal.select_start(0, 0)
            terminal.select_update(1, 0)
            self.assertEqual(terminal.select_finish(), "🇦🇧".encode())

    def test_keycap_and_orphan_extenders_remain_atomic(self):
        samples = (("#️⃣", 2), ("\u0308\u0300", 1), ("\u200d\u0308", 1))
        for sample, width in samples:
            with self.subTest(sample=sample, width=width):
                with Zutty(columns=6, rows=2) as terminal:
                    terminal.write((sample + "X").encode())
                    self.assertEqual(terminal.snapshot().cursor_x, width + 1)
                    terminal.select_start(0, 0)
                    terminal.select_update(width, 0)
                    self.assertEqual(terminal.select_finish(), sample.encode())

    def test_cluster_at_right_edge_wraps_as_one_unit(self):
        cluster = "क्ष"
        with Zutty(columns=3, rows=2) as terminal:
            terminal.write(("ab" + cluster + "X").encode())
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.lines[0], "ab ")
            self.assertEqual(snapshot.lines[1], "क X")
            self.assertTrue(snapshot.cell(0, 1).double_width)
            self.assertTrue(snapshot.cell(1, 1).double_width_continuation)
            terminal.select_start(0, 1)
            terminal.select_update(2, 1)
            self.assertEqual(terminal.select_finish(), cluster.encode())

    def test_variation_width_changes_at_right_edge(self):
        with Zutty(columns=3, rows=2) as terminal:
            terminal.write("ab#️X".encode())
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.lines[0], "ab ")
            self.assertEqual(snapshot.lines[1], "# X")
            self.assertTrue(snapshot.cell(0, 1).double_width)

        with Zutty(columns=3, rows=2) as terminal:
            terminal.write("a⌚︎X".encode())
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.lines[0], "a⌚X")
            self.assertFalse(snapshot.cell(1, 0).double_width)


if __name__ == "__main__":
    unittest.main()
