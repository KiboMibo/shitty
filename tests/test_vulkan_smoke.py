# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

import os
import sys
import unittest

from harness import Shitty


REQUIRED = os.environ.get("SHITTY_TEST_VULKAN_REQUIRED") == "1"
VULKAN_ENVIRONMENT = {"SHITTY_TEST_VULKAN": "1"}


@unittest.skipUnless(sys.platform.startswith("linux"), "Vulkan shadow is Linux-only")
class VulkanSmokeTest(unittest.TestCase):
    # The Vulkan renderer runs the real acquire/submit/present cycle over
    # a VK_EXT_headless_surface swapchain (lavapipe in CI), shadowing the
    # reference renderer frame for frame. This is the coverage that was
    # missing when PR 62's crash - a FontFaceMiss unwinding after frame
    # acquisition - shipped unnoticed.
    def shadowed(self, **kwargs):
        terminal = Shitty(extra_environment=VULKAN_ENVIRONMENT, **kwargs)
        if not terminal.vulkan_shadow():
            terminal.close()
            if REQUIRED:
                self.fail("vulkan shadow required but unavailable")
            self.skipTest("no vulkan device for the headless surface")
        return terminal

    def test_plain_frames_present(self):
        with self.shadowed(columns=60, rows=16) as terminal:
            terminal.write(b"hello vulkan\r\nsecond line")
            terminal.present()
            snapshot = terminal.snapshot()
        self.assertEqual(snapshot.lines[0].rstrip(), "hello vulkan")

    def test_uncovered_script_flood_presents(self):
        # PR 62's reproducer: every fresh cluster unwinds the frame after
        # the miss, and the retry must never corrupt the swapchain state.
        line = "हिन्दी বাংলা ద్రావిడ " * 4
        with self.shadowed(columns=90, rows=20, save_lines=100) as terminal:
            for _ in range(60):
                terminal.write((line + "\r\n").encode())
                terminal.present()
            terminal.write(b"\x1b[2J\x1b[HDONE marker")
            terminal.present()
            snapshot = terminal.snapshot()
        self.assertEqual(snapshot.lines[0].rstrip()[:11], "DONE marker")

    def test_resize_and_font_change_survive(self):
        with self.shadowed(columns=40, rows=10, glyph_px=8, glyph_py=16) as terminal:
            terminal.write(b"before")
            terminal.present()
            terminal.resize(64, 18)
            terminal.write(b" after resize")
            terminal.present()
            snapshot = terminal.snapshot()
        self.assertIn("after resize", snapshot.lines[0])


if __name__ == "__main__":
    unittest.main()
