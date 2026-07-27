# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

import unittest

from harness import Shitty


class OscProtocolTest(unittest.TestCase):
    def test_hyperlink_control_preserves_arbitrary_uri_bytes(self):
        with Shitty(columns=8, rows=2) as terminal:
            terminal.write(b"\x1b]8;;https://example/\xc2x\x1b\\X")
            self.assertEqual(
                terminal.hyperlink_bytes(0, 0),
                b"https://example/\xc2x",
            )

    def test_osc52_selectors(self):
        cases = {
            b";WA==": (True, True),
            b"s;WA==": (True, False),
            b"p;WA==": (True, False),
            b"c;WA==": (False, True),
            b"c;WA": (False, True),
            b"pc;WA==": (True, True),
            b"q;WA==": (False, False),
        }
        with Shitty(columns=8, rows=2) as terminal:
            for argument, destinations in cases.items():
                with self.subTest(argument=argument):
                    terminal.set_primary_selection(b"old-primary")
                    terminal.set_system_clipboard(b"old-clipboard")
                    terminal.write(b"\x1b]52;" + argument + b"\x1b\\")
                    self.assertEqual(
                        terminal.get_selection(primary=True),
                        b"X" if destinations[0] else b"old-primary",
                    )
                    self.assertEqual(
                        terminal.get_selection(primary=False),
                        b"X" if destinations[1] else b"old-clipboard",
                    )

    def test_osc52_query_and_malformed_request(self):
        with Shitty(columns=8, rows=2) as terminal:
            terminal.set_system_clipboard(b"old")
            terminal.write(b"\x1b]52;c;?\x1b\\")
            self.assertEqual(
                terminal.read_input(),
                b"\x1b]52;c;\x1b\\",
            )
            terminal.write(b"\x1b]52;missing-separator\x1b\\")
            terminal.write(b"\x1b]52;c;SGVsbG8!\x1b\\")
            self.assertEqual(terminal.get_selection(primary=False), b"old")
            self.assertEqual(terminal.read_input(), b"")

    def test_osc52_reply_encodes_arbitrary_bytes(self):
        with Shitty(
            columns=8,
            rows=2,
            extra_arguments=("-allowOsc52Read", "true"),
        ) as terminal:
            terminal.set_primary_selection(b"hello\x00world")
            terminal.write(b"\x1b]52;;?\x1b\\")
            self.assertEqual(
                terminal.read_input(),
                b"\x1b]52;s0;aGVsbG8Ad29ybGQ=\x1b\\",
            )
            terminal.set_system_clipboard(b"clipboard")
            terminal.write(b"\x1b]52;c;?\x1b\\")
            self.assertEqual(
                terminal.read_input(),
                b"\x1b]52;c;Y2xpcGJvYXJk\x1b\\",
            )

    def test_osc7_file_url_and_percent_decoding(self):
        with Shitty(columns=8, rows=2) as terminal:
            self.assertEqual(
                terminal.osc7_cwd(b"file://host/tmp/a%20b%2Fc"),
                b"/tmp/a b/c",
            )
            self.assertEqual(terminal.osc7_cwd(b"/plain/path"), b"/plain/path")

    def test_osc7_rejects_non_absolute_paths(self):
        with Shitty(columns=8, rows=2) as terminal:
            self.assertEqual(terminal.osc7_cwd(b"relative/path"), b"")
            self.assertEqual(terminal.osc7_cwd(b"file://host"), b"")


if __name__ == "__main__":
    unittest.main()
