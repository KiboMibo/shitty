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
        with Shitty(columns=8, rows=3) as terminal:
            terminal.write(b"abcd")
            for turn, (modifiers, clipboard, forwarded) in enumerate((
                (8, b"abc", b""),
                (1 | 2, b"external", b"\x03"),
            ) if TEST_PLATFORM == "cocoa" else (
                (8, b"external", b""),
                (1 | 2, b"abc", b""),
            )):
                terminal.set_system_clipboard(b"external")
                # A real window selection: the chord copies it.
                terminal.button(0, True, x=2, y=2, time=10 * turn + 1)
                terminal.button(0, False, x=2, y=2, time=10 * turn + 1.01)
                terminal.button(1, True, x=5, y=2, time=10 * turn + 2)
                terminal.button(1, False, x=5, y=2, time=10 * turn + 2.01)
                terminal.frontend_key_event(ord("C"), 1, modifiers=modifiers)
                terminal.frontend_key_event(ord("C"), 0, modifiers=modifiers)
                self.assertEqual(terminal.get_selection(primary=True), b"abc")
                self.assertEqual(terminal.get_selection(primary=False), clipboard)
                self.assertEqual(terminal.read_input(), forwarded)

    def test_copy_shortcut_without_selection_keeps_the_clipboard(self):
        # A TUI that selects for itself fills the clipboard through
        # OSC 52; the copy chord pressed out of habit right after must
        # not clobber it with a stale staged selection.
        with Shitty() as terminal:
            modifiers = 8 if TEST_PLATFORM == "cocoa" else 1 | 2
            terminal.set_primary_selection(b"stale")
            terminal.write(b"\x1b]52;c;ZnJlc2g=\x1b\\")
            self.assertEqual(terminal.get_selection(primary=False), b"fresh")
            terminal.frontend_key_event(ord("C"), 1, modifiers=modifiers)
            terminal.frontend_key_event(ord("C"), 0, modifiers=modifiers)
            self.assertEqual(terminal.get_selection(primary=False), b"fresh")
            self.assertEqual(terminal.get_selection(primary=True), b"stale")
            self.assertEqual(terminal.read_input(), b"")

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
