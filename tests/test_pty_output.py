import errno
import unittest

from harness import Zutty


class PtyOutputTest(unittest.TestCase):
    def test_partial_writes_resume_at_exact_unsent_byte_after_backpressure(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.script_pty_writes(2, 3, ("error", errno.EAGAIN))
            terminal.input(b"abcdefghi")
            self.assertEqual(terminal.read_written_pty(), b"abcde")
            self.assertEqual(terminal.pending_output(), 4)

            terminal.script_pty_writes(1, 3)
            self.assertTrue(terminal.flush_output_result())
            self.assertEqual(terminal.read_written_pty(), b"fghi")
            self.assertEqual(terminal.pending_output(), 0)

    def test_interrupted_write_is_retried_without_duplication(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.script_pty_writes(
                ("error", errno.EINTR), 1, ("error", errno.EINTR), 8
            )
            terminal.input(b"retry")
            self.assertEqual(terminal.read_written_pty(), b"retry")
            self.assertEqual(terminal.pending_output(), 0)

    def test_fatal_write_keeps_payload_available_for_later_retry(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.script_pty_writes(("error", errno.EPIPE))
            terminal.input(b"retained")
            self.assertEqual(terminal.read_written_pty(), b"")
            self.assertEqual(terminal.pending_output(), 8)

            terminal.script_pty_writes(8)
            self.assertTrue(terminal.flush_output_result())
            self.assertEqual(terminal.read_written_pty(), b"retained")


if __name__ == "__main__":
    unittest.main()
