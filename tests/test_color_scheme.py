import unittest

from harness import Zutty


class ColorSchemeTest(unittest.TestCase):
    def test_private_dsr_reports_dark_and_light_configured_schemes(self):
        cases = (
            ((), b"\x1b[?997;1n"),
            (("-bg", "#ffffff"), b"\x1b[?997;2n"),
        )
        for arguments, expected in cases:
            with self.subTest(arguments=arguments):
                with Zutty(extra_arguments=arguments) as terminal:
                    terminal.write(b"\x1b[?996n")
                    self.assertEqual(terminal.read_input(), expected)

    def test_application_dynamic_colors_do_not_change_reported_scheme(self):
        with Zutty() as terminal:
            terminal.write(
                b"\x1b[?2031h"
                b"\x1b]10;#000000\x1b\\"
                b"\x1b]11;#ffffff\x1b\\"
            )
            self.assertEqual(terminal.read_input(), b"")

            terminal.write(b"\x1b[?996n")
            self.assertEqual(terminal.read_input(), b"\x1b[?997;1n")

    def test_color_scheme_reply_uses_selected_control_width(self):
        with Zutty() as terminal:
            terminal.write(b"\x1b G\x1b[?996n")
            self.assertEqual(terminal.read_input(), b"\x9b?997;1n")


if __name__ == "__main__":
    unittest.main()
