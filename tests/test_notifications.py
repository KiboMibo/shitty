import base64
import unittest

from harness import Zutty


class NotificationProtocolTest(unittest.TestCase):
    def test_default_title_payload_dispatches_a_notification(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(b"\x1b]99;;Hello world\x1b\\")

            self.assertEqual(
                terminal.read_actions(),
                ["NOTIFY  48656c6c6f20776f726c64 "],
            )

    def test_body_only_is_promoted_to_title(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(b"\x1b]99;p=body;Body only\x1b\\")

            self.assertEqual(
                terminal.read_actions(),
                ["NOTIFY  426f6479206f6e6c79 "],
            )

    def test_plain_chunks_concatenate_until_done(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(
                b"\x1b]99;i=job:d=0;Build \x1b\\"
                b"\x1b]99;i=job:d=0;is \x1b\\"
            )
            self.assertEqual(terminal.read_actions(), [])

            terminal.write(b"\x1b]99;i=job;done\x1b\\")
            self.assertEqual(
                terminal.read_actions(),
                ["NOTIFY 6a6f62 4275696c6420697320646f6e65 "],
            )

    def test_base64_chunks_can_split_between_quartets(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(
                b"\x1b]99;i=job:e=1:d=0;QnV\x1b\\"
                b"\x1b]99;i=job:e=1;pbGQ=\x1b\\"
            )

            self.assertEqual(
                terminal.read_actions(),
                ["NOTIFY 6a6f62 4275696c64 "],
            )

    def test_title_and_body_chunks_are_accumulated_independently(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(
                b"\x1b]99;i=job:p=title:d=0;Build\x1b\\"
                b"\x1b]99;i=job:p=body:d=0;half \x1b\\"
                b"\x1b]99;i=job:p=body;done\x1b\\"
            )

            self.assertEqual(
                terminal.read_actions(),
                ["NOTIFY 6a6f62 4275696c64 68616c6620646f6e65"],
            )

    def test_invalid_identifiers_are_rejected(self):
        with Zutty(columns=8, rows=2) as terminal:
            for identifier in (b"bad=id", b"bad value", b"bad,comma"):
                terminal.write(
                    b"\x1b]99;i=" + identifier + b";ignored\x1b\\"
                )

            self.assertEqual(terminal.read_actions(), [])

    def test_invalid_known_metadata_values_are_rejected(self):
        with Zutty(columns=8, rows=2) as terminal:
            for metadata in (b"d=2", b"d=yes", b"e=2", b"e=yes"):
                terminal.write(b"\x1b]99;" + metadata + b";ignored\x1b\\")

            self.assertEqual(terminal.read_actions(), [])

    def test_unknown_metadata_key_is_ignored_for_extensibility(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(b"\x1b]99;z=future;Hello\x1b\\")

            self.assertEqual(
                terminal.read_actions(), ["NOTIFY  48656c6c6f "]
            )

    def test_unknown_payload_type_is_ignored(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(b"\x1b]99;i=job:p=future;ignored\x1b\\")

            self.assertEqual(terminal.read_actions(), [])

    def test_plain_and_base64_payloads_require_escape_safe_utf8(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(b"\x1b]99;;bad\xffutf8\x1b\\")
            terminal.write(b"\x1b]99;e=1;" + base64.b64encode(b"bad\xffutf8") + b"\x1b\\")

            self.assertEqual(terminal.read_actions(), [])

    def test_payload_chunk_limits_accept_2048_plain_and_4096_encoded(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(b"\x1b]99;;" + b"A" * 2048 + b"\x1b\\")
            plain = terminal.read_actions()
            self.assertEqual(len(plain), 1)
            self.assertEqual(plain[0], "NOTIFY  " + (b"A" * 2048).hex() + " ")

            encoded = base64.b64encode(b"B" * 3072)
            self.assertEqual(len(encoded), 4096)
            terminal.write(b"\x1b]99;e=1;" + encoded + b"\x1b\\")
            encoded_actions = terminal.read_actions()
            self.assertEqual(len(encoded_actions), 1)
            self.assertEqual(
                encoded_actions[0], "NOTIFY  " + (b"B" * 3072).hex() + " "
            )

    def test_payload_chunk_limits_reject_oversized_plain_and_encoded(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(b"\x1b]99;;" + b"A" * 2049 + b"\x1b\\")
            terminal.write(
                b"\x1b]99;e=1;"
                + base64.b64encode(b"B" * 3073)
                + b"\x1b\\"
            )

            self.assertEqual(terminal.read_actions(), [])

    def test_accumulated_payload_quota_is_8192_bytes(self):
        with Zutty(columns=8, rows=2) as terminal:
            for _ in range(3):
                terminal.write(b"\x1b]99;i=max:d=0;" + b"A" * 2048 + b"\x1b\\")
            terminal.write(b"\x1b]99;i=max;" + b"A" * 2048 + b"\x1b\\")
            accepted = terminal.read_actions()
            self.assertEqual(len(accepted), 1)
            self.assertEqual(
                accepted[0], "NOTIFY 6d6178 " + (b"A" * 8192).hex() + " "
            )

            for _ in range(4):
                terminal.write(b"\x1b]99;i=over:d=0;" + b"B" * 2048 + b"\x1b\\")
            terminal.write(b"\x1b]99;i=over;B\x1b\\")
            self.assertEqual(terminal.read_actions(), [])

    def test_close_requires_a_live_nonempty_identifier(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(
                b"\x1b]99;p=close;\x1b\\"
                b"\x1b]99;i=missing:p=close;\x1b\\"
            )
            self.assertEqual(terminal.read_actions(), [])

            terminal.write(
                b"\x1b]99;i=job;Title\x1b\\"
                b"\x1b]99;i=job:p=close;\x1b\\"
                b"\x1b]99;i=job:p=close;\x1b\\"
            )
            self.assertEqual(
                terminal.read_actions(),
                ["NOTIFY 6a6f62 5469746c65 ", "NOTIFY_CLOSE 6a6f62"],
            )

    def test_reusing_identifier_dispatches_an_update(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(
                b"\x1b]99;i=job;First\x1b\\"
                b"\x1b]99;i=job;Second\x1b\\"
            )

            self.assertEqual(
                terminal.read_actions(),
                ["NOTIFY 6a6f62 4669727374 ", "NOTIFY 6a6f62 5365636f6e64 "],
            )

    def test_capability_reply_declares_only_implemented_payloads(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(b"\x1b]99;i=query:p=?;\x1b\\")

            self.assertEqual(
                terminal.read_input(),
                b"\x1b]99;i=query:p=?;p=title,body,close\x1b\\",
            )

    def test_invalid_query_identifier_cannot_be_reflected(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(b"\x1b]99;i=bad=id:p=?;\x1b\\")

            self.assertEqual(terminal.read_input(), b"")


if __name__ == "__main__":
    unittest.main()
