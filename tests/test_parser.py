import unittest

from harness import Zutty


def observable(terminal):
    snapshot = terminal.snapshot()
    cells = tuple(
        (
            cell.char,
            cell.double_width,
            cell.double_width_continuation,
            cell.bold,
            cell.italic,
            cell.underline,
            cell.inverse,
            cell.wrapped,
            cell.foreground,
            cell.background,
            cell.hyperlink,
        )
        for cell in snapshot.cells
    )
    return (
        snapshot.columns,
        snapshot.rows,
        snapshot.cursor_x,
        snapshot.cursor_y,
        snapshot.cursor_style,
        snapshot.view_offset,
        cells,
        terminal.read_input(),
    )


class ParserStreamingTest(unittest.TestCase):
    def assert_all_splits_match(self, sequence, suffix=b"X"):
        with Zutty(columns=8, rows=3) as terminal:
            terminal.write(sequence + suffix)
            expected = observable(terminal)

        chunkings = [
            (sequence[:split], sequence[split:], suffix)
            for split in range(1, len(sequence))
        ]
        chunkings.append(tuple(bytes([byte]) for byte in sequence + suffix))
        for chunks in chunkings:
            with self.subTest(chunks=chunks):
                with Zutty(columns=8, rows=3) as terminal:
                    terminal.write_chunks(*chunks)
                    self.assertEqual(observable(terminal), expected)

    def test_csi_private_prefix_survives_read_boundaries(self):
        self.assert_all_splits_match(b"\x1b[?1049h\x1b[2;3H")

    def test_csi_greater_than_prefix_survives_read_boundaries(self):
        self.assert_all_splits_match(b"\x1b[>7u\x1b[2;3H")

    def test_sgr_survives_read_boundaries(self):
        self.assert_all_splits_match(b"\x1b[1;3;4;38;2;1;2;3m")

    def test_device_attributes_query_survives_read_boundaries(self):
        self.assert_all_splits_match(b"\x1b[c", suffix=b"")

    def test_osc_bel_terminator_survives_read_boundaries(self):
        self.assert_all_splits_match(b"\x1b]4;1;?\a", suffix=b"")

    def test_osc_st_terminator_survives_read_boundaries(self):
        self.assert_all_splits_match(b"\x1b]10;?\x1b\\", suffix=b"")

    def test_dcs_survives_read_boundaries(self):
        self.assert_all_splits_match(b"\x1bP$qm\x1b\\", suffix=b"")

    def test_escape_restarts_an_incomplete_csi(self):
        with Zutty(columns=8, rows=3) as terminal:
            terminal.write(b"\x1b[999;\x1b[2;3HX")
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.cell(2, 1).char, "X")
            self.assertEqual(snapshot.lines[0], "        ")

    def test_can_and_sub_cancel_incomplete_sequences(self):
        for cancel in (b"\x18", b"\x1a"):
            with self.subTest(cancel=cancel):
                with Zutty(columns=8, rows=2) as terminal:
                    terminal.write(b"\x1b[31" + cancel + b"X")
                    cell = terminal.snapshot().cell(0, 0)
                    self.assertEqual(cell.char, "X")
                    self.assertEqual(cell.foreground, (255, 255, 255))

    def test_can_cancels_osc_without_emitting_action(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(b"\x1b]2;ignored\x18X")
            self.assertEqual(terminal.read_actions(), [])
            self.assertEqual(terminal.snapshot().cell(0, 0).char, "X")


if __name__ == "__main__":
    unittest.main()
