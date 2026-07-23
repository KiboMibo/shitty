# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

import unittest

from harness import Shitty


def double_click(terminal, column, time=1.0):
    x = column + 2
    terminal.button(0, True, x=x, y=2, time=time)
    terminal.button(0, False, x=x, y=2, time=time + 0.01)
    terminal.button(0, True, x=x, y=2, time=time + 0.1)
    return terminal.button(0, False, x=x, y=2, time=time + 0.11)


class SelectionWordUnicodeTest(unittest.TestCase):
    def test_ascii_punctuation_separates_words(self):
        with Shitty(columns=16, rows=2) as terminal:
            terminal.write(b"foo.bar")
            self.assertEqual(double_click(terminal, 3), b".")

    def test_different_punctuation_is_separate_but_repeats_group(self):
        with Shitty(columns=16, rows=2) as terminal:
            terminal.write(b"a.,!!b")
            self.assertEqual(double_click(terminal, 1), b".")
        with Shitty(columns=16, rows=2) as terminal:
            terminal.write(b"a.,!!b")
            self.assertEqual(double_click(terminal, 3), b"!!")

    def test_connector_punctuation_remains_part_of_identifier(self):
        with Shitty(columns=16, rows=2) as terminal:
            terminal.write(b"one baz_qux two")
            self.assertEqual(double_click(terminal, 7), b"baz_qux")

    def test_unicode_letters_and_marks_form_words(self):
        with Shitty(columns=20, rows=2) as terminal:
            terminal.write("café Ελληνικά".encode())
            self.assertEqual(double_click(terminal, 3), "café".encode())
        with Shitty(columns=20, rows=2) as terminal:
            terminal.write("café Ελληνικά".encode())
            self.assertEqual(double_click(terminal, 7), "Ελληνικά".encode())

    def test_clicking_wide_continuation_selects_the_unicode_word(self):
        with Shitty(columns=16, rows=2) as terminal:
            terminal.write("go 東京 now".encode())
            self.assertEqual(double_click(terminal, 4), "東京".encode())

    def test_whitespace_run_is_selectable_as_one_class(self):
        with Shitty(columns=16, rows=2) as terminal:
            terminal.write(b"foo   bar")
            self.assertEqual(double_click(terminal, 4), b"   ")


if __name__ == "__main__":
    unittest.main()
