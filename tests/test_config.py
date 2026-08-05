# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

import re
import tempfile
import unittest
from pathlib import Path

from harness import ROOT, Shitty, run_startup_failure


EXAMPLE_CONFIG = ROOT / "shitty.toml"


def config_home(directory, text):
    home = Path(directory) / "shitty"
    home.mkdir(parents=True)
    (home / "shitty.toml").write_text(text)
    return directory


class ConfigFileTest(unittest.TestCase):
    def test_example_config_is_accepted_by_the_application(self):
        result = run_startup_failure(
            extra_arguments=("-config", EXAMPLE_CONFIG, "-version")
        )
        self.assertEqual(result.returncode, 0)
        self.assertEqual(result.stderr, b"")

        with Shitty(extra_arguments=("-config", EXAMPLE_CONFIG)) as terminal:
            options = terminal.options()
            self.assertEqual(options["fontsize"], 15)
            self.assertEqual(options["fg"], 0xD8DEE9)
            self.assertEqual(options["bg"], 0x2E3440)
            self.assertEqual(options["cr"], 0x88C0D0)

    def test_example_config_documents_every_public_cli_option(self):
        listed = set()
        for argument in ("-help", "-listres"):
            result = run_startup_failure(extra_arguments=(argument,))
            self.assertEqual(result.returncode, 0)
            listed.update(
                match.decode()
                for match in re.findall(rb"^  -([^ ]+)", result.stdout, re.MULTILINE)
            )

        documented = {}
        for line in EXAMPLE_CONFIG.read_text().splitlines():
            if not line.startswith("# CLI: -"):
                continue
            syntax, separator, description = line.partition(" — ")
            self.assertEqual(separator, " — ", line)
            self.assertTrue(description, line)
            name = syntax.removeprefix("# CLI: -").split()[0]
            self.assertNotIn(name, documented, f"duplicate documentation for -{name}")
            documented[name] = description

        self.assertSetEqual(set(documented), listed)

    def test_option_comes_from_the_default_config_path(self):
        with tempfile.TemporaryDirectory() as directory:
            config_home(directory, "fontsize = 33\n")
            environment = {"XDG_CONFIG_HOME": directory}
            with Shitty(extra_environment=environment) as terminal:
                self.assertEqual(terminal.font_state()[0], 33)

    def test_command_line_beats_the_config(self):
        with tempfile.TemporaryDirectory() as directory:
            config_home(directory, "fontsize = 33\n")
            environment = {"XDG_CONFIG_HOME": directory}
            arguments = ("-fontsize", "22")
            with Shitty(extra_environment=environment, extra_arguments=arguments) as terminal:
                self.assertEqual(terminal.font_state()[0], 22)

    def test_explicit_config_path_is_honored(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "own.toml"
            path.write_text("fontsize = 44\n")
            with Shitty(extra_arguments=("-config", path)) as terminal:
                self.assertEqual(terminal.font_state()[0], 44)

    def test_missing_explicit_config_fails_startup(self):
        result = run_startup_failure(extra_arguments=("-config", "/nonexistent/st.toml"))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"-config", result.stdout + result.stderr)

    def test_broken_config_warns_but_the_terminal_starts(self):
        with tempfile.TemporaryDirectory() as directory:
            config_home(directory, "fontsize = 33\nboldColors = ???\n")
            environment = {"XDG_CONFIG_HOME": directory}
            with Shitty(extra_environment=environment) as terminal:
                self.assertEqual(terminal.font_state()[0], 33)

    def test_unknown_keys_and_tables_are_ignored(self):
        text = "nonsense = 1\n[section]\nx = 2\n"
        with tempfile.TemporaryDirectory() as directory:
            config_home(directory, "fontsize = 33\n" + text)
            environment = {"XDG_CONFIG_HOME": directory}
            with Shitty(extra_environment=environment) as terminal:
                self.assertEqual(terminal.font_state()[0], 33)

    def test_typed_values_parse_alongside_the_applied_one(self):
        text = "boldColors = false\nborder = 7\ntitle = 'from config'\nfontsize = 27\n"
        with tempfile.TemporaryDirectory() as directory:
            config_home(directory, text)
            environment = {"XDG_CONFIG_HOME": directory}
            with Shitty(extra_environment=environment) as terminal:
                self.assertEqual(terminal.font_state()[0], 27)

    def test_environment_expands_in_the_config_body(self):
        text = "fontsize = ${SHITTY_TEST_WANTED_SIZE}\ntitle = '${HOME} of ${NO_SUCH_VARIABLE}'\n"
        with tempfile.TemporaryDirectory() as directory:
            config_home(directory, text)
            environment = {
                "XDG_CONFIG_HOME": directory,
                "SHITTY_TEST_WANTED_SIZE": "31",
            }
            with Shitty(extra_environment=environment) as terminal:
                self.assertEqual(terminal.font_state()[0], 31)

    def test_font_list_is_accepted(self):
        text = 'font = ["Test Font", "Fallback Font"]\nfontsize = 21\n'
        with tempfile.TemporaryDirectory() as directory:
            config_home(directory, text)
            environment = {"XDG_CONFIG_HOME": directory}
            with Shitty(extra_environment=environment) as terminal:
                self.assertEqual(terminal.font_state()[0], 21)

    def test_uri_scheme_list_comes_from_the_config(self):
        text = 'uriScheme = ["gemini"]\n'
        control = 2
        with tempfile.TemporaryDirectory() as directory:
            config_home(directory, text)
            environment = {"XDG_CONFIG_HOME": directory}
            with Shitty(columns=64, rows=1, extra_environment=environment) as terminal:
                uri = b"gemini://example.test"
                terminal.write(uri + b" https://example.test")
                terminal.pointer(2 + 4, 2, modifiers=control)
                self.assertEqual(terminal.desktop_state()["icon"], 1)
                terminal.pointer(2 + len(uri) + 5, 2, modifiers=control)
                self.assertEqual(terminal.desktop_state()["icon"], 0)


if __name__ == "__main__":
    unittest.main()
