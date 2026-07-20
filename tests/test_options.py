import unittest

from harness import Zutty, run_startup_failure


class OptionTest(unittest.TestCase):
    def test_font_size_accepts_inclusive_one_and_255_boundaries(self):
        for value in (1, 255):
            with self.subTest(source="cli", value=value):
                with Zutty(
                    extra_arguments=("-fontsize", str(value))
                ) as terminal:
                    self.assertEqual(terminal.options()["fontsize"], value)
            with self.subTest(source="env", value=value):
                with Zutty(font_size_env=str(value)) as terminal:
                    self.assertEqual(terminal.options()["fontsize"], value)

    def test_font_size_rejects_values_outside_byte_range(self):
        for value in ("0", "256", "-1"):
            with self.subTest(source="cli", value=value):
                result = run_startup_failure(
                    extra_arguments=("-fontsize", value)
                )
                self.assertEqual(result.returncode, 255)
                self.assertIn(b"expected integer within 1..255", result.stdout)
            with self.subTest(source="env", value=value):
                result = run_startup_failure(font_size_env=value)
                self.assertEqual(result.returncode, 255)
                self.assertIn(b"expected integer within 1..255", result.stdout)

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
