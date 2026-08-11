# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

"""Public adaptations of the first 21 xterm.js EscapeSequenceParser cases."""

import unittest

from harness import Shitty


PORTED_CASES = (
    "state GROUND execute action",
    "state GROUND print action",
    "trans ANYWHERE --> GROUND with actions",
    "trans ANYWHERE --> ESCAPE with clear",
    "state ESCAPE execute rules",
    "state ESCAPE ignore",
    "trans ESCAPE --> GROUND with ecs_dispatch action",
    "trans ESCAPE --> ESCAPE_INTERMEDIATE with collect action",
    "state ESCAPE_INTERMEDIATE execute rules",
    "state ESCAPE_INTERMEDIATE ignore",
    "state ESCAPE_INTERMEDIATE collect action",
    "trans ESCAPE_INTERMEDIATE --> GROUND with esc_dispatch action",
    "trans ANYWHERE/ESCAPE --> CSI_ENTRY with clear",
    "state CSI_ENTRY execute rules",
    "state CSI_ENTRY ignore",
    "trans CSI_ENTRY --> GROUND with csi_dispatch action",
    "trans CSI_ENTRY --> CSI_PARAM with param/collect actions",
    "state CSI_PARAM execute rules",
    "state CSI_PARAM param action",
    "state CSI_PARAM ignore",
    "trans CSI_PARAM --> GROUND with csi_dispatch action",
)

C0_EXECUTE = (*range(0x00, 0x18), 0x19, *range(0x1C, 0x20))
C1_EXECUTE = (
    0x80,
    0x81,
    0x82,
    0x83,
    0x84,
    0x85,
    0x86,
    0x87,
    0x88,
    0x89,
    0x8A,
    0x8B,
    0x8C,
    0x8D,
    0x8E,
    0x8F,
    0x91,
    0x92,
    0x93,
    0x94,
    0x95,
    0x96,
    0x97,
    0x99,
    0x9A,
)
ESC_DISPATCH_FINALS = (
    *range(0x30, 0x50),
    *range(0x51, 0x58),
    0x59,
    0x5A,
    *range(0x60, 0x7F),
)

# These byte prefixes reach every state from the source case that has a
# terminal-wire counterpart.  APC entry/intermediate/passthrough collapse to
# one public control-string state in Shitty, but remain three separate inputs.
STATE_PREFIXES = (
    b"",                    # GROUND
    b"\x1b",               # ESCAPE
    b"\x1b!",              # ESCAPE_INTERMEDIATE
    b"\x1b[",              # CSI_ENTRY
    b"\x1b[1",             # CSI_PARAM
    b"\x1b[!",             # CSI_INTERMEDIATE
    b"\x1b[1?",            # CSI_IGNORE
    b"\x1bX",              # SOS_PM_STRING
    b"\x1b]2;pending",      # OSC_STRING
    b"\x1bP",              # DCS_ENTRY
    b"\x1bP1",             # DCS_PARAM
    b"\x1bP1?",            # DCS_IGNORE
    b"\x1bP!",             # DCS_INTERMEDIATE
    b"\x1bP$qpending",      # DCS_PASSTHROUGH
    b"\x1b_",              # APC_ENTRY
    b"\x1b_ ",             # APC_INTERMEDIATE
    b"\x1b_0pending",      # APC_PASSTHROUGH
)


