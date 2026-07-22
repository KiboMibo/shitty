import unittest

from harness import Shitty


class ClipboardTest(unittest.TestCase):
    def test_primary_ownership_and_auto_copy_are_independent(self):
        with Shitty() as terminal:
            terminal.set_system_clipboard(b"external")
            terminal.set_primary_selection(b"primary", auto_copy=False)
            self.assertEqual(terminal.get_selection(primary=True), b"primary")
            self.assertEqual(terminal.get_selection(primary=False), b"external")

            terminal.set_primary_selection(b"mirrored", auto_copy=True)
            self.assertEqual(terminal.get_selection(primary=True), b"mirrored")
            self.assertEqual(terminal.get_selection(primary=False), b"mirrored")

    def test_osc52_writes_only_requested_owned_selections(self):
        with Shitty() as terminal:
            terminal.set_primary_selection(b"old-primary")
            terminal.set_system_clipboard(b"old-clipboard")
            terminal.apply_clipboard_osc52(b"p;bmV3LXByaW1hcnk=")
            self.assertEqual(terminal.get_selection(primary=True), b"new-primary")
            self.assertEqual(
                terminal.get_selection(primary=False), b"old-clipboard"
            )

            terminal.apply_clipboard_osc52(b"c;bmV3LWNsaXBib2FyZA==")
            self.assertEqual(terminal.get_selection(primary=True), b"new-primary")
            self.assertEqual(
                terminal.get_selection(primary=False), b"new-clipboard"
            )


if __name__ == "__main__":
    unittest.main()
