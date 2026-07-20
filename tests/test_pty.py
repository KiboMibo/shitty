import sys
import unittest

from harness import Zutty


class PtyTest(unittest.TestCase):
    def test_nonblocking_read_without_data_is_not_eof(self):
        with Zutty(columns=8, rows=2) as terminal:
            self.assertFalse(terminal.read_pty())

    def test_pump_without_output_does_not_publish_a_frame(self):
        with Zutty(columns=8, rows=2) as terminal:
            before = terminal.snapshot().refresh_count
            terminal.pump()
            self.assertEqual(terminal.snapshot().refresh_count, before)

    def test_failed_present_keeps_terminal_damage_for_retry(self):
        with Zutty(columns=8, rows=2) as terminal:
            before = terminal.snapshot()
            terminal.fail_next_present()
            terminal.write(b"retained")
            failed = terminal.snapshot()
            self.assertEqual(failed.refresh_count, before.refresh_count)
            terminal.present()
            retried = terminal.snapshot()
            self.assertEqual(retried.lines[0], "retained")
            self.assertEqual(retried.refresh_count, before.refresh_count + 1)

    def test_one_nonblocking_drain_publishes_one_frame(self):
        with Zutty(columns=80, rows=24) as terminal:
            terminal.spawn(
                sys.executable,
                "-c",
                "import os,time; os.write(1, b'A' * 65536); time.sleep(1)",
            )
            before = terminal.snapshot().refresh_count
            terminal.wait_read_pty()
            after = terminal.snapshot().refresh_count
            self.assertEqual(after, before + 1)


if __name__ == "__main__":
    unittest.main()
