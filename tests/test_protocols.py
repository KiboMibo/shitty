import unittest

from harness import Zutty


class ProtocolTest(unittest.TestCase):
    def test_device_attributes_and_status_reports(self):
        with Zutty(columns=10, rows=3) as terminal:
            terminal.write(b"\x1b[2;4H\x1b[c\x1b[>c\x1b[5n\x1b[6n")
            self.assertEqual(
                terminal.read_input(),
                b"\x1b[?64;1;9;15;21;22c"
                b"\x1b[>64;0;0c"
                b"\x1b[0n"
                b"\x1b[2;4R",
            )

    def test_decrqss_reports_compatibility_level(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(b"\x1bP$q\"p\x1b\\")
            self.assertEqual(
                terminal.read_input(),
                b"\x1bP1$r64;1;9;15;21;22c\x1b\\",
            )

    def test_decrqss_rejects_unsupported_queries(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(b"\x1bP$qm\x1b\\")
            self.assertEqual(terminal.read_input(), b"\x1bP0$r\x1b\\")

    def test_palette_and_dynamic_color_queries_reply(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(b"\x1b]4;1;?\a\x1b]10;?\x1b\\")
            reply = terminal.read_input()
            self.assertIn(b"\x1b]4;1;rgb:", reply)
            self.assertIn(b"\x1b]10;rgb:", reply)

    def test_osc_title_cwd_and_bell_are_reported_as_actions(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(b"\x1b]2;hello\a\x1b]7;file:///tmp\x1b\\\a")
            self.assertEqual(
                terminal.read_actions(),
                ["OSC 2 68656c6c6f", "OSC 7 66696c653a2f2f2f746d70", "BELL"],
            )

    def test_osc8_hyperlinks_are_attached_and_resolved(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(
                b"\x1b]8;id=x;https://example.test\x1b\\"
                b"link"
                b"\x1b]8;;\x1b\\ plain"
            )
            snapshot = terminal.snapshot()
            self.assertNotEqual(snapshot.cell(0, 0).hyperlink, 0)
            self.assertEqual(snapshot.cell(5, 0).hyperlink, 0)
            self.assertEqual(terminal.hyperlink(1, 0), "https://example.test")

    def test_osc52_clipboard_request_is_forwarded(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(b"\x1b]52;c;SGVsbG8=\x1b\\")
            self.assertEqual(
                terminal.read_actions(),
                ["OSC 52 633b534756736247383d"],
            )


if __name__ == "__main__":
    unittest.main()
