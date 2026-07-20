import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


class DesktopIdentityTests(unittest.TestCase):
    def test_wayland_app_id_matches_desktop_file_id(self):
        source = (ROOT / "application.cpp").read_text()
        match = re.search(
            r'glfwWindowHintString\(GLFW_WAYLAND_APP_ID,\s*"([^"]+)"\)',
            source,
        )

        self.assertIsNotNone(match)
        self.assertEqual(match.group(1), (ROOT / "zutty.desktop").stem)


if __name__ == "__main__":
    unittest.main()
