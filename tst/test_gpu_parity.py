# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

import os
import unittest

from harness import Shitty


REQUIRED = os.environ.get("SHITTY_TEST_VULKAN_REQUIRED") == "1"
# The variable arms whichever GPU backend the build has - Vulkan over a
# headless surface on Linux, Metal into a texture on macOS. Its name, and
# the VULKAN_ control commands the harness speaks, are older than the
# second backend; tst/harness.py is the file that would have to be
# renamed with them.
SHADOW_ENVIRONMENT = {
    "SHITTY_TEST_VULKAN": "1",
    # Exercise fractional coverage and the float push-constant field, not
    # only the one-pixel fallback used by the headless fontpack.
    "SHITTY_TEST_BOX_STROKE": "1.5",
}

SCENES = (
    (
        "text and attributes",
        "\x1b[?25lplain \x1b[1mbold\x1b[0m \x1b[4munder\x1b[0m"
        "\x1b[2;1H\x1b[7minverse\x1b[0m \x1b[31mred on \x1b[44mblue\x1b[0m",
    ),
    (
        "block mosaic",
        "\x1b[?25l\x1b[48;5;16m\x1b[38;2;255;80;0m"
        "\x1b[1;1H██▀▄▌▐\x1b[2;1H▖▗▘▝▚▞\x1b[3;1H░▒▓▁▏▕",
    ),
    (
        "box drawing",
        "\x1b[?25l"
        "\x1b[1;1H─━┄┅┈┉╌╍"
        "\x1b[2;1H┌┯┓╞╪╡"
        "\x1b[3;1H╔═╦═╗╠╬╣"
        "\x1b[4;1H╭─╮╱╲╳"
        "\x1b[5;1H╴╵╶╷╸╹╺╻╼╽╾╿"
        "\x1b[6;1H⎺⎻⎼⎽⎾⎿⏋⏌",
    ),
    (
        "cursor over content",
        "\x1b[38;5;120mcursor here\x1b[1;3H",
    ),
    (
        "sixel patches",
        "\x1b[?25l\x1b[44mtext"
        "\x1bPq#1;2;100;0;0#2;2;0;100;0#1!20~$!10?!10F-#2!13N\x1b\\",
    ),
)


class GpuParityTest(unittest.TestCase):
    # The reference renderer and the compute shader implement one visual
    # contract twice - most recently the synthesized box/block coverage.
    # This drives both over identical frames and compares the pixels:
    # integer blending on the CPU against float blending on the GPU
    # differs by at most a rounding step per channel.
    #
    # What decides whether this runs is whether the build made a shadow
    # renderer, not which platform it is on: the class was skipped
    # outside Linux for as long as Vulkan was the only backend that could
    # draw into something readable, and skipping by platform kept it
    # skipped on macOS after Metal could. The shadow reports itself
    # below, and a build without one still skips - unless the
    # environment demands it, which is what CI does.
    TOLERANCE = 3

    def compare(self, name, text):
        with Shitty(
            columns=20,
            rows=6,
            glyph_px=8,
            glyph_py=16,
            extra_environment=SHADOW_ENVIRONMENT,
        ) as terminal:
            if not terminal.vulkan_shadow():
                if REQUIRED:
                    self.fail("gpu shadow required but unavailable")
                self.skipTest("no gpu shadow renderer in this build")
            terminal.write(text.encode())
            terminal.present()
            reference = terminal.reference_image()
            gpu = terminal.vulkan_image()
        self.assertEqual(reference[:2], gpu[:2], f"{name}: image sizes differ")
        worst = 0
        offenders = 0
        for index in range(len(reference[2])):
            delta = abs(reference[2][index] - gpu[2][index])
            worst = max(worst, delta)
            if delta > self.TOLERANCE:
                offenders += 1
        self.assertEqual(
            offenders,
            0,
            f"{name}: {offenders} channels differ by more than "
            f"{self.TOLERANCE} (worst {worst})",
        )

    def test_scene_parity(self):
        for name, text in SCENES:
            with self.subTest(scene=name):
                self.compare(name, text)


if __name__ == "__main__":
    unittest.main()
