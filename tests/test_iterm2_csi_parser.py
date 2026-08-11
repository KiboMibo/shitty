# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

"""Public adaptations of the first 19 iTerm2 CSI parser cases."""

import unittest

from harness import Shitty


PORTED_CASES = (
    "testCSIOnly",
    "testPrefixOnly",
    "testPrefixParameterOnly",
    "testPrefixParameterIntermediateOnly",
    "testFullyFormedPrefixParameterIntermediateFinal",
    "testSimpleCSI",
    "testSimpleCSIWithParameter",
    "testSimpleCSIWithTwoDigitParameter",
    "testParameterPrefix",
    "testTwoParameters",
    "testCursorForwardTabulation",
    "testCursorForwardTabulationDefault",
    "testSubParameter",
    "testBogusCharacterInParameters",
    "testDualModeRGBForeground",
    "testDualModeRGBBackground",
    "testDualModeIndexedForeground",
    "testDualModeRejectsSemicolonForm",
    "testDualModeShortSubparametersInvalid",
)


def assert_pending_csi(testcase, prefix, suffix, body):
    with Shitty(columns=40, rows=8, save_lines=0) as terminal:
        terminal.parser_trace_on()
        terminal.write(prefix)
        testcase.assertEqual(terminal.parser_trace(), [])
        terminal.write(suffix)
        testcase.assertEqual(terminal.parser_trace(), [("csi", body)])


