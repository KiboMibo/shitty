# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

import unittest

from harness import TEST_PLATFORM, Shitty


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
            terminal.write(b"\x1b]52;p;bmV3LXByaW1hcnk=\x1b\\")
            self.assertEqual(terminal.get_selection(primary=True), b"new-primary")
            self.assertEqual(
                terminal.get_selection(primary=False), b"old-clipboard"
            )

            terminal.write(b"\x1b]52;c;bmV3LWNsaXBib2FyZA==\x1b\\")
            self.assertEqual(terminal.get_selection(primary=True), b"new-primary")
            self.assertEqual(
                terminal.get_selection(primary=False), b"new-clipboard"
            )

    def test_copy_shortcut_follows_platform_convention(self):
        with Shitty() as terminal:
            for modifiers, clipboard, forwarded in (
                (8, b"selected", b""),
                (1 | 2, b"external", b"\x03"),
            ) if TEST_PLATFORM == "cocoa" else (
                (8, b"external", b""),
                (1 | 2, b"selected", b""),
            ):
                terminal.set_system_clipboard(b"external")
                terminal.set_primary_selection(b"selected")
                terminal.frontend_key_event(ord("C"), 1, modifiers=modifiers)
                terminal.frontend_key_event(ord("C"), 0, modifiers=modifiers)
                self.assertEqual(terminal.get_selection(primary=True), b"selected")
                self.assertEqual(terminal.get_selection(primary=False), clipboard)
                self.assertEqual(terminal.read_input(), forwarded)

    def test_paste_shortcut_follows_platform_convention(self):
        with Shitty() as terminal:
            for modifiers, forwarded in (
                (8, b"pasted text"),
                (1 | 2, b"\x16"),
            ) if TEST_PLATFORM == "cocoa" else (
                (8, b""),
                (1 | 2, b"pasted text"),
            ):
                terminal.set_system_clipboard(b"pasted text")
                terminal.frontend_key_event(ord("V"), 1, modifiers=modifiers)
                terminal.frontend_key_event(ord("V"), 0, modifiers=modifiers)
                self.assertEqual(terminal.read_input(), forwarded)

    def test_super_c_with_other_modifiers_is_not_copy(self):
        with Shitty() as terminal:
            terminal.set_system_clipboard(b"external")
            terminal.set_primary_selection(b"selected")
            terminal.frontend_key_event(ord("C"), 1, modifiers=8 | 2)
            terminal.frontend_key_event(ord("C"), 0, modifiers=8 | 2)
            self.assertEqual(terminal.get_selection(primary=False), b"external")
            self.assertEqual(terminal.read_input(), b"\x03")


if __name__ == "__main__":
    unittest.main()
