import errno
import unittest

from harness import Zutty


class PtyStreamingTest(unittest.TestCase):
    def test_csi_split_across_readiness_cycles_keeps_parser_state(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.script_pty_reads(b"\x1b[3", ("error", errno.EAGAIN))
            self.assertFalse(terminal.read_pty())
            self.assertEqual(terminal.snapshot().lines[0], "        ")

            terminal.script_pty_reads(b"1mR", ("error", errno.EAGAIN))
            self.assertFalse(terminal.read_pty())
            cell = terminal.snapshot().cell(0, 0)
            self.assertEqual(cell.char, "R")
            self.assertEqual(cell.foreground, (205, 0, 0))

    def test_utf8_split_across_readiness_cycles_is_not_replaced(self):
        encoded = "界".encode()
        with Zutty(columns=8, rows=2) as terminal:
            terminal.script_pty_reads(encoded[:2], ("error", errno.EAGAIN))
            self.assertFalse(terminal.read_pty())
            self.assertEqual(terminal.snapshot().lines[0], "        ")

            terminal.script_pty_reads(encoded[2:], ("error", errno.EAGAIN))
            self.assertFalse(terminal.read_pty())
            self.assertEqual(terminal.snapshot().cell(0, 0).char, "界")

    def test_osc_split_across_readiness_cycles_dispatches_only_when_complete(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.script_pty_reads(
                b"\x1b]2;split title", ("error", errno.EAGAIN)
            )
            self.assertFalse(terminal.read_pty())
            self.assertEqual(terminal.read_actions(), [])

            terminal.script_pty_reads(b"\x1b\\", ("error", errno.EAGAIN))
            self.assertFalse(terminal.read_pty())
            self.assertEqual(
                terminal.read_actions(),
                ["OSC 2 73706c6974207469746c65"],
            )


if __name__ == "__main__":
    unittest.main()
