import unittest

from harness import Zutty


class OscProtocolTest(unittest.TestCase):
    def test_hyperlink_control_preserves_arbitrary_uri_bytes(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(b"\x1b]8;;https://example/\xc2x\x1b\\X")
            self.assertEqual(
                terminal.hyperlink_bytes(0, 0),
                b"https://example/\xc2x",
            )

    def test_osc52_selectors(self):
        cases = {
            b";WA==": (True, True),
            b"s;WA==": (True, False),
            b"p;WA==": (True, False),
            b"c;WA==": (False, True),
            b"pc;WA==": (True, True),
            b"q;WA==": (False, False),
        }
        with Zutty(columns=8, rows=2) as terminal:
            for argument, destinations in cases.items():
                with self.subTest(argument=argument):
                    valid, query, primary, clipboard, content = terminal.osc52(
                        argument
                    )
                    self.assertTrue(valid)
                    self.assertFalse(query)
                    self.assertEqual((primary, clipboard), destinations)
                    self.assertEqual(content, b"X")

    def test_osc52_query_and_malformed_request(self):
        with Zutty(columns=8, rows=2) as terminal:
            self.assertEqual(
                terminal.osc52(b"c;?"),
                (True, True, False, True, b""),
            )
            self.assertEqual(
                terminal.osc52(b"missing-separator"),
                (False, False, False, False, b""),
            )
            self.assertEqual(
                terminal.osc52(b"c;SGVsbG8!"),
                (False, False, False, False, b""),
            )

    def test_osc52_reply_encodes_arbitrary_bytes(self):
        with Zutty(columns=8, rows=2) as terminal:
            self.assertEqual(
                terminal.osc52_reply(b"hello\x00world"),
                b"\x1b]52;;aGVsbG8Ad29ybGQ=\x1b\\",
            )
            self.assertEqual(
                terminal.osc52_reply(b"clipboard", b"c"),
                b"\x1b]52;c;Y2xpcGJvYXJk\x1b\\",
            )

    def test_osc7_file_url_and_percent_decoding(self):
        with Zutty(columns=8, rows=2) as terminal:
            self.assertEqual(
                terminal.osc7_cwd(b"file://host/tmp/a%20b%2Fc"),
                b"/tmp/a b/c",
            )
            self.assertEqual(terminal.osc7_cwd(b"/plain/path"), b"/plain/path")

    def test_osc7_rejects_non_absolute_paths(self):
        with Zutty(columns=8, rows=2) as terminal:
            self.assertEqual(terminal.osc7_cwd(b"relative/path"), b"")
            self.assertEqual(terminal.osc7_cwd(b"file://host"), b"")


if __name__ == "__main__":
    unittest.main()
