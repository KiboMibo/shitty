# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

"""Public adaptations of the first 44 xterm.js EscapeSequenceParser cases."""

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
    "trans CSI_ENTRY --> CSI_INTERMEDIATE with collect action",
    "trans CSI_PARAM --> CSI_INTERMEDIATE with collect action",
    "state CSI_INTERMEDIATE execute rules",
    "state CSI_INTERMEDIATE collect",
    "state CSI_INTERMEDIATE ignore",
    "trans CSI_INTERMEDIATE --> GROUND with csi_dispatch action",
    'trans CSI_ENTRY --> CSI_PARAM for ":" (0x3a)',
    "trans CSI_PARAM --> CSI_IGNORE",
    "trans CSI_PARAM --> CSI_IGNORE",
    "trans CSI_INTERMEDIATE --> CSI_IGNORE",
    "state CSI_IGNORE execute rules",
    "state CSI_IGNORE ignore",
    "trans CSI_IGNORE --> GROUND",
    "trans ANYWHERE/ESCAPE --> SOS_PM_STRING",
    "state SOS_PM_STRING ignore rules",
    "trans ANYWHERE/ESCAPE --> OSC_STRING",
    "state OSC_STRING ignore rules",
    "state OSC_STRING put action",
    "state DCS_ENTRY",
    "state DCS_ENTRY ignore rules",
    "state DCS_ENTRY --> DCS_PARAM with param/collect actions",
    "state DCS_PARAM ignore rules",
    "state DCS_PARAM param action",
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

    def test_inventory_accounts_for_44_upstream_cases_including_duplicate(self):
        self.assertEqual(len(PORTED_CASES), 44)
        # Upstream contains the same CSI_PARAM -> CSI_IGNORE case twice.
        self.assertEqual(len(set(PORTED_CASES)), 43)

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

    def test_csi_entry_collects_each_intermediate_byte(self):
        for intermediate in range(0x20, 0x30):
            with self.subTest(intermediate=intermediate):
                value = bytes((intermediate,))
                self.assert_trace(
                    b"\x1b[" + value + b"m",
                    [("csi", value + b"m")],
                )

    def test_csi_param_collects_each_intermediate_byte(self):
        for intermediate in range(0x20, 0x30):
            with self.subTest(intermediate=intermediate):
                value = bytes((intermediate,))
                self.assert_trace(
                    b"\x1b[1" + value + b"m",
                    [("csi", b"1" + value + b"m")],
                )

    def test_csi_intermediate_executes_c0_and_keeps_collected_bytes(self):
        for control in C0_EXECUTE:
            with self.subTest(control=control):
                self.assert_trace(
                    b"\x1b[!" + bytes((control,)) + b"m",
                    [("csi", b"!m")],
                )
                self.assertEqual(
                    self.terminal.read_actions(),
                    ["BELL"] if control == 0x07 else [],
                )

    def test_csi_intermediate_collects_multiple_bytes(self):
        for intermediate in range(0x20, 0x30):
            with self.subTest(intermediate=intermediate):
                value = bytes((intermediate,))
                self.assert_trace(
                    b"\x1b[!" + value + b"m",
                    [("csi", b"!" + value + b"m")],
                )

    def test_csi_intermediate_ignores_del(self):
        self.assert_trace(b"\x1b[!\x7fm", [("csi", b"!m")])

    def test_csi_intermediate_dispatches_every_final(self):
        for final in range(0x40, 0x7F):
            with self.subTest(final=final):
                value = bytes((final,))
                self.assert_trace(
                    b"\x1b[!" + value,
                    [("csi", b"!" + value)],
                )

    def test_csi_entry_accepts_a_leading_subparameter_separator(self):
        self.assert_trace(b"\x1b[:m", [("csi", b"0:0m")])

    def test_csi_param_private_marker_enters_ignore(self):
        for marker in b"<=>?":
            with self.subTest(marker=marker):
                self.assert_trace(
                    b"\x1b[1;" + bytes((marker,)) + b"mX",
                    [("text", b"X")],
                )

    def test_csi_param_duplicate_upstream_ignore_case_keeps_empty_parameter(self):
        for marker in b"<=>?":
            with self.subTest(marker=marker):
                self.assert_trace(
                    b"\x1b[;" + bytes((marker,)) + b"mX",
                    [("text", b"X")],
                )

    def test_csi_intermediate_parameter_bytes_enter_ignore(self):
        for parameter in range(0x30, 0x40):
            with self.subTest(parameter=parameter):
                self.assert_trace(
                    b"\x1b[!" + bytes((parameter,)) + b"mX",
                    [("text", b"X")],
                )

    def test_csi_ignore_executes_c0_without_leaving_ignore(self):
        for control in C0_EXECUTE:
            with self.subTest(control=control):
                self.assert_trace(
                    b"\x1b[!0" + bytes((control,)) + b"mX",
                    [("text", b"X")],
                )
                self.assertEqual(
                    self.terminal.read_actions(),
                    ["BELL"] if control == 0x07 else [],
                )

    def test_csi_ignore_discards_parameter_bytes_and_del(self):
        for ignored in (*range(0x20, 0x40), 0x7F):
            with self.subTest(ignored=ignored):
                self.assert_trace(
                    b"\x1b[!0" + bytes((ignored,)) + b"mX",
                    [("text", b"X")],
                )

    def test_csi_ignore_returns_to_ground_on_every_final(self):
        for final in range(0x40, 0x7F):
            with self.subTest(final=final):
                self.assert_trace(
                    b"\x1b[!0" + bytes((final,)) + b"X",
                    [("text", b"X")],
                )

    def test_sos_and_pm_introducers_replace_every_parser_state(self):
        for prefix in STATE_PREFIXES:
            for introducer, event in ((b"\x1bX", "sos"), (b"\x1b^", "pm")):
                with self.subTest(prefix=prefix, introducer=introducer):
                    self.assert_trace(
                        prefix + introducer + b"a\x1b\\X",
                        [(event, b"a"), ("text", b"X")],
                    )

        self.terminal.write(b"\x1b%@")
        for prefix in STATE_PREFIXES:
            for introducer, event in ((b"\x98", "sos"), (b"\x9e", "pm")):
                with self.subTest(prefix=prefix, introducer=introducer):
                    self.assert_trace(
                        prefix + introducer + b"a\x9cX",
                        [(event, b"a"), ("text", b"X")],
                    )
        self.terminal.write(b"\x1b%G")
        self.terminal.parser_trace_clear()

    def test_sos_and_pm_payload_is_inert_until_st(self):
        payload = bytes((*range(0x00, 0x18), 0x19, *range(0x1C, 0x7F)))
        for introducer, event in ((b"\x1bX", "sos"), (b"\x1b^", "pm")):
            with self.subTest(introducer=introducer):
                self.assert_trace(
                    introducer + payload + b"\x1b\\X",
                    [(event, payload), ("text", b"X")],
                )
                self.assertEqual(self.terminal.read_actions(), [])

    def test_osc_introducers_replace_every_parser_state(self):
        for prefix in STATE_PREFIXES:
            with self.subTest(prefix=prefix, introducer=b"\x1b]"):
                self.assert_trace(
                    prefix + b"\x1b]999;a\x1b\\X",
                    [("osc", b"999;a"), ("text", b"X")],
                )

        self.terminal.write(b"\x1b%@")
        for prefix in STATE_PREFIXES:
            with self.subTest(prefix=prefix, introducer=b"\x9d"):
                self.assert_trace(
                    prefix + b"\x9d999;a\x9cX",
                    [("osc", b"999;a"), ("text", b"X")],
                )
        self.terminal.write(b"\x1b%G")
        self.terminal.parser_trace_clear()

    def test_osc_ignores_c0_without_losing_payload(self):
        ignored = (*range(0x00, 0x07), *range(0x08, 0x18), 0x19,
                   *range(0x1C, 0x20))
        for control in ignored:
            with self.subTest(control=control):
                self.assert_trace(
                    b"\x1b]999;a" + bytes((control,)) + b"b\x1b\\X",
                    [("osc", b"999;ab"), ("text", b"X")],
                )
                self.assertEqual(self.terminal.read_actions(), ["OSC 999 6162"])

    def test_osc_collects_every_ascii_payload_byte(self):
        for byte in range(0x20, 0x7F):
            with self.subTest(byte=byte):
                value = bytes((byte,))
                self.assert_trace(
                    b"\x1b]999;" + value + b"\x1b\\X",
                    [("osc", b"999;" + value), ("text", b"X")],
                )

    def test_dcs_introducers_replace_every_parser_state(self):
        for prefix in STATE_PREFIXES:
            with self.subTest(prefix=prefix, introducer=b"\x1bP"):
                self.assert_trace(
                    prefix + b"\x1bP$qm\x1b\\X",
                    [("dcs", b"$qm"), ("text", b"X")],
                )

        self.terminal.write(b"\x1b%@")
        for prefix in STATE_PREFIXES:
            with self.subTest(prefix=prefix, introducer=b"\x90"):
                self.assert_trace(
                    prefix + b"\x90$qm\x9cX",
                    [("dcs", b"$qm"), ("text", b"X")],
                )
        self.terminal.write(b"\x1b%G")
        self.terminal.parser_trace_clear()

    def test_dcs_entry_ignores_c0_and_del(self):
        for control in (*C0_EXECUTE, 0x7F):
            with self.subTest(control=control):
                self.assert_trace(
                    b"\x1bP" + bytes((control,)) + b"$qm\x1b\\X",
                    [("dcs", b"$qm"), ("text", b"X")],
                )
                self.assertEqual(self.terminal.read_actions(), [])

    def test_dcs_entry_collects_parameters_and_private_markers(self):
        for digit in range(10):
            with self.subTest(digit=digit):
                value = str(digit).encode()
                self.assert_trace(
                    b"\x1bP" + value + b"$qm\x1b\\",
                    [("dcs", value + b"$qm")],
                )
        self.assert_trace(b"\x1bP;$qm\x1b\\", [("dcs", b";$qm")])
        for marker in b"<=>?":
            with self.subTest(marker=marker):
                value = bytes((marker,))
                self.assert_trace(
                    b"\x1bP" + value + b"$qm\x1b\\",
                    [("dcs", value + b"$qm")],
                )

    def test_dcs_param_ignores_c0_and_del(self):
        for control in (*C0_EXECUTE, 0x7F):
            with self.subTest(control=control):
                self.assert_trace(
                    b"\x1bP1" + bytes((control,)) + b"$qm\x1b\\X",
                    [("dcs", b"1$qm"), ("text", b"X")],
                )
                self.assertEqual(self.terminal.read_actions(), [])

    def test_dcs_param_accumulates_digits_and_empty_parameters(self):
        for digit in range(10):
            with self.subTest(digit=digit):
                value = str(digit).encode()
                self.assert_trace(
                    b"\x1bP1" + value + b"$qm\x1b\\",
                    [("dcs", b"1" + value + b"$qm")],
                )
        self.assert_trace(b"\x1bP1;$qm\x1b\\", [("dcs", b"1;$qm")])


if __name__ == "__main__":
    unittest.main()
