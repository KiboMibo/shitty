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
# What a GPU backend answers when it is handed a frame of more than one
# pane and cannot draw one. RendererImpl::update() in render_vk.cpp
# raises it; the harness turns the control reply into a RuntimeError
# carrying these words. Matching the sentence is how the split test asks
# the backend what it can draw - see SplitGpuParityTest.
SPLIT_REFUSAL = "splits are not supported here yet"

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


class SplitGpuParityTest(unittest.TestCase):
    # F3, from R3-qa's finding. Every scene above is one pane on the
    # whole surface, and until MirrorRenderer learned the multi-pane
    # form of update() no test could be anything else: the shadow fell
    # through to the Renderer default, which refuses a frame wider than
    # one pane by construction. So the two backends had never drawn the
    # same split frame, let alone been compared on one - and a split
    # frame is where they differ most, because each pane is cleared and
    # clipped on its own and the seam between them is painted by a path
    # a single pane never takes.
    #
    # Point 4 of the diagnosis asks for exactly this comparison. The
    # tolerance is the one above and for the same reason: integer
    # blending on the CPU against float blending on the GPU.
    #
    # Not every GPU backend can make this picture at all. Metal grew the
    # per-pane cell ranges and draws splits; Vulkan refuses a multi-pane
    # frame outright, and says so - it draws incrementally from one
    # damage journal over one grid of retained cells, and two panes need
    # a journal and a cell range each (render_vk.cpp,
    # RendererImpl::update, and the report named below). That is a
    # feature this backend has never had, not a break in one it has.
    #
    # So the comparison stands aside where the backend cannot draw the
    # frame - and it finds that out by building one and reading the
    # answer, which asks the backend rather than the platform. The
    # platform is the wrong question: which backend a build has follows
    # the build (see the note on SHADOW_ENVIRONMENT), and skipping by
    # platform is what test_gpu_smoke.py spent two commits undoing. Only
    # that one refusal counts as the known gap; a split frame that fails
    # any other way is a regression and is left to fail.
    #
    # The gap is carried as work rather than as a silence - what the
    # Vulkan backend is missing, and how much of it, is written down in
    # docs/reports/G9-split-parity-2026-09-03.md. When that work lands
    # the comparison starts running there on its own, with nothing here
    # to remove.
    TOLERANCE = GpuParityTest.TOLERANCE

    def split_frame(self):
        """The reference and GPU pictures of one two-pane frame.

        Skips the calling test where the GPU backend answers that it
        cannot present a multi-pane frame.
        """
        with Shitty(
            columns=20,
            rows=6,
            glyph_px=8,
            glyph_py=16,
            extra_arguments=("-panes",),
            extra_environment=SHADOW_ENVIRONMENT,
        ) as terminal:
            if not terminal.vulkan_shadow():
                if REQUIRED:
                    self.fail("gpu shadow required but unavailable")
                self.skipTest("no gpu shadow renderer in this build")
            try:
                terminal.split("V")
                self.assertEqual(terminal.pane_count(), 2)
                # Both panes written to, and differently: a frame where
                # one pane is blank would pass on a backend that drew
                # the seam in the wrong place.
                #
                # The whole sequence is guarded, not just present(): the
                # refusal lands on whichever command first drives a
                # frame through the renderer, which in CI's Vulkan run
                # is already the first write (33692909258).
                terminal.focus_pane(0)
                terminal.write(b"\x1b[?25l\x1b[31mLEFT\x1b[2;1H\x1b[44mblue\x1b[0m")
                terminal.focus_pane(1)
                terminal.write(b"\x1b[?25l\x1b[32mRIGHT\x1b[2;1H\x1b[7minv\x1b[0m")
                terminal.present()
                return terminal.reference_image(), terminal.vulkan_image()
            except RuntimeError as refusal:
                if SPLIT_REFUSAL not in str(refusal):
                    raise
                # Reported in the backend's own words, so the run says
                # which backend stood aside and why. REQUIRED is not
                # consulted here: it asks for a shadow renderer, not for
                # a particular set of frames from one.
                self.skipTest(str(refusal))

    def test_a_split_frame_is_the_same_picture_on_both_backends(self):
        reference, gpu = self.split_frame()
        self.assertEqual(reference[:2], gpu[:2], "split: image sizes differ")
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
            f"split: {offenders} channels differ by more than "
            f"{self.TOLERANCE} (worst {worst})",
        )


if __name__ == "__main__":
    unittest.main()
