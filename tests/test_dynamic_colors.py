import unittest

from harness import Zutty


def dynamic_query(terminal, *commands):
    terminal.write(
        b"".join(f"\x1b]{command};?\x1b\\".encode() for command in commands)
    )
    return terminal.read_input()


class DynamicColorTest(unittest.TestCase):
    def test_palette_change_recolors_all_indexed_cell_channels(self):
        with Zutty(columns=4, rows=2) as terminal:
            terminal.write(
                b"\x1b[38;5;1;48;5;1;58;5;1mI"
                b"\x1b[38;2;205;0;0;48;2;205;0;0;58;2;205;0;0mT"
                b"\x1b]4;1;#010203\x1b\\"
            )
            snapshot = terminal.snapshot()

            indexed = snapshot.cell(0, 0)
            self.assertEqual(indexed.foreground, (1, 2, 3))
            self.assertEqual(indexed.background, (1, 2, 3))
            self.assertEqual(indexed.underline_color, (1, 2, 3))
            direct = snapshot.cell(1, 0)
            self.assertEqual(direct.foreground, (205, 0, 0))
            self.assertEqual(direct.background, (205, 0, 0))
            self.assertEqual(direct.underline_color, (205, 0, 0))

    def test_palette_change_is_resolved_when_scrollback_becomes_visible(self):
        with Zutty(columns=4, rows=2, save_lines=4) as terminal:
            terminal.write(
                b"\x1b[38;5;1mA\r\nB\r\nC"
                b"\x1b]4;1;#010203\x1b\\"
            )

            terminal.wheel_up()
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.lines[0], "A   ")
            self.assertEqual(snapshot.cell(0, 0).foreground, (1, 2, 3))

    def test_palette_reset_can_target_indices_or_the_whole_palette(self):
        with Zutty(columns=4, rows=2) as terminal:
            terminal.write(
                b"\x1b]4;1;#010203;2;#040506\x1b\\"
                b"\x1b]104;1\x1b\\"
                b"\x1b]4;1;?;2;?\x1b\\"
            )
            targeted = terminal.read_input()
            self.assertIn(b"\x1b]4;1;rgb:cdcd/0000/0000\x1b\\", targeted)
            self.assertIn(b"\x1b]4;2;rgb:0404/0505/0606\x1b\\", targeted)

            terminal.write(b"\x1b]104\x1b\\\x1b]4;2;?\x1b\\")
            self.assertEqual(
                terminal.read_input(),
                b"\x1b]4;2;rgb:0000/cdcd/0000\x1b\\",
            )

    def test_default_foreground_recolors_default_cells_and_underline(self):
        with Zutty(columns=4, rows=2) as terminal:
            terminal.write(
                b"D\x1b[4mU\x1b[31mI"
                b"\x1b]10;#010203\x1b\\"
            )
            snapshot = terminal.snapshot()

            self.assertEqual(snapshot.cell(0, 0).foreground, (1, 2, 3))
            self.assertEqual(snapshot.cell(1, 0).foreground, (1, 2, 3))
            self.assertEqual(snapshot.cell(1, 0).underline_color, (1, 2, 3))
            self.assertEqual(snapshot.cell(2, 0).foreground, (205, 0, 0))

            terminal.write(b"\x1b]110\x1b\\")
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.cell(0, 0).foreground, (255, 255, 255))
            self.assertEqual(snapshot.cell(1, 0).underline_color, (255, 255, 255))

    def test_default_background_recolors_only_default_background_cells(self):
        with Zutty(columns=4, rows=2) as terminal:
            terminal.write(b"D\x1b[41mI\x1b]11;#010203\x1b\\")
            snapshot = terminal.snapshot()

            self.assertEqual(snapshot.cell(0, 0).background, (1, 2, 3))
            self.assertEqual(snapshot.cell(1, 0).background, (205, 0, 0))

            terminal.write(b"\x1b]111\x1b\\")
            self.assertEqual(
                terminal.snapshot().cell(0, 0).background, (0, 0, 0)
            )

    def test_cursor_dynamic_color_set_query_and_reset(self):
        with Zutty(columns=4, rows=2) as terminal:
            terminal.write(b"\x1b]12;#010203\x1b\\")
            self.assertEqual(
                dynamic_query(terminal, 12),
                b"\x1b]12;rgb:0101/0202/0303\x1b\\",
            )

            terminal.write(b"\x1b]112\x1b\\")
            self.assertNotEqual(
                dynamic_query(terminal, 12),
                b"\x1b]12;rgb:0101/0202/0303\x1b\\",
            )

    def test_selection_dynamic_colors_reach_renderer_and_reset(self):
        with Zutty(columns=4, rows=2) as terminal:
            terminal.write(
                b"\x1b]17;#010203\x1b\\"
                b"\x1b]19;#040506\x1b\\"
            )
            state = terminal.render_state()

            self.assertEqual(state.selection_mask, 3)
            self.assertEqual(state.selection_background, (1, 2, 3))
            self.assertEqual(state.selection_foreground, (4, 5, 6))
            self.assertEqual(
                dynamic_query(terminal, 17, 19),
                b"\x1b]17;rgb:0101/0202/0303\x1b\\"
                b"\x1b]19;rgb:0404/0505/0606\x1b\\",
            )

            terminal.write(b"\x1b]117\x1b\\\x1b]119\x1b\\")
            state = terminal.render_state()
            self.assertEqual(state.selection_mask, 0)
            self.assertEqual(state.selection_background, (0, 0, 0))
            self.assertEqual(state.selection_foreground, (255, 255, 255))

    def test_invalid_dynamic_specs_leave_every_color_unchanged(self):
        with Zutty(columns=4, rows=2) as terminal:
            before = dynamic_query(terminal, 10, 11, 12, 17, 19)
            for command in (10, 11, 12, 17, 19):
                terminal.write(
                    f"\x1b]{command};rgb:1/2\x1b\\".encode()
                    + f"\x1b]{command};#xyzxyz\x1b\\".encode()
                )

            self.assertEqual(
                dynamic_query(terminal, 10, 11, 12, 17, 19), before
            )

    def test_rgb_component_widths_scale_to_eight_bits(self):
        expected = (
            (b"rgb:f/0/8", b"ffff/0000/8888"),
            (b"rgb:ff/00/80", b"ffff/0000/8080"),
            (b"rgb:fff/000/800", b"ffff/0000/8080"),
            (b"rgb:ffff/0000/8000", b"ffff/0000/8080"),
        )
        with Zutty(columns=4, rows=2) as terminal:
            for spec, reply in expected:
                with self.subTest(spec=spec):
                    terminal.write(b"\x1b]12;" + spec + b"\x1b\\")
                    self.assertEqual(
                        dynamic_query(terminal, 12),
                        b"\x1b]12;rgb:" + reply + b"\x1b\\",
                    )

    def test_hash_component_widths_are_high_bit_aligned(self):
        expected = (
            (b"#f08", b"f0f0/0000/8080"),
            (b"#f00080", b"f0f0/0000/8080"),
            (b"#f00000800", b"f0f0/0000/8080"),
            (b"#f00000008000", b"f0f0/0000/8080"),
        )
        with Zutty(columns=4, rows=2) as terminal:
            for spec, reply in expected:
                with self.subTest(spec=spec):
                    terminal.write(b"\x1b]12;" + spec + b"\x1b\\")
                    self.assertEqual(
                        dynamic_query(terminal, 12),
                        b"\x1b]12;rgb:" + reply + b"\x1b\\",
                    )


if __name__ == "__main__":
    unittest.main()
