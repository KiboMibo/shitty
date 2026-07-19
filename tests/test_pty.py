import unittest

from harness import Zutty


class PtyTest(unittest.TestCase):
    def test_nonblocking_read_without_data_is_not_eof(self):
        with Zutty(columns=8, rows=2) as terminal:
            self.assertFalse(terminal.read_pty())


if __name__ == "__main__":
    unittest.main()
