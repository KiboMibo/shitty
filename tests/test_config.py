# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

import tempfile
import unittest
from pathlib import Path

from harness import Shitty, run_startup_failure


def config_home(directory, text):
    home = Path(directory) / "shitty"
    home.mkdir(parents=True)
    (home / "shitty.toml").write_text(text)
    return directory


class ConfigFileTest(unittest.TestCase):
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

    def test_font_list_is_accepted(self):
        text = 'font = ["Test Font", "Fallback Font"]\nfontsize = 21\n'
        with tempfile.TemporaryDirectory() as directory:
            config_home(directory, text)
            environment = {"XDG_CONFIG_HOME": directory}
            with Shitty(extra_environment=environment) as terminal:
                self.assertEqual(terminal.font_state()[0], 21)


if __name__ == "__main__":
    unittest.main()
