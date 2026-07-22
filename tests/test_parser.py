import unittest

from harness import Shitty


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
    def test_control_exposes_normalized_parser_events(self):
        with Shitty(columns=8, rows=2) as terminal:
            terminal.parser_trace_on()
            terminal.write(
                b"A\x03"
                b"\x1b[?15;2z"
                b"\x1b]27;Something\x1b\\"
            )
            self.assertEqual(
                terminal.parser_trace(),
                [
                    ("text", b"A"),
                    ("control", b"\x03"),
                    ("csi", b"?15;2z"),
                    ("osc", b"27;Something"),
                ],
            )

    def test_parameter_intermediate_and_final_bytes_are_independent(self):
        with Shitty(columns=6, rows=2) as terminal:
            terminal.write_chunks(b"\x1b[1;", b"2", b"\"", b"q", b"X")
            self.assertTrue(terminal.snapshot().cell(0, 0).protected)

            terminal.write(b"\x1b[1!2pY")
            self.assertEqual(terminal.snapshot().cell(1, 0).char, "Y")

    def test_unknown_multi_intermediate_csi_is_ignored_atomically(self):
        with Shitty(columns=8, rows=2) as terminal:
            terminal.write(b"a\x1b[12 !~b")
            self.assertEqual(terminal.snapshot().lines[0][:2], "ab")

    def test_utf8_graphic_aborts_malformed_csi_and_is_reprocessed(self):
        with Shitty(columns=8, rows=2) as terminal:
            terminal.parser_trace_on()
            terminal.write(b"\x1b[\xc4\x80a")
            self.assertEqual(terminal.snapshot().lines[0][:2], "Āa")
            self.assertEqual(terminal.parser_trace(), [("text", b"\xc4\x80a")])

    def test_cancel_discards_oversized_csi_but_remains_observable(self):
        with Shitty(columns=8, rows=2) as terminal:
            terminal.parser_trace_on()
            terminal.write(b"\x1b[" + b"1;" * 32 + b"1m\x18X")
            self.assertEqual(
                terminal.parser_trace(),
                [("control", b"\x18"), ("text", b"X")],
            )

    def test_invalid_utf8_in_dcs_header_discards_the_control_string(self):
        with Shitty(columns=8, rows=2) as terminal:
            terminal.parser_trace_on()
            terminal.write(b"\x1bP1 \xc4\x80a\x1b\\X")
            self.assertEqual(terminal.snapshot().lines[0][0], "X")
            self.assertEqual(terminal.parser_trace(), [("text", b"X")])

    def test_st_encodings_remain_observable_outside_control_strings(self):
        cases = (
            (b"\x1b\\", [("escape", b"\\")]),
            (b"\x9c", [("control", b"\x9c")]),
            (b"\x1b\x1b\\", [("escape", b"\\")]),
            (b"\x1b\x9c", [("control", b"\x9c")]),
        )
        for sequence, expected in cases:
            with self.subTest(sequence=sequence):
                with Shitty(columns=8, rows=2) as terminal:
                    terminal.parser_trace_on()
                    terminal.write(sequence)
                    self.assertEqual(terminal.parser_trace(), expected)

    def test_seven_bit_c1_form_remains_an_escape_event(self):
        with Shitty(columns=8, rows=2) as terminal:
            terminal.parser_trace_on()
            terminal.write(b"\x1bD\x84")
            self.assertEqual(
                terminal.parser_trace(),
                [("escape", b"D"), ("control", b"\x84")],
            )

    def test_special_first_intermediate_does_not_end_escape_sequence(self):
        with Shitty(columns=8, rows=2) as terminal:
            terminal.parser_trace_on()
            terminal.write(b"\x1b 0\x1b#!A\x1b%/B")
            self.assertEqual(
                terminal.parser_trace(),
                [
                    ("escape", b" 0"),
                    ("escape", b"#!A"),
                    ("escape", b"%/B"),
                ],
            )

    def test_osc_accepts_mixed_width_terminators_and_legacy_bel(self):
        cases = (
            b"\x1b]TEST\x1b\\",
            b"\x1b]TEST\x9c",
            b"\x1b]TEST\x07",
            b"\x9dTEST\x1b\\",
            b"\x9dTEST\x9c",
            b"\x9dTEST\x07",
        )
        for sequence in cases:
            with self.subTest(sequence=sequence):
                with Shitty(columns=8, rows=2) as terminal:
                    terminal.parser_trace_on()
                    terminal.write(sequence)
                    self.assertEqual(terminal.parser_trace(), [("osc", b"TEST")])

    def test_private_prefix_after_numeric_parameters_is_rejected(self):
        with Shitty(columns=8, rows=2) as terminal:
            terminal.write(b"a\x1b[1?25hb")
            self.assertEqual(terminal.snapshot().lines[0][:2], "ab")

    def assert_all_splits_match(self, sequence, suffix=b"X"):
        with Shitty(columns=8, rows=3) as terminal:
            terminal.write(sequence + suffix)
            expected = observable(terminal)

        chunkings = [
            (sequence[:split], sequence[split:], suffix)
            for split in range(1, len(sequence))
        ]
        chunkings.append(tuple(bytes([byte]) for byte in sequence + suffix))
        for chunks in chunkings:
            with self.subTest(chunks=chunks):
                with Shitty(columns=8, rows=3) as terminal:
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
        with Shitty(columns=8, rows=3) as terminal:
            terminal.write(b"\x1b[999;\x1b[2;3HX")
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.cell(2, 1).char, "X")
            self.assertEqual(snapshot.lines[0], "        ")

    def test_can_and_sub_cancel_incomplete_sequences(self):
        for cancel in (b"\x18", b"\x1a"):
            with self.subTest(cancel=cancel):
                with Shitty(columns=8, rows=2) as terminal:
                    terminal.write(b"\x1b[31" + cancel + b"X")
                    cell = terminal.snapshot().cell(0, 0)
                    self.assertEqual(cell.char, "X")
                    self.assertEqual(cell.foreground, (255, 255, 255))

    def test_can_cancels_osc_without_emitting_action(self):
        with Shitty(columns=8, rows=2) as terminal:
            terminal.write(b"\x1b]2;ignored\x18X")
            self.assertEqual(terminal.read_actions(), [])
            self.assertEqual(terminal.snapshot().cell(0, 0).char, "X")

    def test_unknown_csi_intermediates_are_ignored_as_a_unit(self):
        sequences = (
            b"\x1b[?1$p",
            b"\x1b[1;2;3;4$x",
            b"\x1b[1;2&z",
        )
        for sequence in sequences:
            with self.subTest(sequence=sequence):
                with Shitty(columns=8, rows=2) as terminal:
                    terminal.write(b"A" + sequence + b"B")
                    self.assertEqual(terminal.snapshot().lines[0], "AB      ")

    def test_unknown_string_protocols_are_ignored_through_st(self):
        sequences = (
            b"\x1b_Gi=31;QUJDRA==\x1b\\",
            b"\x1b^private message\x1b\\",
            b"\x1bXstart of string\x1b\\",
        )
        for sequence in sequences:
            with self.subTest(sequence=sequence):
                with Shitty(columns=8, rows=2) as terminal:
                    terminal.write(b"A" + sequence + b"B")
                    self.assertEqual(terminal.snapshot().lines[0], "AB      ")

    def test_oversized_osc_and_dcs_are_discarded_through_st(self):
        sequences = (
            b"\x1b]2;" + b"x" * (1024 * 1024 + 1) + b"\x1b\\",
            b"\x1bP$q" + b"x" * 5000 + b"\x1b\\",
        )
        for sequence in sequences:
            with self.subTest(sequence=sequence[:3]):
                with Shitty(columns=8, rows=2) as terminal:
                    terminal.write(b"A" + sequence + b"B")
                    self.assertEqual(terminal.snapshot().lines[0], "AB      ")
                    self.assertEqual(terminal.read_actions(), [])
                    self.assertEqual(terminal.read_input(), b"")

    def test_overflowing_csi_parameters_do_not_leak_as_text(self):
        sequences = (
            b"\x1b[999999999999999999999A",
            b"\x1b[" + b"1;" * 32 + b"1m",
        )
        for sequence in sequences:
            with self.subTest(sequence=sequence[:16]):
                with Shitty(columns=8, rows=2) as terminal:
                    terminal.write(b"A" + sequence + b"B")
                    self.assertEqual(terminal.snapshot().lines[0], "AB      ")

    def test_eight_bit_c1_sequences_match_seven_bit_forms(self):
        with Shitty(columns=8, rows=3) as terminal:
            terminal.write(
                b"\x9b2;3HX"
                b"\x9d2;eight bit title\x9c"
                b"\x90$q\"p\x9c"
            )
            snapshot = terminal.snapshot()

            self.assertEqual(snapshot.cell(2, 1).char, "X")
            self.assertEqual(
                terminal.read_actions(),
                ["OSC 2 656967687420626974207469746c65"],
            )
            self.assertEqual(
                terminal.read_input(), b"\x1bP1$r64;1\"p\x1b\\"
            )

    def test_eight_bit_string_protocols_are_ignored_through_st(self):
        for sequence in (b"\x9fignored\x9c", b"\x9eignored\x9c", b"\x98ignored\x9c"):
            with self.subTest(sequence=sequence):
                with Shitty(columns=8, rows=2) as terminal:
                    terminal.write(b"A" + sequence + b"B")
                    self.assertEqual(terminal.snapshot().lines[0], "AB      ")


if __name__ == "__main__":
    unittest.main()
