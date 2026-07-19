import unittest

from harness import Zutty


class ProtocolTest(unittest.TestCase):
    def test_device_attributes_and_status_reports(self):
        with Zutty(columns=10, rows=3) as terminal:
            terminal.write(b"\x1b[2;4H\x1b[c\x1b[>c\x1b[5n\x1b[6n")
            self.assertEqual(
                terminal.read_input(),
            b"\x1b[?64;1;2;6;8;9;15;21;22;28;29c"
                b"\x1b[>41;14;0c"
                b"\x1b[0n"
                b"\x1b[2;4R",
            )

    def test_decrqss_reports_compatibility_level(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(b"\x1bP$q\"p\x1b\\")
            self.assertEqual(
                terminal.read_input(),
                b"\x1bP1$r64;1\"p\x1b\\",
            )

    def test_decrqss_reports_current_terminal_state(self):
        with Zutty(columns=10, rows=6) as terminal:
            terminal.write(
                b"\x1b[1;3;4;31;42m"
                b"\x1b[2;5r"
                b"\x1b[?69h\x1b[3;8s"
                b"\x1b[5 q"
                b"\x1bP$qm\x1b\\"
                b"\x1bP$qr\x1b\\"
                b"\x1bP$qs\x1b\\"
                b"\x1bP$q q\x1b\\"
                b"\x1bP$q\"q\x1b\\"
            )
            self.assertEqual(
                terminal.read_input(),
                b"\x1bP1$r0;1;3;4;38;5;1;48;5;2m\x1b\\"
                b"\x1bP1$r2;5r\x1b\\"
                b"\x1bP1$r3;8s\x1b\\"
                b"\x1bP1$r5 q\x1b\\"
                b"\x1bP1$r0\"q\x1b\\",
            )

    def test_decrqm_reports_ansi_and_private_mode_state(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(
                b"\x1b[?7$p"
                b"\x1b[?7l\x1b[?7$p"
                b"\x1b[4$p"
                b"\x1b[4h\x1b[4$p"
                b"\x1b[?9999$p"
            )
            self.assertEqual(
                terminal.read_input(),
                b"\x1b[?7;1$y"
                b"\x1b[?7;2$y"
                b"\x1b[4;2$y"
                b"\x1b[4;1$y"
                b"\x1b[?9999;0$y",
            )

    def test_xtgettcap_reports_declared_capabilities(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(b"\x1bP+q544e;436f;524742;626f677573\x1b\\")
            self.assertEqual(
                terminal.read_input(),
                b"\x1bP1+r544e=787465726d2d323536636f6c6f72\x1b\\"
                b"\x1bP1+r436f=323536\x1b\\"
                b"\x1bP1+r524742=38\x1b\\"
                b"\x1bP0+r626f677573\x1b\\",
            )

    def test_version_and_tertiary_device_attributes(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(b"\x1b[>q\x1b[=c")
            self.assertEqual(
                terminal.read_input(),
                b"\x1bP>|Zutty 0.14\x1b\\"
                b"\x1bP!|00000000\x1b\\",
            )

    def test_xtwinops_reports_terminal_and_cell_dimensions(self):
        with Zutty(columns=10, rows=4) as terminal:
            terminal.write(b"\x1b[14t\x1b[16t\x1b[18t")
            self.assertEqual(
                terminal.read_input(),
                b"\x1b[4;4;10t"
                b"\x1b[6;1;1t"
                b"\x1b[8;4;10t",
            )

    def test_xtwinops_reports_position_window_and_monitor_geometry(self):
        with Zutty(columns=10, rows=4) as terminal:
            terminal.write(
                b"\x1b[11t\x1b[13t\x1b[14;2t\x1b[15t\x1b[19t"
            )
            self.assertEqual(
                terminal.read_input(),
                b"\x1b[1t\x1b[3;10;20t\x1b[4;8;14t"
                b"\x1b[5;1080;1920t\x1b[9;1080;1920t",
            )

    def test_xtwinops_resize_requests_reach_window_backend(self):
        with Zutty(columns=10, rows=4) as terminal:
            terminal.write(b"\x1b[8;6;20t\x1b[4;120;320t")
            self.assertEqual(
                terminal.read_actions(),
                ["WINDOW 8 6 20", "WINDOW 4 120 320"],
            )

    def test_s7c1t_and_s8c1t_select_response_encoding(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(b"\x1b G\x1b[c")
            self.assertEqual(
                terminal.read_input(), b"\x9b?64;1;2;6;8;9;15;21;22;28;29c"
            )

            terminal.write(b"\x1b F\x1b[c")
            self.assertEqual(
                terminal.read_input(), b"\x1b[?64;1;2;6;8;9;15;21;22;28;29c"
            )

    def test_decscl_selects_response_control_width(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(b"\x1b[64;0\"p\x1b[c")
            self.assertEqual(
                terminal.read_input(), b"\x9b?64;1;2;6;8;9;15;21;22;28;29c"
            )

            terminal.write(b"\x1b[64;1\"p\x1b[c")
            self.assertEqual(
                terminal.read_input(), b"\x1b[?64;1;2;6;8;9;15;21;22;28;29c"
            )

    def test_decrqss_rejects_unsupported_queries(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(b"\x1bP$qz\x1b\\")
            self.assertEqual(terminal.read_input(), b"\x1bP0$r\x1b\\")

    def test_palette_and_dynamic_color_queries_reply(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(b"\x1b]4;1;?\a\x1b]10;?\x1b\\")
            reply = terminal.read_input()
            self.assertIn(b"\x1b]4;1;rgb:", reply)
            self.assertIn(b"\x1b]10;rgb:", reply)

    def test_palette_can_be_set_queried_and_reset_in_batches(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(
                b"\x1b]4;1;#010203;2;rgb:0404/0505/0606\x1b\\"
                b"\x1b]4;1;?;2;?\x1b\\"
            )
            reply = terminal.read_input()
            self.assertIn(b"\x1b]4;1;rgb:0101/0202/0303\x1b\\", reply)
            self.assertIn(b"\x1b]4;2;rgb:0404/0505/0606\x1b\\", reply)

            terminal.write(b"\x1b]104;1;2\x1b\\\x1b]4;1;?;2;?\x1b\\")
            self.assertNotEqual(terminal.read_input(), reply)

    def test_palette_changes_recolor_existing_indexed_cells_only(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(
                b"\x1b[38;5;1mI"
                b"\x1b[38;2;205;0;0mT"
                b"\x1b]4;1;#010203\x1b\\"
            )
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.cell(0, 0).foreground, (1, 2, 3))
            self.assertEqual(snapshot.cell(1, 0).foreground, (205, 0, 0))

            terminal.write(b"\x1b]104;1\x1b\\")
            self.assertEqual(
                terminal.snapshot().cell(0, 0).foreground,
                (205, 0, 0),
            )

    def test_dynamic_defaults_recolor_existing_default_cells(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(
                b"D\x1b[31;42mI\x1b[39;49m"
                b"\x1b]10;#010203\x1b\\"
                b"\x1b]11;#040506\x1b\\"
            )
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.cell(0, 0).foreground, (1, 2, 3))
            self.assertEqual(snapshot.cell(0, 0).background, (4, 5, 6))
            self.assertNotEqual(snapshot.cell(1, 0).foreground, (1, 2, 3))
            self.assertNotEqual(snapshot.cell(1, 0).background, (4, 5, 6))

    def test_dynamic_color_queries_are_independent_of_sgr(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(
                b"\x1b]10;#010203\x1b\\"
                b"\x1b]11;#040506\x1b\\"
                b"\x1b]12;#070809\x1b\\"
                b"\x1b]17;#0a0b0c\x1b\\"
                b"\x1b]19;#0d0e0f\x1b\\"
                b"\x1b[31;42m"
                b"\x1b]10;?\x1b\\\x1b]11;?\x1b\\\x1b]12;?\x1b\\"
                b"\x1b]17;?\x1b\\\x1b]19;?\x1b\\"
            )
            reply = terminal.read_input()
            for command, color in (
                (10, b"0101/0202/0303"),
                (11, b"0404/0505/0606"),
                (12, b"0707/0808/0909"),
                (17, b"0a0a/0b0b/0c0c"),
                (19, b"0d0d/0e0e/0f0f"),
            ):
                self.assertIn(
                    f"\x1b]{command};rgb:".encode() + color + b"\x1b\\",
                    reply,
                )

    def test_osc_title_cwd_and_bell_are_reported_as_actions(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(b"\x1b]2;hello\a\x1b]7;file:///tmp\x1b\\\a")
            self.assertEqual(
                terminal.read_actions(),
                ["OSC 2 68656c6c6f", "OSC 7 66696c653a2f2f2f746d70", "BELL"],
            )

    def test_title_stack_saves_restores_and_reports_both_titles(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(
                b"\x1b]1;icon-one\x1b\\\x1b]2;window-one\x1b\\"
                b"\x1b[22;0t"
                b"\x1b]1;icon-two\x1b\\\x1b]2;window-two\x1b\\"
                b"\x1b[20t\x1b[21t\x1b[23;0t"
            )
            self.assertEqual(
                terminal.read_input(),
                b"\x1b]Licon-two\x1b\\\x1b]lwindow-two\x1b\\",
            )
            self.assertEqual(
                terminal.read_actions(),
                [
                    "OSC 1 69636f6e2d6f6e65",
                    "OSC 2 77696e646f772d6f6e65",
                    "OSC 1 69636f6e2d74776f",
                    "OSC 2 77696e646f772d74776f",
                    "OSC 1 69636f6e2d6f6e65",
                    "OSC 2 77696e646f772d6f6e65",
                ],
            )

    def test_in_band_resize_reports_enable_and_completed_resizes(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(b"\x1b[?2048$p\x1b[?2048h")
            self.assertEqual(
                terminal.read_input(),
                b"\x1b[?2048;2$y\x1b[48;2;8;2;8t",
            )
            terminal.resize(11, 4)
            self.assertEqual(
                terminal.read_input(), b"\x1b[48;4;11;4;11t"
            )
            terminal.write(b"\x1b[?2048l")
            terminal.resize(12, 5)
            self.assertEqual(terminal.read_input(), b"")

    def test_osc133_marks_prompt_command_and_output_cells(self):
        with Zutty(columns=16, rows=2) as terminal:
            terminal.write(
                b"\x1b]133;A\x1b\\prompt "
                b"\x1b]133;B\x1b\\command"
                b"\x1b]133;C\x1b\\output"
                b"\x1b]133;D;0\x1b\\done"
            )
            snapshot = terminal.snapshot()
            self.assertEqual(snapshot.cell(0, 0).semantic, 1)
            self.assertEqual(snapshot.cell(7, 0).semantic, 2)
            self.assertEqual(snapshot.cell(14, 0).semantic, 3)
            self.assertEqual(snapshot.cell(4, 1).semantic, 0)

    def test_osc9_notifications_and_progress_are_dispatched(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(
                b"\x1b]2;build\x1b\\"
                b"\x1b]9;finished\x1b\\"
                b"\x1b]9;4;1;37\x1b\\"
            )
            self.assertEqual(
                terminal.read_actions(),
                [
                    "OSC 2 6275696c64",
                    "NOTIFY  6275696c64 66696e6973686564",
                    "PROGRESS 1 37",
                ],
            )

    def test_osc99_assembles_chunked_base64_notifications_and_closes(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(
                b"\x1b]99;i=job:p=title:e=1;QnVp\x1b\\"
                b"\x1b]99;i=job:p=title:e=1;bGQ=\x1b\\"
                b"\x1b]99;i=job:p=body:d=0;half \x1b\\"
                b"\x1b]99;i=job:p=body:d=1;done\x1b\\"
                b"\x1b]99;i=job:p=close;\x1b\\"
            )
            self.assertEqual(
                terminal.read_actions(),
                [
                    "NOTIFY 6a6f62 4275696c64 68616c6620646f6e65",
                    "NOTIFY_CLOSE 6a6f62",
                ],
            )

    def test_osc99_capability_query_gets_a_protocol_reply(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(b"\x1b]99;i=query:p=?;\x1b\\")
            reply = terminal.read_input()
            self.assertTrue(reply.startswith(b"\x1b]99;i=query:p=?;"))
            self.assertIn(b"title", reply)
            self.assertTrue(reply.endswith(b"\x1b\\"))

    def test_osc8_hyperlinks_are_attached_and_resolved(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(
                b"\x1b]8;id=x;https://example.test\x1b\\"
                b"link"
                b"\x1b]8;;\x1b\\ plain"
            )
            snapshot = terminal.snapshot()
            self.assertNotEqual(snapshot.cell(0, 0).hyperlink, 0)
            self.assertEqual(snapshot.cell(5, 0).hyperlink, 0)
            self.assertEqual(terminal.hyperlink(1, 0), "https://example.test")

    def test_osc8_explicit_ids_distinguish_equal_uris(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(
                b"\x1b]8;id=first;https://example.test\x1b\\A"
                b"\x1b]8;id=second;https://example.test\x1b\\B"
                b"\x1b]8;;\x1b\\"
            )
            snapshot = terminal.snapshot()
            self.assertNotEqual(
                snapshot.cell(0, 0).hyperlink,
                snapshot.cell(1, 0).hyperlink,
            )

    def test_large_osc_command_reaches_dispatcher(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(b"\x1b]133;A\x1b\\\x1b]777;notify\x1b\\")
            self.assertEqual(
                terminal.read_actions(),
                ["OSC 133 41", "OSC 777 6e6f74696679"],
            )

    def test_osc52_clipboard_request_is_forwarded(self):
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(b"\x1b]52;c;SGVsbG8=\x1b\\")
            self.assertEqual(
                terminal.read_actions(),
                ["OSC 52 633b534756736247383d"],
            )

    def test_osc52_accepts_clipboard_larger_than_legacy_parser_limit(self):
        payload = b"A" * 16384
        with Zutty(columns=8, rows=2) as terminal:
            terminal.write(b"\x1b]52;c;" + payload + b"\x1b\\")
            self.assertEqual(
                terminal.read_actions(),
                ["OSC 52 " + (b"c;" + payload).hex()],
            )

    def test_unused_hyperlink_metadata_is_reclaimed(self):
        output = bytearray()
        for number in range(600):
            output.extend(
                f"\x1b]8;id={number};https://example.test/{number}\x1b\\X\r".encode()
            )
        output.extend(b"\x1b]8;;\x1b\\")
        with Zutty(columns=2, rows=1, save_lines=1) as terminal:
            terminal.write(bytes(output))
            self.assertLess(terminal.hyperlink_count(), 256)


if __name__ == "__main__":
    unittest.main()
