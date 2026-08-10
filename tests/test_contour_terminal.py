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
)


class ContourTerminalTest(unittest.TestCase):
    def test_upstream_inventory_has_first_9_cases(self):
        self.assertEqual(len(UPSTREAM_CASES), 9)
        self.assertEqual(len(set(UPSTREAM_CASES)), 9)

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


if __name__ == "__main__":
    unittest.main()
