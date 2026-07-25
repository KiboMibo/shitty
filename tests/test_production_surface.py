# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

import os
import subprocess
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PRODUCTION_BINARY = Path(
    os.environ.get("SHITTY_PRODUCTION_BINARY", ROOT / "st")
)
TEST_VERSION = os.environ.get("SHITTY_TEST_VERSION")


class ProductionSurfaceTest(unittest.TestCase):
    def test_control_protocol_is_absent_from_production_binary(self):
        binary = PRODUCTION_BINARY.read_bytes()
        for marker in (
            b"--test-fd",
            b"SHITTY_TEST_GLYPH",
            b"test control write failed",
            b"FRONTEND_KEY_EVENT ",
            b"MODEL_SNAPSHOT",
        ):
            with self.subTest(marker=marker):
                self.assertNotIn(marker, binary)

    def test_production_binary_rejects_test_control_option(self):
        result = subprocess.run(
            [str(PRODUCTION_BINARY), "--test-fd", "1", "-help"],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=5,
            check=False,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"unknown option: --test-fd", result.stderr)

    def test_version_probe_exits_before_window_startup(self):
        expected = (
            f"Shitty {TEST_VERSION}\n"
            "Copyright (C) 2026 Shitty team\n"
        ).encode()
        for argument in ("-v", "-version"):
            with self.subTest(argument=argument):
                result = subprocess.run(
                    [str(PRODUCTION_BINARY), argument],
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    timeout=5,
                    check=False,
                )
                self.assertEqual(result.returncode, 0)
                self.assertEqual(result.stdout, expected)
                self.assertEqual(result.stderr, b"")


if __name__ == "__main__":
    unittest.main()
