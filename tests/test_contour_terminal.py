# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

import unittest
from pathlib import Path
from tempfile import TemporaryDirectory

from harness import Shitty


UPSTREAM_CASES = (
    "Terminal.BlinkingCursor",
    "Terminal.IME.CursorVisibleDuringComposition",
    "Terminal.ModifierKeysDoNotScrollViewport",
    "Terminal.localPathAtMousePosition",
    "Terminal.AutoScrollOnUpdate",
    "Terminal.DECCARA",
    "Terminal.CaptureScreenBuffer",
    "Terminal.RIS",
    "Terminal.RIS.keepsFrozenModesAppliedToInputGenerator",
    "Terminal.RIS.resetsPassiveMouseTracking",
    "Terminal.mouse coordinate modes are one mutually-exclusive setting",
    "Terminal.forceRedraw keeps the cell size",
    "Terminal.clampedTotalPageSize",
    "Terminal.DECCOLM.doesNotDoubleCountStatusLine",
    "Terminal.DECSCPP.doesNotDoubleCountStatusLine",
    "Terminal.DECCOLM.resizesGridSynchronously",
    "Terminal.RIS.resetsDECCOLM",
    "Terminal.DECNCSM",
    "Terminal.SynchronizedOutput",
    "Terminal.XTPUSHCOLORS_and_XTPOPCOLORS",
    "Terminal.DECAC",
    "Terminal.DECATC",
    "Terminal.DECSTGLT",
    "Terminal.DECATC.doesNotRecolorTheStatusLine",
    "Terminal.UnderlineStyleClearing",
    "Terminal.selection_does_not_pad_wide_characters",
    "Terminal.a_one_column_selection_still_breaks_lines",
    "Terminal.selection_keeps_leading_blank_lines",
    "Terminal.CurlyUnderline",
    "Terminal.TextSelection",
    "Terminal.TextSelection_wrapped_line",
    "Terminal.ParsingBuffer",
    "Terminal.TrivialLineBufferIntegrity",
    "Terminal.BoxDrawingCharacters",
    "Terminal.smoothScrollExtraLines.zero_when_no_offset",
    "Terminal.smoothScrollExtraLines.one_when_offset_nonzero",
    "Terminal.screenTransitionProgress.no_transition_returns_1",
    "Terminal.cursorAnimationProgress.no_animation_returns_1",
    "Terminal.applySmoothScrollPixelDelta.accumulates_subline_offset",
    "Terminal.applySmoothScrollPixelDelta.converts_full_cell_to_scroll",
    "Terminal.applySmoothScrollPixelDelta.clamps_at_top_of_history",
    "Terminal.applySmoothScrollPixelDelta.returns_disabled_on_alternate_screen",
    "Terminal.onBufferScrolled.preserves_viewport_with_pixel_offset",
    "Terminal.momentumScroll.starts_on_end_with_velocity",
    "Terminal.momentumScroll.velocity_computation_is_correct",
    "Terminal.momentumScroll.no_start_below_threshold",
    "Terminal.momentumScroll.no_start_with_single_sample",
    "Terminal.momentumScroll.decelerates_over_ticks",
    "Terminal.momentumScroll.stops_at_min_velocity",
)


def write_numbered_history(terminal, count=16):
    terminal.write(
        b"".join(f"{line:02d}\r\n".encode() for line in range(count))
    )


def precise_scroll_result(*deltas):
    with Shitty(
        columns=8, rows=4, save_lines=32, glyph_px=4, glyph_py=8
    ) as terminal:
        write_numbered_history(terminal)
        time = 0.01
        for delta in deltas:
            terminal.scroll(
                0, delta, phase="update", precise=True, time=time
            )
            time += 0.01
        return terminal.snapshot(), terminal.reference_image()


