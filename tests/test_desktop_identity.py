# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


class DesktopIdentityTests(unittest.TestCase):
    def setUp(self):
        self.desktop = (ROOT / "shitty.desktop").read_text()

    def desktop_value(self, key):
        match = re.search(
            rf"^{re.escape(key)}=(.+)$",
            self.desktop,
            re.MULTILINE,
        )
        self.assertIsNotNone(match)
        return match.group(1)

    def test_wayland_app_id_matches_desktop_file_id(self):
        source = (ROOT / "application.cpp").read_text()
        match = re.search(
            r'glfwWindowHintString\(GLFW_WAYLAND_APP_ID,\s*"([^"]+)"\)',
            source,
        )

        self.assertIsNotNone(match)
        self.assertEqual(match.group(1), (ROOT / "shitty.desktop").stem)

    def test_desktop_entry_launches_st(self):
        self.assertEqual(self.desktop_value("Exec"), "st")

    def test_desktop_icon_matches_installed_svg(self):
        self.assertEqual(self.desktop_value("Icon"), (ROOT / "shitty.svg").stem)


if __name__ == "__main__":
    unittest.main()
