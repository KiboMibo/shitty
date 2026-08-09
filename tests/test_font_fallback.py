# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

import tempfile
import unittest
from pathlib import Path

from font_fixture import NERD_FONT, make_box_font, make_font
from test_font_resolver import FontResolverTest
from harness import Shitty, TEST_PLATFORM


def has_ink(pixels):
    return any(max(pixels[offset:offset + 3]) > 32 for offset in range(0, len(pixels), 3))


def chromatic_colors(pixels):
    return {
        pixels[offset:offset + 3]
        for offset in range(0, len(pixels), 3)
        if max(pixels[offset:offset + 3]) != min(pixels[offset:offset + 3])
    }


class FontFallbackTest(unittest.TestCase):
    def test_emoji_renders_in_color_out_of_the_box(self):
        with Shitty(columns=2, rows=1) as terminal:
            terminal.write("\x1b[?25l😀".encode())
            width, height, pixels = terminal.render_image("monospace")
        self.assertGreater(len(chromatic_colors(pixels)), 16)

    def test_zwj_cluster_renders_through_one_fallback_face(self):
        with Shitty(columns=2, rows=1) as terminal:
            terminal.write("\x1b[?25l👩‍💻".encode())
            width, height, pixels = terminal.render_image("monospace")
        self.assertGreater(len(chromatic_colors(pixels)), 16)

    def render_with_fixture(self, text):
        # A primary font that covers only "M" and U+3000, with blank
        # outlines: anything else must come from the embedded fallbacks.
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            font = root / "fixture.ttf"
            font.write_bytes(
                make_font("Shitty Coverage Fixture", 500, 1000, 1000)
            )
            config = FontResolverTest.write_fontconfig(root)
            with Shitty(
                columns=4,
                rows=1,
                extra_environment={"FONTCONFIG_FILE": str(config)},
            ) as terminal:
                terminal.write(("\x1b[?25l" + text).encode())
                return terminal.render_image("Shitty Coverage Fixture")

    def test_narrow_codepoint_falls_back_by_coverage(self):
        width, height, pixels = self.render_with_fixture("W")
        self.assertTrue(has_ink(pixels))

    def test_codepoint_covered_nowhere_draws_a_box(self):
        width, height, pixels = self.render_with_fixture("͸")
        self.assertTrue(has_ink(pixels))


def ink_weight(pixels):
    return sum(
        max(pixels[offset:offset + 3])
        for offset in range(0, len(pixels), 3)
    )


class SyntheticStyleTest(unittest.TestCase):
    def test_single_face_family_synthesizes_every_style(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            font = root / "single.ttf"
            font.write_bytes(make_font("Shitty Single Face", 500, 1000, 1000))
            config = FontResolverTest.write_fontconfig(root)
            with Shitty(
                extra_environment={"FONTCONFIG_FILE": str(config)}
            ) as terminal:
                loaded = terminal.load_font("Shitty Single Face")
        self.assertEqual(
            (loaded["bold"], loaded["italic"], loaded["bold_italic"]),
            (1, 1, 1),
        )

    def test_synthetic_bold_adds_ink(self):
        # A path-loaded font has only its regular face, so bold must be
        # synthesized by emboldening.
        renders = {}
        for name, escape in (("regular", b""), ("bold", b"\x1b[1m")):
            with Shitty(columns=4, rows=1) as terminal:
                terminal.write(b"\x1b[?25l" + escape + b"WWWW")
                renders[name] = terminal.render_image(NERD_FONT)[2]
        self.assertGreater(
            ink_weight(renders["bold"]),
            ink_weight(renders["regular"]) * 1.05,
        )


def render_system_fallback_cell(text, with_fontconfig_fallback=True):
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        (root / "primary.ttf").write_bytes(
            make_font("Shitty Coverage Fixture", 500, 1000, 1000)
        )
        if with_fontconfig_fallback:
            (root / "extra.ttf").write_bytes(
                make_box_font("Shitty System Extra", 500, (0x0E01, ord("W")))
            )
        config = FontResolverTest.write_fontconfig(root)
        with Shitty(
            columns=4,
            rows=1,
            extra_environment={"FONTCONFIG_FILE": str(config)},
        ) as terminal:
            terminal.write(("\x1b[?25l" + text).encode())
            metrics = terminal.load_font("Shitty Coverage Fixture")
            border = terminal.options()["border"]
            width, height, pixels = terminal.render_image(
                "Shitty Coverage Fixture"
            )
    mask = []
    for y in range(border, border + metrics["py"]):
        for x in range(border, border + metrics["px"]):
            offset = 3 * (y * width + x)
            mask.append(max(pixels[offset : offset + 3]) > 40)
    return tuple(mask)


def ink_fraction(mask):
    return sum(mask) / len(mask)


@unittest.skipIf(
    TEST_PLATFORM == "cocoa",
    "Core Text precedes the Fontconfig fixture on Darwin",
)
class SystemFallbackTest(unittest.TestCase):
    # Issues 48/58: scripts the configured and embedded fonts miss must
    # come from the system's own fallback chain, and a system face must
    # outrank the embedded last resort.
    def test_uncovered_script_comes_from_the_system_chain(self):
        # The fixture box glyph fills ~half the cell; the U+FFFD
        # substitute the pack draws without system fallback inks ~28%.
        self.assertGreater(ink_fraction(render_system_fallback_cell("ก")), 0.4)

    def test_system_face_outranks_the_embedded_fallback(self):
        # W is covered by the embedded JetBrains Mono too (~30% ink);
        # the system face with its solid box must win the walk.
        self.assertGreater(ink_fraction(render_system_fallback_cell("W")), 0.4)


@unittest.skipUnless(TEST_PLATFORM == "cocoa", "Core Text is only used on Darwin")
class CoreTextSystemFallbackTest(unittest.TestCase):
    def test_uncovered_script_comes_from_the_system_chain(self):
        thai = render_system_fallback_cell("ก", with_fontconfig_fallback=False)
        missing = render_system_fallback_cell("͸", with_fontconfig_fallback=False)

        self.assertTrue(any(thai))
        self.assertNotEqual(thai, missing)


if __name__ == "__main__":
    unittest.main()
