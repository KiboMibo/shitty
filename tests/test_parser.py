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


if __name__ == "__main__":
    unittest.main()
