# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

import unittest

from harness import Shitty


UPSTREAM_CASES = (
    "ShellIntegration.OSC_133",
    "ShellIntegration.SETMARK",
    "SemanticBlockProtocol.LineFlags",
    "SemanticBlockProtocol.lastCommandBlockWithoutMode2034",
    "SemanticBlockProtocol.lastCommandBlockWithoutShellIntegration",
    "SemanticBlockProtocol.outputWithoutTrailingNewline",
    "SemanticBlockProtocol.outputThatFitsOnTheLineItStartedOn",
    "SemanticBlockProtocol.marksSurviveResize",
    "SemanticBlockProtocol.noCommandAtAllIsNoBlock",
    "SemanticBlockProtocol.TrackerMetadata",
    "SemanticBlockProtocol.DECRQM",
    "SemanticBlockProtocol.QueryDisabled",
    "SemanticBlockProtocol.QueryLastCommand",
    "SemanticBlockProtocol.QueryLastNCommands",
    "SemanticBlockProtocol.QueryInProgress",
    "SemanticBlockProtocol.NoCompletedCommands",
    "SemanticBlockProtocol.TokenOnEnable",
    "SemanticBlockProtocol.TokenInvalidatedOnDisable",
    "SemanticBlockProtocol.QueryWithoutToken",
    "SemanticBlockProtocol.QueryWithWrongToken",
    "SemanticBlockProtocol.TokenChangesOnReEnable",
    "ShellIntegration.OSC_133_B stamps PromptEnd on the logical head",
    "ShellIntegration.OSC_133_B on a multi-line prompt marks the line it ended on",
    "ShellIntegration.PromptEnd survives reflow",
    "ShellIntegration.LineFlags formatter names PromptEnd",
    "ShellIntegration.livePromptSpan reports a fresh prompt",
    "ShellIntegration.livePromptSpan spans a reflowed multi-line prompt",
    "ShellIntegration.livePromptSpan declines while a command is running",
    "ShellIntegration.livePromptSpan reports a prompt again once the command finished",
    "ShellIntegration.livePromptSpan reports no integration for a plain shell",
    "ShellIntegration.livePromptSpan is silent on the alternate screen",
)


def semantics(snapshot, row, begin, end):
    return [
        snapshot.cell(column, row).semantic
        for column in range(begin, end)
    ]


class ContourShellIntegrationTest(unittest.TestCase):
    def test_upstream_inventory_has_all_31_cases(self):
        self.assertEqual(len(UPSTREAM_CASES), 31)
        self.assertEqual(len(set(UPSTREAM_CASES)), 31)

    def test_osc_133_metadata_and_transitions(self):
        with Shitty(columns=24, rows=3) as terminal:
            terminal.write(
                b"\x1b]133;A;click_events=1\x1b\\prompt"
                b"\x1b]133;B;future=value\x1b\\input"
                b"\x1b]133;C;cmdline_url=echo%20ok\x1b\\output"
                b"\x1b]133;D;123\x1b\\idle"
            )
            snapshot = terminal.snapshot()

            self.assertEqual(snapshot.lines[0][:21], "promptinputoutputidle")
            self.assertEqual(semantics(snapshot, 0, 0, 6), [1] * 6)
            self.assertEqual(semantics(snapshot, 0, 6, 11), [2] * 5)
            self.assertEqual(semantics(snapshot, 0, 11, 17), [3] * 6)
            self.assertEqual(semantics(snapshot, 0, 17, 21), [0] * 4)

    def test_setmark_marks_in_place_without_forcing_a_new_line(self):
        with Shitty(columns=8, rows=2) as terminal:
            terminal.write(b"x\x1b[>My")
            snapshot = terminal.snapshot()

            self.assertEqual(snapshot.lines[0][:2], "xy")
            self.assertEqual(semantics(snapshot, 0, 0, 2), [0, 1])

    def test_prompt_end_boundary_survives_forward_and_reverse_reflow(self):
        with Shitty(columns=20, rows=4, save_lines=8) as terminal:
            terminal.write(
                b"\x1b]133;A\x1b\\0123456789abcdef> "
                b"\x1b]133;B\x1b\\command"
            )

            for columns in (10, 40, 7, 20):
                terminal.resize(columns, 4)
                snapshot = terminal.snapshot()
                cells = [
                    cell.semantic
                    for cell in snapshot.cells
                    if cell.drawn
                ]
                self.assertEqual(cells, [1] * 18 + [2] * 7)

    def test_multiline_prompt_keeps_prompt_and_input_regions_distinct(self):
        with Shitty(columns=20, rows=4) as terminal:
            terminal.write(
                b"\x1b]133;A\x1b\\first\r\n> "
                b"\x1b]133;B\x1b\\ls"
            )
            snapshot = terminal.snapshot()

            self.assertEqual(semantics(snapshot, 0, 0, 5), [1] * 5)
            self.assertEqual(semantics(snapshot, 1, 0, 2), [1] * 2)
            self.assertEqual(semantics(snapshot, 1, 2, 4), [2] * 2)

    def test_running_command_and_next_prompt_have_disjoint_regions(self):
        with Shitty(columns=20, rows=4) as terminal:
            terminal.write(
                b"\x1b]133;A\x1b\\$ "
                b"\x1b]133;B\x1b\\ls\r\n"
                b"\x1b]133;C\x1b\\file\r\n"
                b"\x1b]133;D;0\x1b\\"
                b"\x1b]133;A\x1b\\$ "
                b"\x1b]133;B\x1b\\x"
            )
            snapshot = terminal.snapshot()

            self.assertEqual(semantics(snapshot, 0, 0, 2), [1, 1])
            self.assertEqual(semantics(snapshot, 0, 2, 4), [2, 2])
            self.assertEqual(semantics(snapshot, 1, 0, 4), [3] * 4)
            self.assertEqual(semantics(snapshot, 2, 0, 2), [1, 1])
            self.assertEqual(snapshot.cell(2, 2).semantic, 2)

    def test_plain_shell_has_no_semantic_regions(self):
        with Shitty(columns=8, rows=2) as terminal:
            terminal.write(b"$ plain")
            snapshot = terminal.snapshot()
            self.assertEqual(semantics(snapshot, 0, 0, 7), [0] * 7)

    def test_alternate_screen_does_not_inherit_live_prompt_state(self):
        with Shitty(columns=10, rows=3) as terminal:
            terminal.write(
                b"\x1b]133;A\x1b\\$ "
                b"\x1b]133;B\x1b\\"
                b"\x1b[?1049happ"
            )
            alternate = terminal.snapshot()
            self.assertEqual(semantics(alternate, 0, 0, 3), [0, 0, 0])

            terminal.write(b"\x1b[?1049lcmd")
            primary = terminal.snapshot()
            self.assertEqual(primary.lines[0][:5], "$ cmd")
            self.assertEqual(semantics(primary, 0, 0, 5), [1, 1, 2, 2, 2])


if __name__ == "__main__":
    unittest.main()
