import unittest

from harness import Zutty


class OptionTest(unittest.TestCase):
    def test_font_size_source_priority_is_cli_then_env_then_default(self):
        with Zutty(font_size_env=None) as terminal:
            self.assertEqual(terminal.options()["fontsize"], 16)

        with Zutty(font_size_env="23") as terminal:
            self.assertEqual(terminal.options()["fontsize"], 23)

        with Zutty(
            font_size_env="23",
            extra_arguments=("-fontsize", "31"),
        ) as terminal:
            self.assertEqual(terminal.options()["fontsize"], 31)


if __name__ == "__main__":
    unittest.main()
