# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

import configparser
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class BuildMetadataTests(unittest.TestCase):
    def run_build_with_descr(self, descr):
        with tempfile.TemporaryDirectory() as directory:
            build_file = Path(directory) / "build.py"
            build_file.write_text(
                "target = command(\n"
                "    outputs=['$(B)/result'],\n"
                "    cmd=['true'],\n"
                f"    descr={descr!r},\n"
                ")\n"
                "install(target)\n"
            )
            return subprocess.run(
                [
                    ROOT / "build",
                    "--build-file",
                    build_file,
                    "--list",
                ],
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )

    def test_build_accepts_exactly_two_ascii_letters_in_descr(self):
        result = self.run_build_with_descr("OK")

        self.assertEqual(result.returncode, 0, result.stderr)

    def test_build_rejects_every_other_descr_shape(self):
        for descr in ("A", "ABC", "A1", "A ", "ÄB"):
            with self.subTest(descr=descr):
                result = self.run_build_with_descr(descr)

                self.assertNotEqual(result.returncode, 0)
                self.assertIn(
                    "descr must be exactly two ASCII letters",
                    result.stderr,
                )

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
