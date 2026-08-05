# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

import importlib.util
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "terminal_colors.py"


def load_generator():
    spec = importlib.util.spec_from_file_location("terminal_colors_generator", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class TerminalColorsDataTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.generator = load_generator()

    def test_catalog_is_normalized_and_has_the_whole_upstream_collection(self):
        catalog = self.generator.load_catalog()
        self.assertEqual(catalog["source"]["license"], "MIT")
        self.assertEqual(len(catalog["source"]["revision"]), 40)
        self.assertGreaterEqual(len(catalog["schemes"]), 500)
        self.assertTrue((ROOT / "LICENSE.iTerm2-Color-Schemes").is_file())

        names = [scheme["name"] for scheme in catalog["schemes"]]
        self.assertIn("3024 Night", names)
        self.assertIn("Nord", names)
        self.assertEqual(len(names), len({name.casefold() for name in names}))

    def test_cpp_table_generation_is_deterministic_and_complete(self):
        self.assertFalse((ROOT / "terminal_colors.json.h").exists())
        catalog = self.generator.load_catalog()
        first = self.generator.generate_header(catalog)
        second = self.generator.generate_header(catalog)
        self.assertEqual(first, second)
        self.assertEqual(
            first.count("    {\n        \""),
            len(catalog["schemes"]),
        )
        self.assertIn(
            "static constexpr TerminalColorScheme terminalColorSchemes[]",
            first,
        )


if __name__ == "__main__":
    unittest.main()
