# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

import configparser
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class BuildMetadataTests(unittest.TestCase):
    def test_libstd_submodule_uses_public_transport(self):
        modules = configparser.ConfigParser()
        modules.read(ROOT / ".gitmodules")
        libstd = modules["submodule \"third_party/libstd\""]
        self.assertEqual(libstd["path"], "third_party/libstd")
        self.assertEqual(libstd["url"], "https://github.com/pg83/std.git")

    def test_readme_initializes_bundled_dependencies(self):
        readme = (ROOT / "README.md").read_text()
        self.assertIn(
            "git submodule update --init --recursive",
            readme,
        )


if __name__ == "__main__":
    unittest.main()