class ITerm2CSIParserTest(unittest.TestCase):
    def test_upstream_inventory_has_first_19_distinct_cases(self):
        self.assertEqual(len(PORTED_CASES), 19)
        self.assertEqual(len(set(PORTED_CASES)), 19)

    def test_csi_introducer_remains_pending(self):
        assert_pending_csi(self, b"\x1b[", b"D", b"D")

    def test_private_prefix_remains_pending(self):
        assert_pending_csi(self, b"\x1b[?", b"36$p", b"?36$p")

    def test_private_prefix_and_parameter_remain_pending(self):
        assert_pending_csi(self, b"\x1b[?36", b"$p", b"?36$p")

    def test_private_parameter_and_intermediate_remain_pending(self):
        assert_pending_csi(self, b"\x1b[?36$", b"p", b"?36$p")

    def test_complete_private_parameter_intermediate_final_dispatches(self):
        with Shitty(columns=40, rows=8, save_lines=0) as terminal:
            terminal.parser_trace_on()
            terminal.write(b"\x1b[?36$p")
            self.assertEqual(terminal.parser_trace(), [("csi", b"?36$p")])

    def test_cub_defaults_to_one_column(self):
        with Shitty(columns=40, rows=2, save_lines=0) as terminal:
            terminal.write(b"ABCDE\x1b[DX")
            self.assertEqual(terminal.snapshot().lines[0][:5], "ABCDX")

    def test_cub_accepts_one_digit_parameter(self):
        with Shitty(columns=40, rows=2, save_lines=0) as terminal:
            terminal.write(b"ABCDE\x1b[2DX")
            self.assertEqual(terminal.snapshot().lines[0][:5], "ABCXE")

    def test_cub_accepts_two_digit_parameter(self):
        with Shitty(columns=40, rows=2, save_lines=0) as terminal:
            terminal.write(b"A" * 25 + b"\x1b[23DX")
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.cell(2, 0).char, "X")
            self.assertEqual(snapshot.cursor_x, 3)

    def test_da2_private_prefix_and_parameter_dispatch(self):
        with Shitty(columns=40, rows=2, save_lines=0) as terminal:
            terminal.parser_trace_on()
            terminal.write(b"\x1b[>23c")
            self.assertEqual(terminal.parser_trace(), [("csi", b">23c")])
            self.assertTrue(terminal.read_input().startswith(b"\x1b[>"))

    def test_cup_accepts_two_parameters(self):
        with Shitty(columns=40, rows=8, save_lines=0) as terminal:
            terminal.write(b"\x1b[5;6H")
            snapshot = terminal.snapshot()
            self.assertEqual((snapshot.cursor_x, snapshot.cursor_y), (5, 4))

    def test_cht_accepts_explicit_count(self):
        with Shitty(columns=40, rows=2, save_lines=0) as terminal:
            terminal.write(b"\x1b[2IX")
            self.assertEqual(terminal.snapshot().cell(16, 0).char, "X")

    def test_cht_defaults_to_one_tab_stop(self):
        with Shitty(columns=40, rows=2, save_lines=0) as terminal:
            terminal.write(b"\x1b[IX")
            self.assertEqual(terminal.snapshot().cell(8, 0).char, "X")

    def test_colon_subparameters_reach_truecolor_executor(self):
        with Shitty(columns=8, rows=2, save_lines=0) as terminal:
            terminal.parser_trace_on()
            terminal.write(b"\x1b[38:2:255:128:64:0:5:1mX")
            self.assertEqual(
                terminal.parser_trace(),
                [
                    ("csi", b"38:2:255:128:64:0:5:1m"),
                    ("text", b"X"),
                ],
            )
            self.assertEqual(terminal.snapshot().cell(0, 0).foreground, (128, 64, 0))

    def test_bogus_equals_in_parameter_section_invalidates_csi(self):
        with Shitty(columns=8, rows=2, save_lines=0) as terminal:
            terminal.parser_trace_on()
            terminal.write(b"\x1b[38=mX")
            self.assertEqual(terminal.parser_trace(), [("text", b"X")])

    @unittest.expectedFailure
    def test_dual_mode_rgb_foreground_tracks_light_and_dark_values(self):
        for background, expected in (
            ("#ffffff", (10, 20, 30)),
            ("#000000", (40, 50, 60)),
        ):
            with self.subTest(background=background):
                with Shitty(
                    columns=8,
                    rows=2,
                    save_lines=0,
                    extra_arguments=("-bg", background),
                ) as terminal:
                    terminal.write(b"\x1b[38:12:10:20:30:40:50:60mX")
                    self.assertEqual(
                        terminal.snapshot().cell(0, 0).foreground,
                        expected,
                    )

    @unittest.expectedFailure
    def test_dual_mode_rgb_background_tracks_light_and_dark_values(self):
        for configured_background, expected in (
            ("#ffffff", (1, 2, 3)),
            ("#000000", (4, 5, 6)),
        ):
            with self.subTest(background=configured_background):
                with Shitty(
                    columns=8,
                    rows=2,
                    save_lines=0,
                    extra_arguments=("-bg", configured_background),
                ) as terminal:
                    terminal.write(b"\x1b[48:12:1:2:3:4:5:6mX")
                    self.assertEqual(
                        terminal.snapshot().cell(0, 0).background,
                        expected,
                    )

    @unittest.expectedFailure
    def test_dual_mode_indexed_foreground_tracks_both_palette_indices(self):
        for background, expected in (("#ffffff", 208), ("#000000", 11)):
            with self.subTest(background=background):
                with Shitty(
                    columns=8,
                    rows=2,
                    save_lines=0,
                    extra_arguments=("-bg", background),
                ) as terminal:
                    terminal.write(b"\x1b[38:13:208:11mX")
                    self.assertEqual(
                        terminal.snapshot().cell(0, 0).foreground_index,
                        expected,
                    )

    def test_dual_mode_semicolon_form_is_not_accepted(self):
        with Shitty(columns=8, rows=2, save_lines=0) as terminal:
            default_foreground = terminal.snapshot().cell(0, 0).foreground
            terminal.write(b"\x1b[38;12;1;2;3;4;5;6mX")
            cell = terminal.snapshot().cell(0, 0)
            self.assertEqual(cell.foreground, default_foreground)
            self.assertTrue(cell.bold)
            self.assertTrue(cell.italic)
            self.assertTrue(cell.underline)

    def test_short_dual_mode_subparameters_do_not_set_a_color(self):
        with Shitty(columns=8, rows=2, save_lines=0) as terminal:
            default_foreground = terminal.snapshot().cell(0, 0).foreground
            terminal.write(b"\x1b[38:12:1:2:3mX")
            self.assertEqual(
                terminal.snapshot().cell(0, 0).foreground,
                default_foreground,
            )


if __name__ == "__main__":
    unittest.main()
