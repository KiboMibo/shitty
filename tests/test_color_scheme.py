# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

import tempfile
import unittest
from pathlib import Path

from harness import PRETTY, Shitty, run_startup_failure


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
        self.assertEqual(names[0], "default")
        self.assertIn("classic", names)
        self.assertIn("3024 Night", names)
        self.assertIn("Nord", names)

    def test_brand_default_scheme_tints_vga_toward_the_accent(self):
        # The Shitty brand: VGA pulled toward the logo amber #ffb000 at
        # the compile-time tint 35. Foreground, background, and cursor
        # stay plain white on black at any tint.
        with Shitty(tint=None) as terminal:
            options = terminal.options()
            self.assertEqual(options["fg"], 0xFFFFFF)
            self.assertEqual(options["bg"], 0x000000)
            self.assertEqual(options["cr"], options["fg"])

            terminal.write(b"\x1b]4;1;?;3;?\x1b\\")
            self.assertEqual(
                terminal.read_input(),
                b"\x1b]4;1;rgb:a8a8/4646/0606\x1b\\"
                b"\x1b]4;3;rgb:abab/6d6d/3030\x1b\\",
            )

    def test_tint_slider_spans_plain_vga_to_full_sepia(self):
        for tint, red in (("0", b"aaaa/0000/0000"), ("100", b"7777/5050/0000")):
            with self.subTest(tint=tint):
                with Shitty(tint=tint) as terminal:
                    options = terminal.options()
                    self.assertEqual(options["fg"], 0xFFFFFF)
                    self.assertEqual(options["bg"], 0x000000)
                    terminal.write(b"\x1b]4;1;?\x1b\\")
                    self.assertEqual(
                        terminal.read_input(),
                        b"\x1b]4;1;rgb:" + red + b"\x1b\\",
                    )

    def test_default_scheme_name_is_case_insensitive(self):
        with Shitty(
            tint=None, extra_arguments=("-colorScheme", "DEFAULT")
        ) as terminal:
            terminal.write(b"\x1b]4;1;?\x1b\\")
            self.assertEqual(
                terminal.read_input(),
                b"\x1b]4;1;rgb:a8a8/4646/0606\x1b\\",
            )

    def test_classic_scheme_restores_the_pre_brand_defaults(self):
        with Shitty(
            tint=None, extra_arguments=("-colorScheme", "classic")
        ) as terminal:
            options = terminal.options()
            self.assertEqual(options["fg"], 0xFFFFFF)
            self.assertEqual(options["bg"], 0x000000)
            terminal.write(b"\x1b]4;1;?;12;?\x1b\\")
            self.assertEqual(
                terminal.read_input(),
                b"\x1b]4;1;rgb:cdcd/0000/0000\x1b\\"
                b"\x1b]4;12;rgb:5c5c/5c5c/ffff\x1b\\",
            )

    def test_explicit_colors_override_the_brand_default_scheme(self):
        with Shitty(
            tint=None, extra_arguments=("-fg", "#010203", "-color1", "#040506")
        ) as terminal:
            self.assertEqual(terminal.options()["fg"], 0x010203)
            self.assertEqual(terminal.options()["bg"], 0x000000)
            terminal.write(b"\x1b]4;1;?\x1b\\")
            self.assertEqual(
                terminal.read_input(),
                b"\x1b]4;1;rgb:0404/0505/0606\x1b\\",
            )

    def test_config_colors_override_the_brand_default_scheme(self):
        with tempfile.TemporaryDirectory() as directory:
            config = Path(directory) / "shitty.toml"
            config.write_text(
                'tint = "35"\n'
                'color1 = "#010203"\n'
            )
            with Shitty(
                tint=None, extra_arguments=("-config", config)
            ) as terminal:
                terminal.write(b"\x1b]4;1;?;3;?\x1b\\")
                self.assertEqual(
                    terminal.read_input(),
                    b"\x1b]4;1;rgb:0101/0202/0303\x1b\\"
                    b"\x1b]4;3;rgb:abab/6d6d/3030\x1b\\",
                )

    def test_pretty_brand_leans_toward_its_own_accent(self):
        with Shitty(tint=None, binary=PRETTY) as terminal:
            options = terminal.options()
            self.assertEqual(options["fg"], 0xFFFFFF)
            self.assertEqual(options["bg"], 0x000000)
            terminal.write(b"\x1b]4;1;?\x1b\\")
            self.assertEqual(
                terminal.read_input(),
                b"\x1b]4;1;rgb:a4a4/4141/5b5b\x1b\\",
            )

    def test_invalid_tint_fails_startup(self):
        for tint in ("101", "-5", "0.5", "poo"):
            with self.subTest(tint=tint):
                result = run_startup_failure(
                    extra_arguments=("-tint", tint)
                )
                self.assertEqual(result.returncode, 255)
                self.assertIn(b"-tint", result.stdout)

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