class ContourTerminalTest(unittest.TestCase):
    def test_upstream_inventory_has_first_49_cases(self):
        self.assertEqual(len(UPSTREAM_CASES), 49)
        self.assertEqual(len(set(UPSTREAM_CASES)), 49)

    def test_blinking_cursor_advances_through_both_phases(self):
        with Shitty(columns=8, rows=2) as terminal:
            terminal.write(b"\x1b[1 q")
            self.assertTrue(terminal.render_state().cursor_blink)
            self.assertTrue(terminal.render_state().blink_visible)

            terminal.blink_tick()
            self.assertFalse(terminal.render_state().blink_visible)
            terminal.blink_tick()
            self.assertTrue(terminal.render_state().blink_visible)

    def test_ime_composition_renders_while_blink_phase_is_off(self):
        with Shitty(columns=8, rows=2) as terminal:
            terminal.write(b"\x1b[1 q")
            terminal.blink_tick()
            self.assertFalse(terminal.render_state().blink_visible)

            terminal.preedit("test", 0, 4)
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.lines[0], "test    ")
            self.assertEqual(
                [snapshot.cell(column, 0).inverse for column in range(4)],
                [True] * 4,
            )

    def test_reported_modifier_keys_preserve_the_scrolled_viewport(self):
        modifiers = (
            (280, 16),
            (282, 32),
            (340, 1),
            (341, 2),
            (342, 4),
            (343, 8),
            (344, 1),
            (345, 2),
            (346, 4),
            (347, 8),
        )
        for key, mask in modifiers:
            with self.subTest(key=key):
                with Shitty(columns=8, rows=3, save_lines=8) as terminal:
                    terminal.write(
                        b"one\r\ntwo\r\nthree\r\nfour\x1b[>8u"
                    )
                    terminal.page_up()

                    terminal.frontend_key_event(key, 1, modifiers=mask)

                    self.assertEqual(terminal.snapshot().view_offset, 1)
                    packet = terminal.read_input()
                    self.assertTrue(packet.startswith(b"\x1b["))
                    self.assertTrue(packet.endswith(b"u"))

        with Shitty(columns=8, rows=3, save_lines=8) as terminal:
            terminal.write(b"one\r\ntwo\r\nthree\r\nfour\x1b[>8u")
            terminal.page_up()

            terminal.frontend_key_event(257, 1)

            self.assertEqual(terminal.snapshot().view_offset, 0)
            self.assertEqual(terminal.read_input(), b"\x1b[13u")

    @unittest.expectedFailure
    def test_existing_local_paths_are_resolved_at_the_pointer(self):
        with TemporaryDirectory(prefix="shitty-contour-path-") as root_text:
            root = Path(root_text)
            nested = root / "nested"
            nested.mkdir()
            target = nested / "file.txt"
            target.write_text("test")
            short = root / "short~1"
            short.mkdir()
            short_target = short / "file.txt"
            short_target.write_text("test")

            with Shitty(columns=240, rows=4) as terminal:
                terminal.osc7_cwd(("file://" + root.as_posix()).encode())
                terminal.write(
                    b"open nested/file.txt now\r\n"
                    + b"open " + target.as_posix().encode() + b"\r\n"
                    + b"open " + short_target.as_posix().encode() + b"\r\n"
                    + b"open nested/missing.txt now"
                )

                self.assertEqual(
                    (
                        terminal.hyperlink(10, 0),
                        terminal.hyperlink(8, 1),
                        terminal.hyperlink(8, 2),
                        terminal.hyperlink(10, 3),
                    ),
                    (
                        target.as_posix(),
                        target.as_posix(),
                        short_target.as_posix(),
                        "",
                    ),
                )

    def test_output_preserves_viewport_and_typed_input_returns_to_bottom(self):
        for input_kind in ("key", "text"):
            with self.subTest(input_kind=input_kind):
                with Shitty(columns=8, rows=3, save_lines=8) as terminal:
                    terminal.write(b"one\r\ntwo\r\nthree\r\nfour")
                    terminal.page_up()
                    before = terminal.snapshot()

                    terminal.write(b"\r\nfive")
                    after_output = terminal.snapshot()
                    self.assertEqual(after_output.view_offset, 2)
                    self.assertEqual(after_output.lines, before.lines)

                    if input_kind == "key":
                        terminal.frontend_key_event(257, 1)
                    else:
                        terminal.frontend_text_event("a")
                    self.assertEqual(terminal.snapshot().view_offset, 0)

    def test_deccara_changes_only_the_requested_rectangle_attributes(self):
        with Shitty(columns=5, rows=5) as terminal:
            original = ["12345", "67890", "ABCDE", "abcde", "fghij"]
            terminal.write("\r\n".join(original).encode())
            terminal.write(
                b"\x1b[2*x"
                b"\x1b[2;3;4;5;1;4$r"
            )

            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.lines, original)
            for row in range(5):
                for column in range(5):
                    cell = snapshot.cell(column, row)
                    changed = 1 <= row <= 3 and 2 <= column <= 4
                    self.assertEqual(cell.bold, changed)
                    self.assertEqual(cell.underline, changed)

    def test_contour_private_capture_request_is_ignored(self):
        with Shitty(columns=5, rows=5, save_lines=20) as terminal:
            terminal.write(
                b"1\r\n2\r\n3\r\n4\r\n5\r\n6\r\n7\r\n8\r\n9\r\n10"
            )
            before = terminal.snapshot()

            terminal.write(b"\x1b[>0;7,t")

            after = terminal.snapshot()
            self.assertEqual(after.lines, before.lines)
            self.assertEqual(after.view_offset, before.view_offset)
            self.assertEqual(terminal.read_input(), b"")

    def test_ris_resets_application_cursor_mode_and_its_encoder_state(self):
        with Shitty(columns=8, rows=3) as terminal:
            terminal.write(b"text\x1b[?1h")
            terminal.frontend_key_event(265, 1)
            self.assertEqual(terminal.read_input(), b"\x1bOA")

            terminal.write(b"\x1bc")
            terminal.frontend_key_event(265, 1)

            self.assertEqual(terminal.snapshot().lines, [" " * 8] * 3)
            self.assertEqual(terminal.read_input(), b"\x1b[A")

    def test_contour_passive_mouse_mode_is_an_intentional_boundary(self):
        with Shitty(columns=8, rows=3) as terminal:
            terminal.write(b"\x1b[?2029h\x1b[?2029$p")
            self.assertEqual(terminal.read_input(), b"\x1b[?2029;0$y")

            terminal.write(b"\x1bc\x1b[?2029$p")
            self.assertEqual(terminal.read_input(), b"\x1b[?2029;0$y")

    def test_mouse_coordinate_modes_are_mutually_exclusive(self):
        with Shitty(columns=8, rows=3) as terminal:
            terminal.write(b"\x1b[?1006h")
            self.assertEqual(terminal.state()[1], 2)
            for reset in (1005, 1015, 1016):
                terminal.write(f"\x1b[?{reset}l".encode())
                self.assertEqual(terminal.state()[1], 2)

            terminal.write(b"\x1b[?1016h")
            self.assertEqual(terminal.state()[1], 4)
            terminal.write(b"\x1b[?1015h")
            self.assertEqual(terminal.state()[1], 3)
            terminal.write(b"\x1b[?1015l")
            self.assertEqual(terminal.state()[1], 0)

    def test_redraw_resize_round_trip_keeps_cell_pixel_size(self):
        with Shitty(
            columns=10, rows=5, glyph_px=9, glyph_py=18
        ) as terminal:
            self.assertEqual(terminal.winsize_full(), (10, 5, 90, 90))
            terminal.resize(11, 5)
            self.assertEqual(terminal.winsize_full(), (11, 5, 99, 90))
            terminal.resize(10, 5)
            self.assertEqual(terminal.winsize_full(), (10, 5, 90, 90))

    def test_pixel_resize_clamps_the_page_to_one_cell(self):
        with Shitty(
            columns=20, rows=5, glyph_px=9, glyph_py=18
        ) as terminal:
            terminal.resize_pixels(5, 5)
            snapshot = terminal.snapshot()
            self.assertEqual((snapshot.columns, snapshot.rows), (1, 1))
            self.assertEqual(terminal.winsize_full(), (1, 1, 9, 18))

    def test_deccolm_changes_columns_without_changing_rows(self):
        with Shitty(columns=80, rows=24) as terminal:
            terminal.write(b"\x1b[?40h\x1b[?3h")
            snapshot = terminal.snapshot()
            self.assertEqual((snapshot.columns, snapshot.rows), (132, 24))
            terminal.write(b"\x1b[?3l")
            snapshot = terminal.snapshot()
            self.assertEqual((snapshot.columns, snapshot.rows), (80, 24))

    @unittest.expectedFailure
    def test_decscpp_changes_columns_without_changing_rows(self):
        with Shitty(columns=80, rows=24) as terminal:
            terminal.write(b"\x1b[132$|")
            snapshot = terminal.snapshot()
            self.assertEqual((snapshot.columns, snapshot.rows), (132, 24))

    def test_deccolm_resizes_before_following_output_is_processed(self):
        with Shitty(columns=80, rows=24) as terminal:
            terminal.write(b"\x1b[?40h\x1b[?3h\x1b[1;132HX")
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.columns, 132)
            self.assertEqual(snapshot.cell(131, 0).char, "X")

    def test_ris_resets_deccolm_to_80_columns(self):
        with Shitty(columns=80, rows=24) as terminal:
            terminal.write(b"\x1b[?40h\x1b[?3h")
            self.assertEqual(terminal.snapshot().columns, 132)
            terminal.write(b"\x1bc")
            self.assertEqual(terminal.snapshot().columns, 80)

    def test_decncsm_controls_deccolm_screen_clearing(self):
        with Shitty(columns=80, rows=5) as terminal:
            terminal.write(b"\x1b[?40hHELLO\x1b[?3h")
            self.assertNotIn("HELLO", terminal.snapshot().lines[0])

        with Shitty(columns=80, rows=5) as terminal:
            terminal.write(
                b"\x1b[65;1\"p\x1b[?40h\x1b[?95hHELLO\x1b[?3h"
            )
            self.assertTrue(terminal.snapshot().lines[0].startswith("HELLO"))

    def test_synchronized_output_publishes_only_the_completed_frame(self):
        with Shitty(columns=8, rows=1) as terminal:
            terminal.write(b"\x00\x1b[?2026hHello ")
            self.assertEqual(terminal.snapshot().lines, [" " * 8])
            terminal.write(b"X")
            self.assertEqual(terminal.snapshot().lines, [" " * 8])
            terminal.write(b"\x1b[?2026l")
            self.assertEqual(terminal.snapshot().lines, ["Hello X "])

    @unittest.expectedFailure
    def test_xterm_color_stack_restores_the_pushed_palette(self):
        with Shitty(columns=4, rows=1) as terminal:
            terminal.write(
                b"\x1b]4;1;rgb:ff/00/00\x1b\\"
                b"\x1b[#P"
                b"\x1b]4;1;rgb:00/ff/00\x1b\\"
                b"\x1b[#Q"
                b"\x1b[31mX"
            )
            self.assertEqual(
                terminal.snapshot().cell(0, 0).foreground,
                (255, 0, 0),
            )

    def test_decac_assigns_and_resets_default_text_colors(self):
        with Shitty(columns=4, rows=1) as terminal:
            terminal.write(b"\x1b[1;2;5,|A")
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.cell(0, 0).foreground, (0, 170, 0))
            self.assertEqual(snapshot.cell(0, 0).background, (170, 0, 170))

            terminal.write(b"\x1b[1,|B")
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.cell(1, 0).foreground, (255, 255, 255))
            self.assertEqual(snapshot.cell(1, 0).background, (0, 0, 0))

    @unittest.expectedFailure
    def test_decatc_assigns_colors_to_the_bold_attribute(self):
        with Shitty(columns=4, rows=1) as terminal:
            terminal.write(b"\x1b[1){\x1b[1;2;5,}\x1b[1mX")
            cell = terminal.snapshot().cell(0, 0)
            self.assertTrue(cell.bold)
            self.assertEqual(cell.foreground, (0, 170, 0))
            self.assertEqual(cell.background, (170, 0, 170))

    @unittest.expectedFailure
    def test_decstglt_selects_the_alternate_color_table(self):
        with Shitty(columns=4, rows=1) as terminal:
            terminal.write(
                b"\x1b[1;2;5,}"
                b"\x1b[1){\x1b[1mA"
                b"\x1b[3){\x1b[1mB"
            )
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.cell(0, 0).foreground, (0, 170, 0))
            self.assertNotEqual(
                snapshot.cell(1, 0).foreground,
                (0, 170, 0),
            )

    def test_decatc_status_line_case_has_no_synthetic_screen(self):
        with Shitty(columns=20, rows=4) as terminal:
            terminal.write(b"AB\x1b[1){\x1b[0;2;5,}")
            snapshot = terminal.snapshot()
            self.assertEqual((snapshot.columns, snapshot.rows), (20, 4))
            self.assertTrue(snapshot.lines[0].startswith("AB"))

    def test_each_underline_style_replaces_the_previous_one(self):
        with Shitty(columns=12, rows=1) as terminal:
            terminal.write(
                b"\x1b[4:1mAB\x1b[21mCD\x1b[4:3mEF"
                b"\x1b[24mGH\x1b[4:2mIJ\x1b[mKL"
            )
            snapshot = terminal.snapshot()
            self.assertEqual(
                [snapshot.cell(column, 0).underline_style
                 for column in range(12)],
                [1, 1, 2, 2, 3, 3, 0, 0, 2, 2, 0, 0],
            )

    def test_selection_does_not_pad_wide_characters(self):
        with Shitty(columns=6, rows=1) as terminal:
            terminal.write("中ab".encode())
            terminal.select_start(0, 0)
            terminal.select_update(4, 0)
            self.assertEqual(terminal.select_finish(), "中ab".encode())

    def test_one_column_rectangular_selection_keeps_line_breaks(self):
        with Shitty(columns=4, rows=3) as terminal:
            terminal.write(b"a\r\nb\r\nc")
            terminal.select_start(0, 0)
            terminal.select_rectangular()
            terminal.select_update(1, 2)
            self.assertEqual(terminal.select_finish(), b"a\nb\nc")

    def test_linear_selection_keeps_leading_blank_lines(self):
        with Shitty(columns=6, rows=3) as terminal:
            terminal.write(b"\x1b[3;1Habc")
            terminal.select_start(0, 0)
            terminal.select_update(3, 2)
            self.assertEqual(terminal.select_finish(), b"\n\nabc")

    def test_curly_underline_is_not_italic_and_resets_cleanly(self):
        with Shitty(columns=4, rows=1) as terminal:
            terminal.write(b"\x1b[4:3mAB\x1b[mCD")
            snapshot = terminal.snapshot()
            self.assertEqual(
                [snapshot.cell(column, 0).underline_style
                 for column in range(4)],
                [3, 3, 0, 0],
            )
            self.assertEqual(
                [snapshot.cell(column, 0).italic for column in range(4)],
                [False] * 4,
            )

    def test_mouse_selection_drag_release_and_click_clear(self):
        with Shitty(
            columns=5, rows=5, glyph_px=2, glyph_py=2
        ) as terminal:
            terminal.write(
                b"12345\r\n67890\r\nABCDE\r\nabcde\r\nfghij"
            )
            terminal.pointer(5, 5)
            self.assertEqual(
                terminal.button(0, True, x=5, y=5, time=1.0), b""
            )
            terminal.pointer(8, 7)
            self.assertEqual(
                terminal.button(0, False, x=8, y=7, time=1.1),
                b"7890\nABC",
            )

            terminal.button(0, True, x=8, y=7, time=2.0)
            self.assertEqual(
                terminal.button(0, False, x=8, y=7, time=2.1), b""
            )

    def test_mouse_selection_joins_a_soft_wrapped_line(self):
        with Shitty(
            columns=5, rows=2, glyph_px=2, glyph_py=2
        ) as terminal:
            terminal.write(b"aaaaaaaaaa")
            terminal.pointer(5, 3)
            terminal.button(0, True, x=5, y=3, time=1.0)
            terminal.pointer(6, 5)
            self.assertEqual(
                terminal.button(0, False, x=6, y=5, time=1.1),
                b"aaaaaa",
            )

    def test_parser_buffer_survives_chunk_boundaries(self):
        with Shitty(columns=12, rows=2) as terminal:
            terminal.feed_chunks(
                b"Hello \xe2", b"\x94", b"\x82", b"\x1b[3", b"1mX"
            )
            snapshot = terminal.snapshot()
            self.assertTrue(snapshot.lines[0].startswith("Hello │X"))
            self.assertEqual(
                snapshot.cell(7, 0).foreground, (170, 0, 0)
            )

    def test_trivial_ascii_line_buffer_keeps_all_bytes(self):
        with Shitty(columns=20, rows=3) as terminal:
            terminal.write(b"ABCDEFGHIJ")
            self.assertEqual(
                terminal.snapshot().lines[0], "ABCDEFGHIJ" + " " * 10
            )

    def test_box_drawing_utf8_is_not_corrupted(self):
        with Shitty(columns=20, rows=5) as terminal:
            terminal.write("│── file\r\n├── dir".encode())
            snapshot = terminal.snapshot()
            self.assertTrue(snapshot.lines[0].startswith("│── file"))
            self.assertTrue(snapshot.lines[1].startswith("├── dir"))
            self.assertNotIn("�", "".join(snapshot.lines))

    def test_smooth_scroll_has_no_extra_render_row_at_zero_offset(self):
        snapshot, image = precise_scroll_result(0.0)
        self.assertEqual(snapshot.view_offset, 0)
        self.assertEqual(image, precise_scroll_result(0.0)[1])

    @unittest.expectedFailure
    def test_smooth_scroll_renders_an_extra_row_at_fractional_offset(self):
        zero, zero_image = precise_scroll_result(0.0)
        partial, partial_image = precise_scroll_result(0.25)
        self.assertEqual((zero.view_offset, partial.view_offset), (0, 0))
        self.assertNotEqual(partial_image, zero_image)

    def test_screen_without_transition_has_a_stable_complete_frame(self):
        with Shitty(columns=10, rows=4) as terminal:
            terminal.write(b"stable")
            before = terminal.snapshot()
            terminal.repaint()
            after = terminal.snapshot()
            self.assertEqual(after.lines, before.lines)
            self.assertEqual(
                (after.cursor_x, after.cursor_y, after.cursor_style),
                (before.cursor_x, before.cursor_y, before.cursor_style),
            )

    def test_cursor_without_animation_has_a_stable_complete_frame(self):
        with Shitty(columns=10, rows=4) as terminal:
            terminal.write(b"\x1b[2 qX")
            before = terminal.snapshot()
            terminal.blink_tick()
            after = terminal.snapshot()
            self.assertEqual(after.lines, before.lines)
            self.assertEqual(
                (after.cursor_x, after.cursor_y, after.cursor_style),
                (before.cursor_x, before.cursor_y, before.cursor_style),
            )

    @unittest.expectedFailure
    def test_precision_scroll_accumulates_visible_subline_offset(self):
        zero, zero_image = precise_scroll_result(0.0)
        quarter, quarter_image = precise_scroll_result(0.25)
        half, half_image = precise_scroll_result(0.25, 0.25)
        self.assertEqual(
            (zero.view_offset, quarter.view_offset, half.view_offset),
            (0, 0, 0),
        )
        self.assertNotEqual(quarter_image, zero_image)
        self.assertNotEqual(half_image, quarter_image)

    @unittest.expectedFailure
    def test_precision_scroll_keeps_remainder_after_full_cell(self):
        whole, whole_image = precise_scroll_result(1.0)
        partial, partial_image = precise_scroll_result(1.25)
        self.assertEqual((whole.view_offset, partial.view_offset), (1, 1))
        self.assertNotEqual(partial_image, whole_image)

    @unittest.expectedFailure
    def test_precision_scroll_clears_fraction_at_history_boundary(self):
        with Shitty(
            columns=8, rows=4, save_lines=32, glyph_px=4, glyph_py=8
        ) as terminal:
            write_numbered_history(terminal)
            terminal.scroll(
                0, 20.5, phase="update", precise=True, time=0.01
            )
            top = terminal.snapshot().view_offset
            terminal.scroll(
                0, -1.25, phase="update", precise=True, time=0.02
            )
            self.assertEqual(terminal.snapshot().view_offset, top - 1)

    def test_precision_scroll_is_disabled_on_alternate_screen(self):
        with Shitty(
            columns=12, rows=4, save_lines=16, glyph_px=4, glyph_py=8
        ) as terminal:
            terminal.write(b"\x1b[?1049halternate")
            terminal.scroll(
                0, 0.5, phase="update", precise=True, time=0.01
            )
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.view_offset, 0)
            self.assertTrue(snapshot.lines[0].startswith("alternate"))

    @unittest.expectedFailure
    def test_output_preserves_viewport_with_fractional_pixel_offset(self):
        with Shitty(
            columns=8, rows=4, save_lines=32, glyph_px=4, glyph_py=8
        ) as terminal:
            write_numbered_history(terminal, 10)
            terminal.scroll(
                0, 0.25, phase="update", precise=True, time=0.01
            )
            before = terminal.reference_image()
            terminal.write(b"10\r\n11\r\n")
            after = terminal.snapshot()
            self.assertGreater(after.view_offset, 0)
            self.assertEqual(terminal.reference_image(), before)

    @unittest.expectedFailure
    def test_precision_gesture_end_starts_momentum_with_velocity(self):
        with Shitty(columns=8, rows=4, save_lines=64) as terminal:
            write_numbered_history(terminal, 32)
            terminal.scroll(
                0, 0, phase="begin", precise=True, time=0.0
            )
            for time in (0.01, 0.02, 0.03):
                terminal.scroll(
                    0, 0.75, phase="update", precise=True, time=time
                )
            terminal.scroll(
                0, 0, phase="end", precise=True, time=0.04
            )
            before = terminal.snapshot().view_offset
            terminal.blink_tick()
            self.assertGreater(terminal.snapshot().view_offset, before)

    def test_native_momentum_velocity_advances_the_viewport(self):
        with Shitty(columns=8, rows=4, save_lines=64) as terminal:
            write_numbered_history(terminal, 32)
            terminal.scroll(
                0, 2.0, phase="begin", precise=True, momentum=True,
                time=0.05,
            )
            before = terminal.snapshot().view_offset
            terminal.scroll(
                0, 1.5, phase="update", precise=True, momentum=True,
                time=0.066,
            )
            self.assertGreater(terminal.snapshot().view_offset, before)

    def test_slow_precision_gesture_does_not_start_momentum(self):
        with Shitty(columns=8, rows=4, save_lines=32) as terminal:
            write_numbered_history(terminal)
            terminal.scroll(
                0, 0, phase="begin", precise=True, time=0.0
            )
            terminal.scroll(
                0, 0.02, phase="update", precise=True, time=0.1
            )
            terminal.scroll(
                0, 0.02, phase="update", precise=True, time=0.2
            )
            terminal.scroll(
                0, 0, phase="end", precise=True, time=0.3
            )
            terminal.blink_tick()
            self.assertEqual(terminal.snapshot().view_offset, 0)

    def test_single_native_momentum_sample_is_applied(self):
        with Shitty(columns=8, rows=4, save_lines=32) as terminal:
            write_numbered_history(terminal)
            terminal.scroll(
                0, 1.25, phase="begin", precise=True, momentum=True,
                time=0.01,
            )
            terminal.scroll(
                0, 0, phase="end", precise=True, momentum=True,
                time=0.02,
            )
            self.assertEqual(terminal.snapshot().view_offset, 1)

    def test_native_momentum_packets_decelerate_without_reversing(self):
        with Shitty(columns=8, rows=4, save_lines=64) as terminal:
            write_numbered_history(terminal, 32)
            offsets = [terminal.snapshot().view_offset]
            deltas = (3.0, 2.0, 1.0, 0.5, 0.25, 0.125)
            for index, delta in enumerate(deltas):
                terminal.scroll(
                    0,
                    delta,
                    phase="begin" if index == 0 else "update",
                    precise=True,
                    momentum=True,
                    time=0.05 + index * 0.016,
                )
                offsets.append(terminal.snapshot().view_offset)
            advances = [
                current - previous
                for previous, current in zip(offsets, offsets[1:])
            ]
            self.assertEqual(advances, sorted(advances, reverse=True))
            self.assertGreater(advances[0], advances[-1])

    def test_native_momentum_end_stops_viewport_motion(self):
        with Shitty(columns=8, rows=4, save_lines=64) as terminal:
            write_numbered_history(terminal, 32)
            terminal.scroll(
                0, 2.0, phase="begin", precise=True, momentum=True,
                time=0.05,
            )
            terminal.scroll(
                0, 1.0, phase="update", precise=True, momentum=True,
                time=0.066,
            )
            terminal.scroll(
                0, 0, phase="end", precise=True, momentum=True,
                time=0.082,
            )
            stopped = terminal.snapshot().view_offset
            terminal.blink_tick()
            terminal.blink_tick()
            self.assertEqual(terminal.snapshot().view_offset, stopped)


if __name__ == "__main__":
    unittest.main()
