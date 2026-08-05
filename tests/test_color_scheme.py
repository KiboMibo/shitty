# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

import tempfile
import unittest
from pathlib import Path

from harness import Shitty, run_startup_failure


class ColorSchemeTest(unittest.TestCase):
    def test_named_scheme_sets_defaults_and_ansi_palette(self):
        with Shitty(
            extra_arguments=("-colorScheme", "3024 Night")
        ) as terminal:
            options = terminal.options()
            self.assertEqual(options["fg"], 0xA5A2A2)
            self.assertEqual(options["bg"], 0x090300)
            self.assertEqual(options["cr"], options["fg"])

            terminal.write(b"\x1b]4;1;?\x1b\\")
            self.assertEqual(
                terminal.read_input(),
                b"\x1b]4;1;rgb:dbdb/2d2d/2020\x1b\\",
            )

    def test_explicit_colors_override_a_named_scheme(self):
        with Shitty(
            extra_arguments=(
                "-colorScheme", "3024 Night",
                "-fg", "#010203",
                "-color1", "#040506",
            )
        ) as terminal:
            self.assertEqual(terminal.options()["fg"], 0x010203)
            terminal.write(b"\x1b]4;1;?\x1b\\")
            self.assertEqual(
                terminal.read_input(),
                b"\x1b]4;1;rgb:0404/0505/0606\x1b\\",
            )

    def test_cli_scheme_replaces_lower_priority_config_colors(self):
        with tempfile.TemporaryDirectory() as directory:
            config = Path(directory) / "shitty.toml"
            config.write_text(
                'colorScheme = "Nord"\n'
                'fg = "#010203"\n'
                'color1 = "#040506"\n'
            )
            with Shitty(
                extra_arguments=(
                    "-config", config,
                    "-colorScheme", "3024 Night",
                )
            ) as terminal:
                self.assertEqual(terminal.options()["fg"], 0xA5A2A2)
                terminal.write(b"\x1b]4;1;?\x1b\\")
                self.assertEqual(
                    terminal.read_input(),
                    b"\x1b]4;1;rgb:dbdb/2d2d/2020\x1b\\",
                )

    def test_scheme_names_are_case_insensitive_and_listed(self):
        with Shitty(
            extra_arguments=("-colorScheme", "3024 night")
        ) as terminal:
            self.assertEqual(terminal.options()["bg"], 0x090300)

        result = run_startup_failure(extra_arguments=("-listColorSchemes",))
        self.assertEqual(result.returncode, 0)
        names = result.stdout.decode().splitlines()
        self.assertGreaterEqual(len(names), 500)
        self.assertIn("3024 Night", names)
        self.assertIn("Nord", names)

    def test_unknown_named_scheme_fails_with_discovery_hint(self):
        result = run_startup_failure(
            extra_arguments=("-colorScheme", "no such scheme")
        )
        self.assertEqual(result.returncode, 255)
        self.assertIn(b"unknown scheme", result.stdout)
        self.assertIn(b"-listColorSchemes", result.stdout)

    def test_private_dsr_reports_dark_and_light_configured_schemes(self):
        cases = (
            ((), b"\x1b[?997;1n"),
            (("-bg", "#ffffff"), b"\x1b[?997;2n"),
        )
        for arguments, expected in cases:
            with self.subTest(arguments=arguments):
                with Shitty(extra_arguments=arguments) as terminal:
                    terminal.write(b"\x1b[?996n")
                    self.assertEqual(terminal.read_input(), expected)

    def test_application_dynamic_colors_do_not_change_reported_scheme(self):
        with Shitty() as terminal:
            terminal.write(
                b"\x1b[?2031h"
                b"\x1b]10;#000000\x1b\\"
                b"\x1b]11;#ffffff\x1b\\"
            )
            self.assertEqual(terminal.read_input(), b"")

            terminal.write(b"\x1b[?996n")
            self.assertEqual(terminal.read_input(), b"\x1b[?997;1n")

    def test_color_scheme_reply_uses_selected_control_width(self):
        with Shitty() as terminal:
            terminal.write(b"\x1b G\x1b[?996n")
            self.assertEqual(terminal.read_input(), b"\x9b?997;1n")


if __name__ == "__main__":
    unittest.main()