class XtermJsEscapeSequenceParserTest(unittest.TestCase):
    def setUp(self):
        self.terminal = Shitty(columns=128, rows=8)
        self.terminal.parser_trace_on()

    def tearDown(self):
        self.terminal.close()

    def assert_trace(self, sequence, expected):
        self.terminal.parser_trace_clear()
        self.terminal.read_actions()
        self.terminal.read_input()
        self.terminal.write(sequence)
        self.assertEqual(self.terminal.parser_trace(), expected)

    def test_inventory_accounts_for_21_distinct_upstream_cases(self):
        self.assertEqual(len(PORTED_CASES), 21)
        self.assertEqual(len(set(PORTED_CASES)), 21)

    def test_ground_executes_every_c0_action_byte(self):
        payload = bytes(C0_EXECUTE)
        self.assert_trace(
            payload,
            [("control", bytes((byte,))) for byte in C0_EXECUTE if byte],
        )
        self.assertEqual(self.terminal.read_actions(), ["BELL"])

    def test_ground_prints_the_complete_ascii_graphic_range(self):
        payload = bytes(range(0x20, 0x7F))
        self.assert_trace(payload, [("text", payload)])
        self.assertEqual(self.terminal.snapshot().lines[0][:95], payload.decode())

    def test_anywhere_controls_abort_the_sequence_and_return_to_ground(self):
        for prefix in STATE_PREFIXES:
            for control in (0x18, 0x1A):
                with self.subTest(prefix=prefix, control=control):
                    self.assert_trace(
                        prefix + bytes((control,)) + b"X\x1b\\",
                        [
                            ("control", bytes((control,))),
                            ("text", b"X"),
                            ("escape", b"\\"),
                        ],
                    )

        # Shitty is UTF-8-only by default.  ESC %@ selects the existing ISO
        # single-byte input path where raw C1 controls are meaningful.
        self.terminal.write(b"\x1b%@")
        for prefix in STATE_PREFIXES:
            for control in C1_EXECUTE:
                with self.subTest(prefix=prefix, control=control):
                    self.terminal.parser_trace_clear()
                    self.terminal.write(prefix + bytes((control,)) + b"X\x1b\\")
                    actual = self.terminal.parser_trace()
                    self.assertEqual(
                        actual[-3:],
                        [
                            ("control", bytes((control,))),
                            ("text", b"X"),
                            ("escape", b"\\"),
                        ],
                    )
                    self.assertFalse(
                        any(event in {"osc", "dcs", "apc", "pm", "sos"}
                            for event, _ in actual)
                    )

        # ST terminates an active control string (or is a no-op elsewhere),
        # and in every state leaves the following graphic in ground.
        for prefix in STATE_PREFIXES:
            with self.subTest(prefix=prefix, control=0x9C):
                self.terminal.parser_trace_clear()
                self.terminal.write(prefix + b"\x9cX")
                self.assertEqual(self.terminal.parser_trace()[-1:], [("text", b"X")])
        self.terminal.write(b"\x1b%G")
        self.terminal.parser_trace_clear()

    def test_escape_from_any_state_clears_the_abandoned_sequence(self):
        for prefix in STATE_PREFIXES:
            with self.subTest(prefix=prefix):
                self.assert_trace(
                    prefix + b"\x1bDX\x1b\\",
                    [
                        ("escape", b"D"),
                        ("text", b"X"),
                        ("escape", b"\\"),
                    ],
                )

    def test_escape_executes_c0_without_losing_escape_state(self):
        for control in C0_EXECUTE:
            with self.subTest(control=control):
                self.assert_trace(
                    b"\x1b" + bytes((control,)) + b"A",
                    [("escape", b"A")],
                )
                if control == 0x07:
                    self.assertEqual(self.terminal.read_actions(), ["BELL"])

    def test_escape_ignores_del_without_losing_escape_state(self):
        self.assert_trace(b"\x1b\x7fA", [("escape", b"A")])

    def test_escape_dispatches_every_non_introducer_final(self):
        for final in ESC_DISPATCH_FINALS:
            with self.subTest(final=final):
                value = bytes((final,))
                self.assert_trace(b"\x1b" + value, [("escape", value)])

    def test_escape_collects_each_intermediate_byte(self):
        for intermediate in range(0x20, 0x30):
            with self.subTest(intermediate=intermediate):
                value = bytes((intermediate,))
                self.assert_trace(
                    b"\x1b" + value + b"A",
                    [("escape", value + b"A")],
                )

    def test_escape_intermediate_executes_c0_and_keeps_collected_bytes(self):
        for intermediate in range(0x20, 0x30):
            for control in C0_EXECUTE:
                with self.subTest(intermediate=intermediate, control=control):
                    value = bytes((intermediate,))
                    self.assert_trace(
                        b"\x1b" + value + bytes((control,)) + b"A",
                        [("escape", value + b"A")],
                    )
                    if control == 0x07:
                        self.assertEqual(self.terminal.read_actions(), ["BELL"])

    def test_escape_intermediate_ignores_del(self):
        for intermediate in range(0x20, 0x30):
            with self.subTest(intermediate=intermediate):
                value = bytes((intermediate,))
                self.assert_trace(
                    b"\x1b" + value + b"\x7fA",
                    [("escape", value + b"A")],
                )

    def test_escape_intermediate_collects_multiple_bytes(self):
        for intermediate in range(0x20, 0x30):
            with self.subTest(intermediate=intermediate):
                value = bytes((intermediate,))
                self.assert_trace(
                    b"\x1b!" + value + b"A",
                    [("escape", b"!" + value + b"A")],
                )

    def test_escape_intermediate_dispatches_every_final(self):
        for final in range(0x30, 0x7F):
            with self.subTest(final=final):
                value = bytes((final,))
                self.assert_trace(
                    b"\x1b!" + value,
                    [("escape", b"!" + value)],
                )

    def test_csi_introducer_from_any_state_clears_old_parameters(self):
        for prefix in STATE_PREFIXES:
            with self.subTest(prefix=prefix, introducer=b"\x1b["):
                self.assert_trace(
                    prefix + b"\x1b[HX\x1b\\",
                    [("csi", b"H"), ("text", b"X"), ("escape", b"\\")],
                )

        self.terminal.write(b"\x1b%@")
        for prefix in STATE_PREFIXES:
            with self.subTest(prefix=prefix, introducer=b"\x9b"):
                self.assert_trace(
                    prefix + b"\x9bHX\x1b\\",
                    [("csi", b"H"), ("text", b"X"), ("escape", b"\\")],
                )
        self.terminal.write(b"\x1b%G")
        self.terminal.parser_trace_clear()

    def test_csi_entry_executes_c0_without_losing_csi_state(self):
        for control in C0_EXECUTE:
            with self.subTest(control=control):
                self.assert_trace(
                    b"\x1b[" + bytes((control,)) + b"H",
                    [("csi", b"H")],
                )
                if control == 0x07:
                    self.assertEqual(self.terminal.read_actions(), ["BELL"])

    def test_csi_entry_ignores_del(self):
        self.assert_trace(b"\x1b[\x7fH", [("csi", b"H")])

    def test_csi_entry_dispatches_every_final(self):
        for final in range(0x40, 0x7F):
            with self.subTest(final=final):
                value = bytes((final,))
                self.assert_trace(b"\x1b[" + value, [("csi", value)])

    def test_csi_entry_collects_parameters_and_private_prefixes(self):
        for digit in range(10):
            with self.subTest(digit=digit):
                raw = str(digit).encode()
                self.assert_trace(b"\x1b[" + raw + b"m", [("csi", raw + b"m")])
        self.assert_trace(b"\x1b[;m", [("csi", b"0;0m")])
        for prefix in b"<=>?":
            with self.subTest(prefix=prefix):
                raw = bytes((prefix,))
                self.assert_trace(b"\x1b[" + raw + b"m", [("csi", raw + b"m")])

    def test_csi_param_executes_c0_without_losing_parameters(self):
        for control in C0_EXECUTE:
            with self.subTest(control=control):
                self.assert_trace(
                    b"\x1b[1" + bytes((control,)) + b"m",
                    [("csi", b"1m")],
                )
                if control == 0x07:
                    self.assertEqual(self.terminal.read_actions(), ["BELL"])

    def test_csi_param_accumulates_digits_and_empty_parameters(self):
        for digit in range(10):
            with self.subTest(digit=digit):
                raw = str(digit).encode()
                self.assert_trace(b"\x1b[" + raw + b"m", [("csi", raw + b"m")])
        self.assert_trace(b"\x1b[;m", [("csi", b"0;0m")])

    def test_csi_param_ignores_del(self):
        self.assert_trace(b"\x1b[1\x7fm", [("csi", b"1m")])

    def test_csi_param_dispatches_every_final_with_all_parameters(self):
        for final in range(0x40, 0x7F):
            with self.subTest(final=final):
                value = bytes((final,))
                self.assert_trace(
                    b"\x1b[0;1" + value,
                    [("csi", b"0;1" + value)],
                )


if __name__ == "__main__":
    unittest.main()
