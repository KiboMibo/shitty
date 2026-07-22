test "resize clears synchronized output on unchanged cell dimensions" {
    var t: Terminal = try .init(testing.allocator, .{ .cols = 80, .rows = 24 });
    defer t.deinit(testing.allocator);

    var s: Stream = .initAlloc(testing.allocator, .init(&t));
    defer s.deinit();

    t.modes.set(.synchronized_output, true);
    try s.handler.resize(.{
        .cols = 80,
        .rows = 24,
        .cell_size_px = .{ .width = 9, .height = 18 },
    });

    try testing.expect(!t.modes.get(.synchronized_output));
    try testing.expectEqual(@as(u32, 720), t.width_px);
    try testing.expectEqual(@as(u32, 432), t.height_px);
}

test "resize reports mode 2048 geometry" {
    var t: Terminal = try .init(testing.allocator, .{ .cols = 80, .rows = 24 });
    defer t.deinit(testing.allocator);

    const S = struct {
        var response: [128]u8 = undefined;
        var response_len: usize = 0;

        fn writePty(_: *Handler, data: [:0]const u8) void {
            @memcpy(response[0..data.len], data);
            response_len = data.len;
        }
    };
    S.response_len = 0;

    var handler: Handler = .init(&t);
    handler.effects.write_pty = &S.writePty;
    var s: Stream = .initAlloc(testing.allocator, handler);
    defer s.deinit();

    t.modes.set(.in_band_size_reports, true);
    try s.handler.resize(.{
        .cols = 100,
        .rows = 40,
        .cell_size_px = .{ .width = 9, .height = 18 },
    });

    try testing.expectEqualStrings(
        "\x1B[48;40;100;720;900t",
        S.response[0..S.response_len],
    );
}

test "resize suppresses mode 2048 reports" {
    const S = struct {
        var calls: usize = 0;

        fn writePty(_: *Handler, _: [:0]const u8) void {
            calls += 1;
        }
    };
    S.calls = 0;

    var t: Terminal = try .init(testing.allocator, .{ .cols = 80, .rows = 24 });
    defer t.deinit(testing.allocator);
    var handler: Handler = .init(&t);
    handler.effects.write_pty = &S.writePty;
    var s: Stream = .initAlloc(testing.allocator, handler);
    defer s.deinit();

    // Disabled mode suppresses a report even with pixels and a callback.
    try s.handler.resize(.{
        .cols = 80,
        .rows = 24,
        .cell_size_px = .{ .width = 9, .height = 18 },
    });
    try testing.expectEqual(@as(usize, 0), S.calls);

    // Missing pixel geometry suppresses a report even with the mode enabled.
    t.modes.set(.in_band_size_reports, true);
    try s.handler.resize(.{ .cols = 80, .rows = 24 });
    try testing.expectEqual(@as(usize, 0), S.calls);

    // A read-only stream has no write effect and remains successful.
    var readonly_terminal: Terminal = try .init(
        testing.allocator,
        .{ .cols = 80, .rows = 24 },
    );
    defer readonly_terminal.deinit(testing.allocator);
    readonly_terminal.modes.set(.in_band_size_reports, true);
    var readonly_stream: Stream = .initAlloc(
        testing.allocator,
        .init(&readonly_terminal),
    );
    defer readonly_stream.deinit();
    try readonly_stream.handler.resize(.{
        .cols = 80,
        .rows = 24,
        .cell_size_px = .{ .width = 9, .height = 18 },
    });
    try testing.expectEqual(@as(usize, 0), S.calls);
}

test "resize failure preserves terminal state and does not write" {
    var failing = testing.FailingAllocator.init(testing.allocator, .{});
    const alloc = failing.allocator();
    var t: Terminal = try .init(alloc, .{ .cols = 10, .rows = 1 });
    defer t.deinit(alloc);

    const S = struct {
        var called: bool = false;

        fn writePty(_: *Handler, _: [:0]const u8) void {
            called = true;
        }
    };
    S.called = false;

    var handler: Handler = .init(&t);
    handler.effects.write_pty = &S.writePty;
    var s: Stream = .initAlloc(alloc, handler);
    defer s.deinit();

    t.modes.set(.synchronized_output, true);
    t.modes.set(.in_band_size_reports, true);
    failing.fail_index = failing.alloc_index;
    try testing.expectError(error.OutOfMemory, s.handler.resize(.{
        .cols = 513,
        .rows = 1,
        .cell_size_px = .{ .width = 9, .height = 18 },
    }));

    try testing.expect(t.modes.get(.synchronized_output));
    try testing.expect(!S.called);
    try testing.expectEqual(@as(@TypeOf(t.cols), 10), t.cols);
    try testing.expectEqual(@as(u32, 0), t.width_px);
    try testing.expectEqual(@as(u32, 0), t.height_px);
}

test "resize effects do not change canonical terminal state" {
    var authoritative: Terminal = try .init(
        testing.allocator,
        .{ .cols = 10, .rows = 5 },
    );
    defer authoritative.deinit(testing.allocator);
    var readonly: Terminal = try .init(
        testing.allocator,
        .{ .cols = 10, .rows = 5 },
    );
    defer readonly.deinit(testing.allocator);

    const S = struct {
        fn writePty(_: *Handler, _: [:0]const u8) void {}
    };
    var authoritative_handler: Handler = .init(&authoritative);
    authoritative_handler.effects.write_pty = &S.writePty;
    var authoritative_stream: Stream = .initAlloc(
        testing.allocator,
        authoritative_handler,
    );
    defer authoritative_stream.deinit();
    var readonly_stream: Stream = .initAlloc(
        testing.allocator,
        .init(&readonly),
    );
    defer readonly_stream.deinit();

    authoritative.modes.set(.in_band_size_reports, true);
    readonly.modes.set(.in_band_size_reports, true);
    const value: Terminal.Resize = .{
        .cols = 20,
        .rows = 10,
        .cell_size_px = .{ .width = 9, .height = 18 },
    };
    try authoritative_stream.handler.resize(value);
    try readonly_stream.handler.resize(value);

    try testing.expectEqual(authoritative.cols, readonly.cols);
    try testing.expectEqual(authoritative.rows, readonly.rows);
    try testing.expectEqual(authoritative.width_px, readonly.width_px);
    try testing.expectEqual(authoritative.height_px, readonly.height_px);
    try testing.expect(std.meta.eql(authoritative.modes, readonly.modes));
    try testing.expect(std.meta.eql(
        authoritative.scrolling_region,
        readonly.scrolling_region,
    ));
}

test "basic print" {
    var t: Terminal = try .init(testing.allocator, .{ .cols = 10, .rows = 10 });
    defer t.deinit(testing.allocator);

    var s: Stream = .initAlloc(testing.allocator, .init(&t));
    defer s.deinit();

    s.nextSlice("Hello");
    try testing.expectEqual(@as(usize, 5), t.screens.active.cursor.x);
    try testing.expectEqual(@as(usize, 0), t.screens.active.cursor.y);

    const str = try t.plainString(testing.allocator);
    defer testing.allocator.free(str);
    try testing.expectEqualStrings("Hello", str);
}

test "semantic failure is sticky while processing continues" {
    var failing = testing.FailingAllocator.init(testing.allocator, .{});
    const alloc = failing.allocator();
    var t: Terminal = try .init(alloc, .{ .cols = 10, .rows = 2 });
    defer t.deinit(alloc);

    var s: Stream = .initAlloc(alloc, .init(&t));
    defer s.deinit();
    try testing.expect(!s.handler.semantic_failure);

    // Setting the title is a terminal-owned semantic update. Force its
    // allocation to fail at the central vtFallible boundary.
    failing.fail_index = failing.alloc_index;
    s.nextSlice("\x1B]2;unavailable\x1B\\");
    try testing.expect(s.handler.semantic_failure);

    // Later input and RIS remain best-effort and never clear the diagnostic.
    failing.fail_index = std.math.maxInt(usize);
    s.nextSlice("ignored");
    s.nextSlice("\x1Bc");
    s.nextSlice("OK");
    try testing.expect(s.handler.semantic_failure);

    const str = try t.plainString(testing.allocator);
    defer testing.allocator.free(str);
    try testing.expectEqualStrings("OK", str);

    // A new execution root starts without inheriting the diagnostic.
    var fresh = Handler.init(&t);
    defer fresh.deinit();
    try testing.expect(!fresh.semantic_failure);
}

test "cursor movement" {
    var t: Terminal = try .init(testing.allocator, .{ .cols = 10, .rows = 10 });
    defer t.deinit(testing.allocator);

    var s: Stream = .initAlloc(testing.allocator, .init(&t));
    defer s.deinit();

    // Move cursor using escape sequences
    s.nextSlice("Hello\x1B[1;1H");
    try testing.expectEqual(@as(usize, 0), t.screens.active.cursor.x);
    try testing.expectEqual(@as(usize, 0), t.screens.active.cursor.y);

    // Move to position 2,3
    s.nextSlice("\x1B[2;3H");
    try testing.expectEqual(@as(usize, 2), t.screens.active.cursor.x);
    try testing.expectEqual(@as(usize, 1), t.screens.active.cursor.y);
}

test "erase operations" {
    var t: Terminal = try .init(testing.allocator, .{ .cols = 20, .rows = 10 });
    defer t.deinit(testing.allocator);

    var s: Stream = .initAlloc(testing.allocator, .init(&t));
    defer s.deinit();

    // Print some text
    s.nextSlice("Hello World");
    try testing.expectEqual(@as(usize, 11), t.screens.active.cursor.x);
    try testing.expectEqual(@as(usize, 0), t.screens.active.cursor.y);

    // Move cursor to position 1,6 and erase from cursor to end of line
    s.nextSlice("\x1B[1;6H");
    s.nextSlice("\x1B[K");

    const str = try t.plainString(testing.allocator);
    defer testing.allocator.free(str);
    try testing.expectEqualStrings("Hello", str);
}

test "tabs" {
    var t: Terminal = try .init(testing.allocator, .{ .cols = 80, .rows = 10 });
    defer t.deinit(testing.allocator);

    var s: Stream = .initAlloc(testing.allocator, .init(&t));
    defer s.deinit();

    s.nextSlice("A\tB");
    try testing.expectEqual(@as(usize, 9), t.screens.active.cursor.x);

    const str = try t.plainString(testing.allocator);
    defer testing.allocator.free(str);
    try testing.expectEqualStrings("A       B", str);
}

test "modes" {
    var t: Terminal = try .init(testing.allocator, .{ .cols = 80, .rows = 24 });
    defer t.deinit(testing.allocator);

    var s: Stream = .initAlloc(testing.allocator, .init(&t));
    defer s.deinit();

    // Test wraparound mode
    try testing.expect(t.modes.get(.wraparound));
    s.nextSlice("\x1B[?7l"); // Disable wraparound
    try testing.expect(!t.modes.get(.wraparound));
    s.nextSlice("\x1B[?7h"); // Enable wraparound
    try testing.expect(t.modes.get(.wraparound));
}

test "scrolling regions" {
    var t: Terminal = try .init(testing.allocator, .{ .cols = 80, .rows = 24 });
    defer t.deinit(testing.allocator);

    var s: Stream = .initAlloc(testing.allocator, .init(&t));
    defer s.deinit();

    // Set scrolling region from line 5 to 20
    s.nextSlice("\x1B[5;20r");
    try testing.expectEqual(@as(usize, 4), t.scrolling_region.top);
    try testing.expectEqual(@as(usize, 19), t.scrolling_region.bottom);
    try testing.expectEqual(@as(usize, 0), t.scrolling_region.left);
    try testing.expectEqual(@as(usize, 79), t.scrolling_region.right);
}

test "charsets" {
    var t: Terminal = try .init(testing.allocator, .{ .cols = 80, .rows = 24 });
    defer t.deinit(testing.allocator);

    var s: Stream = .initAlloc(testing.allocator, .init(&t));
    defer s.deinit();

    // Configure G0 as DEC special graphics
    s.nextSlice("\x1B(0");
    s.nextSlice("`"); // Should print diamond character

    const str = try t.plainString(testing.allocator);
    defer testing.allocator.free(str);
    try testing.expectEqualStrings("◆", str);
}

test "alt screen" {
    var t: Terminal = try .init(testing.allocator, .{ .cols = 10, .rows = 5 });
    defer t.deinit(testing.allocator);

    var s: Stream = .initAlloc(testing.allocator, .init(&t));
    defer s.deinit();

    // Write to primary screen
    s.nextSlice("Primary");
    try testing.expectEqual(.primary, t.screens.active_key);

    // Switch to alt screen
    s.nextSlice("\x1B[?1049h");
    try testing.expectEqual(.alternate, t.screens.active_key);

    // Write to alt screen
    s.nextSlice("Alt");

    // Switch back to primary
    s.nextSlice("\x1B[?1049l");
    try testing.expectEqual(.primary, t.screens.active_key);

    const str = try t.plainString(testing.allocator);
    defer testing.allocator.free(str);
    try testing.expectEqualStrings("Primary", str);
}

test "cursor save and restore" {
    var t: Terminal = try .init(testing.allocator, .{ .cols = 80, .rows = 24 });
    defer t.deinit(testing.allocator);

    var s: Stream = .initAlloc(testing.allocator, .init(&t));
    defer s.deinit();

    // Move cursor to 10,15
    s.nextSlice("\x1B[10;15H");
    try testing.expectEqual(@as(usize, 14), t.screens.active.cursor.x);
    try testing.expectEqual(@as(usize, 9), t.screens.active.cursor.y);

    // Save cursor
    s.nextSlice("\x1B7");

    // Move cursor elsewhere
    s.nextSlice("\x1B[1;1H");
    try testing.expectEqual(@as(usize, 0), t.screens.active.cursor.x);
    try testing.expectEqual(@as(usize, 0), t.screens.active.cursor.y);

    // Restore cursor
    s.nextSlice("\x1B8");
    try testing.expectEqual(@as(usize, 14), t.screens.active.cursor.x);
    try testing.expectEqual(@as(usize, 9), t.screens.active.cursor.y);
}

test "attributes" {
    var t: Terminal = try .init(testing.allocator, .{ .cols = 80, .rows = 24 });
    defer t.deinit(testing.allocator);

    var s: Stream = .initAlloc(testing.allocator, .init(&t));
    defer s.deinit();

    // Set bold and write text
    s.nextSlice("\x1B[1mBold\x1B[0m");

    // Verify we can write attributes - just check the string was written
    const str = try t.plainString(testing.allocator);
    defer testing.allocator.free(str);
    try testing.expectEqualStrings("Bold", str);
}

test "DECALN screen alignment" {
    var t: Terminal = try .init(testing.allocator, .{ .cols = 10, .rows = 3 });
    defer t.deinit(testing.allocator);

    var s: Stream = .initAlloc(testing.allocator, .init(&t));
    defer s.deinit();

    // Run DECALN
    s.nextSlice("\x1B#8");

    // Verify entire screen is filled with 'E'
    const str = try t.plainString(testing.allocator);
    defer testing.allocator.free(str);
    try testing.expectEqualStrings("EEEEEEEEEE\nEEEEEEEEEE\nEEEEEEEEEE", str);

    // Cursor should be at 1,1
    try testing.expectEqual(@as(usize, 0), t.screens.active.cursor.x);
    try testing.expectEqual(@as(usize, 0), t.screens.active.cursor.y);
}

test "full reset" {
    var t: Terminal = try .init(testing.allocator, .{ .cols = 80, .rows = 24 });
    defer t.deinit(testing.allocator);

    var s: Stream = .initAlloc(testing.allocator, .init(&t));
    defer s.deinit();

    // Make some changes
    s.nextSlice("Hello");
    s.nextSlice("\x1B[10;20H");
    s.nextSlice("\x1B[5;20r"); // Set scroll region
    s.nextSlice("\x1B[?7l"); // Disable wraparound
    s.nextSlice("\x1B_25a1;r;cp=e0a0;AAAAAAAAAAAAAA==\x1B\\");
    try testing.expect(t.glyph_glossary.contains(0xE0A0));

    // Full reset
    s.nextSlice("\x1Bc");

    // Verify reset state
    try testing.expectEqual(@as(usize, 0), t.screens.active.cursor.x);
    try testing.expectEqual(@as(usize, 0), t.screens.active.cursor.y);
    try testing.expectEqual(@as(usize, 0), t.scrolling_region.top);
    try testing.expectEqual(@as(usize, 23), t.scrolling_region.bottom);
    try testing.expect(t.modes.get(.wraparound));
    try testing.expect(!t.glyph_glossary.contains(0xE0A0));
}

test "glyph protocol APC with write_pty callback" {
    var t: Terminal = try .init(testing.allocator, .{ .cols = 80, .rows = 24 });
    defer t.deinit(testing.allocator);

    const S = struct {
        var last_response: ?[:0]const u8 = null;
        fn writePty(_: *Handler, data: [:0]const u8) void {
            if (last_response) |old| testing.allocator.free(old);
            last_response = testing.allocator.dupeZ(u8, data) catch @panic("OOM");
        }
    };
    S.last_response = null;
    defer if (S.last_response) |old| testing.allocator.free(old);

    var handler: Handler = .init(&t);
    handler.effects.write_pty = &S.writePty;

    var s: Stream = .initAlloc(testing.allocator, handler);
    defer s.deinit();

    s.nextSlice("\x1B_25a1;s\x1B\\");
    try testing.expectEqualStrings("\x1B_25a1;s;fmt=glyf\x1B\\", S.last_response.?);

    s.nextSlice("\x1B_25a1;r;cp=e0a0;AAAAAAAAAAAAAA==\x1B\\");
    try testing.expectEqualStrings("\x1B_25a1;r;cp=e0a0;status=0\x1B\\", S.last_response.?);
    try testing.expect(t.glyph_glossary.contains(0xE0A0));
}

test "ignores query actions" {
    var t: Terminal = try .init(testing.allocator, .{ .cols = 80, .rows = 24 });
    defer t.deinit(testing.allocator);

    var s: Stream = .initAlloc(testing.allocator, .init(&t));
    defer s.deinit();

    // These should be ignored without error
    s.nextSlice("\x1B[c"); // Device attributes
    s.nextSlice("\x1B[5n"); // Device status report
    s.nextSlice("\x1B[6n"); // Cursor position report
    s.nextSlice("\x1B]4;0;?\x1B\\"); // OSC color query
    s.nextSlice("\x1B]21;foreground=?\x1B\\"); // Kitty color query
    s.nextSlice("\x1B]52;c;%%%invalid-base64%%%\x1B\\");
    s.nextSlice("\x1B_Ga=p,i=999\x1B\\"); // Missing Kitty image
    s.nextSlice("\x1B_25a1;r;cp=41;%%%invalid%%%\x1B\\"); // Rejected glyph

    // Query, malformed input, protocol failure responses, and external-effect
    // failures do not imply that terminal-owned semantic state diverged.
    try testing.expect(!s.handler.semantic_failure);

    // Terminal should still be functional
    s.nextSlice("Test");
    const str = try t.plainString(testing.allocator);
    defer testing.allocator.free(str);
    try testing.expectEqualStrings("Test", str);
}

test "OSC 4 set and reset palette" {
    var t: Terminal = try .init(testing.allocator, .{ .cols = 10, .rows = 10 });
    defer t.deinit(testing.allocator);

    var s: Stream = .initAlloc(testing.allocator, .init(&t));
    defer s.deinit();

    // Save default color
    const default_color_0 = t.colors.palette.original[0];

    // Set color 0 to red
    s.nextSlice("\x1b]4;0;rgb:ff/00/00\x1b\\");
    try testing.expectEqual(@as(u8, 0xff), t.colors.palette.current[0].r);
    try testing.expectEqual(@as(u8, 0x00), t.colors.palette.current[0].g);
    try testing.expectEqual(@as(u8, 0x00), t.colors.palette.current[0].b);
    try testing.expect(t.colors.palette.mask.isSet(0));

    // Reset color 0
    s.nextSlice("\x1b]104;0\x1b\\");
    try testing.expectEqual(default_color_0, t.colors.palette.current[0]);
    try testing.expect(!t.colors.palette.mask.isSet(0));
}

test "OSC 104 reset all palette colors" {
    var t: Terminal = try .init(testing.allocator, .{ .cols = 10, .rows = 10 });
    defer t.deinit(testing.allocator);

    var s: Stream = .initAlloc(testing.allocator, .init(&t));
    defer s.deinit();

    // Set multiple colors
    s.nextSlice("\x1b]4;0;rgb:ff/00/00\x1b\\");
    s.nextSlice("\x1b]4;1;rgb:00/ff/00\x1b\\");
    s.nextSlice("\x1b]4;2;rgb:00/00/ff\x1b\\");
    try testing.expect(t.colors.palette.mask.isSet(0));
    try testing.expect(t.colors.palette.mask.isSet(1));
    try testing.expect(t.colors.palette.mask.isSet(2));

    // Reset all palette colors
    s.nextSlice("\x1b]104\x1b\\");
    try testing.expectEqual(t.colors.palette.original[0], t.colors.palette.current[0]);
    try testing.expectEqual(t.colors.palette.original[1], t.colors.palette.current[1]);
    try testing.expectEqual(t.colors.palette.original[2], t.colors.palette.current[2]);
    try testing.expect(!t.colors.palette.mask.isSet(0));
    try testing.expect(!t.colors.palette.mask.isSet(1));
    try testing.expect(!t.colors.palette.mask.isSet(2));
}

test "OSC 10 set and reset foreground color" {
    var t: Terminal = try .init(testing.allocator, .{ .cols = 10, .rows = 10 });
    defer t.deinit(testing.allocator);

    var s: Stream = .initAlloc(testing.allocator, .init(&t));
    defer s.deinit();

    // Initially unset
    try testing.expect(t.colors.foreground.get() == null);

    // Set foreground to red
    s.nextSlice("\x1b]10;rgb:ff/00/00\x1b\\");
    const fg = t.colors.foreground.get().?;
    try testing.expectEqual(@as(u8, 0xff), fg.r);
    try testing.expectEqual(@as(u8, 0x00), fg.g);
    try testing.expectEqual(@as(u8, 0x00), fg.b);

    // Reset foreground
    s.nextSlice("\x1b]110\x1b\\");
    try testing.expect(t.colors.foreground.get() == null);
}

test "OSC 11 set and reset background color" {
    var t: Terminal = try .init(testing.allocator, .{ .cols = 10, .rows = 10 });
    defer t.deinit(testing.allocator);

    var s: Stream = .initAlloc(testing.allocator, .init(&t));
    defer s.deinit();

    // Set background to green
    s.nextSlice("\x1b]11;rgb:00/ff/00\x1b\\");
    const bg = t.colors.background.get().?;
    try testing.expectEqual(@as(u8, 0x00), bg.r);
    try testing.expectEqual(@as(u8, 0xff), bg.g);
    try testing.expectEqual(@as(u8, 0x00), bg.b);

    // Reset background
    s.nextSlice("\x1b]111\x1b\\");
    try testing.expect(t.colors.background.get() == null);
}

test "OSC 12 set and reset cursor color" {
    var t: Terminal = try .init(testing.allocator, .{ .cols = 10, .rows = 10 });
    defer t.deinit(testing.allocator);

    var s: Stream = .initAlloc(testing.allocator, .init(&t));
    defer s.deinit();

    // Set cursor to blue
    s.nextSlice("\x1b]12;rgb:00/00/ff\x1b\\");
    const cursor = t.colors.cursor.get().?;
    try testing.expectEqual(@as(u8, 0x00), cursor.r);
    try testing.expectEqual(@as(u8, 0x00), cursor.g);
    try testing.expectEqual(@as(u8, 0xff), cursor.b);

    // Reset cursor
    s.nextSlice("\x1b]112\x1b\\");
    // After reset, cursor might be null (using default)
}

test "OSC color query responses" {
    var t: Terminal = try .init(testing.allocator, .{ .cols = 10, .rows = 10 });
    defer t.deinit(testing.allocator);

    const S = struct {
        var last_response: ?[:0]const u8 = null;

        fn reset() void {
            if (last_response) |old| testing.allocator.free(old);
            last_response = null;
        }

        fn writePty(_: *Handler, data: [:0]const u8) void {
            reset();
            last_response = testing.allocator.dupeZ(u8, data) catch @panic("OOM");
        }
    };
    S.last_response = null;
    defer S.reset();

    var handler: Handler = .init(&t);
    handler.effects.write_pty = &S.writePty;

    var s: Stream = .initAlloc(testing.allocator, handler);
    defer s.deinit();

    s.nextSlice("\x1b]10;?\x1b\\");
    try testing.expect(S.last_response == null);

    s.nextSlice("\x1b]11;?\x1b\\");
    try testing.expect(S.last_response == null);

    s.nextSlice("\x1b]4;2;rgb:12/34/56;2;?\x1b\\");
    try testing.expectEqualStrings(
        "\x1b]4;2;rgb:1212/3434/5656\x1b\\",
        S.last_response.?,
    );

    s.nextSlice("\x1b]10;rgb:01/02/03\x1b\\");
    s.nextSlice("\x1b]11;rgb:04/05/06\x1b\\");
    s.nextSlice("\x1b]12;rgb:07/08/09\x1b\\");
    s.nextSlice("\x1b]10;?;?;?\x1b\\");
    try testing.expectEqualStrings(
        "\x1b]10;rgb:0101/0202/0303\x1b\\" ++
            "\x1b]11;rgb:0404/0505/0606\x1b\\" ++
            "\x1b]12;rgb:0707/0808/0909\x1b\\",
        S.last_response.?,
    );

    s.nextSlice("\x1b]112\x1b\\");
    s.nextSlice("\x1b]12;?\x07");
    try testing.expectEqualStrings(
        "\x1b]12;rgb:0101/0202/0303\x07",
        S.last_response.?,
    );
}

test "kitty color protocol set palette" {
    var t: Terminal = try .init(testing.allocator, .{ .cols = 10, .rows = 10 });
    defer t.deinit(testing.allocator);

    var s: Stream = .initAlloc(testing.allocator, .init(&t));
    defer s.deinit();

    // Set palette color 5 to magenta using kitty protocol
    s.nextSlice("\x1b]21;5=rgb:ff/00/ff\x1b\\");
    try testing.expectEqual(@as(u8, 0xff), t.colors.palette.current[5].r);
    try testing.expectEqual(@as(u8, 0x00), t.colors.palette.current[5].g);
    try testing.expectEqual(@as(u8, 0xff), t.colors.palette.current[5].b);
    try testing.expect(t.colors.palette.mask.isSet(5));
    try testing.expect(t.flags.dirty.palette);
}

test "kitty color protocol reset palette" {
    var t: Terminal = try .init(testing.allocator, .{ .cols = 10, .rows = 10 });
    defer t.deinit(testing.allocator);

    var s: Stream = .initAlloc(testing.allocator, .init(&t));
    defer s.deinit();

    // Set and then reset palette color
    const original = t.colors.palette.original[7];
    s.nextSlice("\x1b]21;7=rgb:aa/bb/cc\x1b\\");
    try testing.expect(t.colors.palette.mask.isSet(7));

    s.nextSlice("\x1b]21;7=\x1b\\");
    try testing.expectEqual(original, t.colors.palette.current[7]);
    try testing.expect(!t.colors.palette.mask.isSet(7));
}

test "kitty color protocol set foreground" {
    var t: Terminal = try .init(testing.allocator, .{ .cols = 10, .rows = 10 });
    defer t.deinit(testing.allocator);

    var s: Stream = .initAlloc(testing.allocator, .init(&t));
    defer s.deinit();

    // Set foreground using kitty protocol
    s.nextSlice("\x1b]21;foreground=rgb:12/34/56\x1b\\");
    const fg = t.colors.foreground.get().?;
    try testing.expectEqual(@as(u8, 0x12), fg.r);
    try testing.expectEqual(@as(u8, 0x34), fg.g);
    try testing.expectEqual(@as(u8, 0x56), fg.b);
}

test "kitty color protocol set background" {
    var t: Terminal = try .init(testing.allocator, .{ .cols = 10, .rows = 10 });
    defer t.deinit(testing.allocator);

    var s: Stream = .initAlloc(testing.allocator, .init(&t));
    defer s.deinit();

    // Set background using kitty protocol
    s.nextSlice("\x1b]21;background=rgb:78/9a/bc\x1b\\");
    const bg = t.colors.background.get().?;
    try testing.expectEqual(@as(u8, 0x78), bg.r);
    try testing.expectEqual(@as(u8, 0x9a), bg.g);
    try testing.expectEqual(@as(u8, 0xbc), bg.b);
}

test "kitty color protocol set cursor" {
    var t: Terminal = try .init(testing.allocator, .{ .cols = 10, .rows = 10 });
    defer t.deinit(testing.allocator);

    var s: Stream = .initAlloc(testing.allocator, .init(&t));
    defer s.deinit();

    // Set cursor using kitty protocol
    s.nextSlice("\x1b]21;cursor=rgb:de/f0/12\x1b\\");
    const cursor = t.colors.cursor.get().?;
    try testing.expectEqual(@as(u8, 0xde), cursor.r);
    try testing.expectEqual(@as(u8, 0xf0), cursor.g);
    try testing.expectEqual(@as(u8, 0x12), cursor.b);
}

test "kitty color protocol reset foreground" {
    var t: Terminal = try .init(testing.allocator, .{ .cols = 10, .rows = 10 });
    defer t.deinit(testing.allocator);

    var s: Stream = .initAlloc(testing.allocator, .init(&t));
    defer s.deinit();

    // Set and reset foreground
    s.nextSlice("\x1b]21;foreground=rgb:11/22/33\x1b\\");
    try testing.expect(t.colors.foreground.get() != null);

    s.nextSlice("\x1b]21;foreground=\x1b\\");
    // After reset, should be unset
    try testing.expect(t.colors.foreground.get() == null);
}

test "kitty color protocol query responses" {
    var t: Terminal = try .init(testing.allocator, .{ .cols = 10, .rows = 10 });
    defer t.deinit(testing.allocator);

    const S = struct {
        var last_response: ?[:0]const u8 = null;

        fn reset() void {
            if (last_response) |old| testing.allocator.free(old);
            last_response = null;
        }

        fn writePty(_: *Handler, data: [:0]const u8) void {
            reset();
            last_response = testing.allocator.dupeZ(u8, data) catch @panic("OOM");
        }
    };
    S.last_response = null;
    defer S.reset();

    var handler: Handler = .init(&t);
    handler.effects.write_pty = &S.writePty;

    var s: Stream = .initAlloc(testing.allocator, handler);
    defer s.deinit();

    s.nextSlice("\x1b]21;background=?\x1b\\");
    try testing.expectEqualStrings(
        "\x1b]21;background=\x1b\\",
        S.last_response.?,
    );

    s.nextSlice("\x1b]21;foreground=rgb:12/34/56;2=rgb:aa/bb/cc\x1b\\");
    s.nextSlice("\x1b]21;foreground=?;background=?;2=?\x1b\\");
    try testing.expectEqualStrings(
        "\x1b]21;foreground=rgb:12/34/56;background=;2=rgb:aa/bb/cc\x1b\\",
        S.last_response.?,
    );
}

test "palette dirty flag set on color change" {
    var t: Terminal = try .init(testing.allocator, .{ .cols = 10, .rows = 10 });
    defer t.deinit(testing.allocator);

    var s: Stream = .initAlloc(testing.allocator, .init(&t));
    defer s.deinit();

    // Clear dirty flag
    t.flags.dirty.palette = false;

    // Setting palette color should set dirty flag
    s.nextSlice("\x1b]4;0;rgb:ff/00/00\x1b\\");
    try testing.expect(t.flags.dirty.palette);

    // Clear and test reset
    t.flags.dirty.palette = false;
    s.nextSlice("\x1b]104;0\x1b\\");
    try testing.expect(t.flags.dirty.palette);

    // Clear and test kitty protocol
    t.flags.dirty.palette = false;
    s.nextSlice("\x1b]21;1=rgb:00/ff/00\x1b\\");
    try testing.expect(t.flags.dirty.palette);
}

test "semantic prompt fresh line" {
    var t: Terminal = try .init(testing.allocator, .{ .cols = 10, .rows = 10 });
    defer t.deinit(testing.allocator);

    var s: Stream = .initAlloc(testing.allocator, .init(&t));
    defer s.deinit();

    s.nextSlice("Hello");
    s.nextSlice("\x1b]133;L\x07");
    try testing.expectEqual(@as(usize, 0), t.screens.active.cursor.x);
    try testing.expectEqual(@as(usize, 1), t.screens.active.cursor.y);
}

test "semantic prompt fresh line new prompt" {
    var t: Terminal = try .init(testing.allocator, .{ .cols = 10, .rows = 10 });
    defer t.deinit(testing.allocator);

    var s: Stream = .initAlloc(testing.allocator, .init(&t));
    defer s.deinit();

    // Write some text and then send OSC 133;A (fresh_line_new_prompt)
    s.nextSlice("Hello");
    s.nextSlice("\x1b]133;A\x07");

    // Should do a fresh line (carriage return + index)
    try testing.expectEqual(@as(usize, 0), t.screens.active.cursor.x);
    try testing.expectEqual(@as(usize, 1), t.screens.active.cursor.y);

    // Should set cursor semantic_content to prompt
    try testing.expectEqual(.prompt, t.screens.active.cursor.semantic_content);

    // Test with redraw option
    s.nextSlice("prompt$ ");
    s.nextSlice("\x1b]133;A;redraw=1\x07");
    try testing.expect(t.flags.shell_redraws_prompt == .true);
}

test "semantic prompt end of input, then start output" {
    var t: Terminal = try .init(testing.allocator, .{ .cols = 10, .rows = 10 });
    defer t.deinit(testing.allocator);

    var s: Stream = .initAlloc(testing.allocator, .init(&t));
    defer s.deinit();

    // Write some text and then send OSC 133;A (fresh_line_new_prompt)
    s.nextSlice("Hello");
    s.nextSlice("\x1b]133;A\x07");
    s.nextSlice("prompt$ ");
    s.nextSlice("\x1b]133;B\x07");
    try testing.expectEqual(.input, t.screens.active.cursor.semantic_content);
    s.nextSlice("\x1b]133;C\x07");
    try testing.expectEqual(.output, t.screens.active.cursor.semantic_content);
}

test "semantic prompt prompt_start" {
    var t: Terminal = try .init(testing.allocator, .{ .cols = 10, .rows = 10 });
    defer t.deinit(testing.allocator);

    var s: Stream = .initAlloc(testing.allocator, .init(&t));
    defer s.deinit();

    // Write some text
    s.nextSlice("Hello");

    // OSC 133;P marks the start of a prompt (without fresh line behavior)
    s.nextSlice("\x1b]133;P\x07");
    try testing.expectEqual(.prompt, t.screens.active.cursor.semantic_content);
    try testing.expectEqual(@as(usize, 5), t.screens.active.cursor.x);
    try testing.expectEqual(@as(usize, 0), t.screens.active.cursor.y);
}

test "semantic prompt new_command" {
    var t: Terminal = try .init(testing.allocator, .{ .cols = 10, .rows = 10 });
    defer t.deinit(testing.allocator);

    var s: Stream = .initAlloc(testing.allocator, .init(&t));
    defer s.deinit();

    // Write some text
    s.nextSlice("Hello");
    s.nextSlice("\x1b]133;N\x07");

    // Should behave like fresh_line_new_prompt - cursor moves to column 0
    // on next line since we had content
    try testing.expectEqual(@as(usize, 0), t.screens.active.cursor.x);
    try testing.expectEqual(@as(usize, 1), t.screens.active.cursor.y);
    try testing.expectEqual(.prompt, t.screens.active.cursor.semantic_content);
}

test "semantic prompt new_command at column zero" {
    var t: Terminal = try .init(testing.allocator, .{ .cols = 10, .rows = 10 });
    defer t.deinit(testing.allocator);

    var s: Stream = .initAlloc(testing.allocator, .init(&t));
    defer s.deinit();

    // OSC 133;N when already at column 0 should stay on same line
    s.nextSlice("\x1b]133;N\x07");
    try testing.expectEqual(@as(usize, 0), t.screens.active.cursor.x);
    try testing.expectEqual(@as(usize, 0), t.screens.active.cursor.y);
    try testing.expectEqual(.prompt, t.screens.active.cursor.semantic_content);
}

test "semantic prompt end_prompt_start_input_terminate_eol clears on linefeed" {
    var t: Terminal = try .init(testing.allocator, .{ .cols = 10, .rows = 10 });
    defer t.deinit(testing.allocator);

    var s: Stream = .initAlloc(testing.allocator, .init(&t));
    defer s.deinit();

    // Set input terminated by EOL
    s.nextSlice("\x1b]133;I\x07");
    try testing.expectEqual(.input, t.screens.active.cursor.semantic_content);

    // Linefeed should reset semantic content to output
    s.nextSlice("\n");
    try testing.expectEqual(.output, t.screens.active.cursor.semantic_content);
}

test "bell effect callback" {
    var t: Terminal = try .init(testing.allocator, .{ .cols = 80, .rows = 24 });
    defer t.deinit(testing.allocator);

    // Test bell with null callback (default readonly effects) doesn't crash
    {
        var s: Stream = .initAlloc(testing.allocator, .init(&t));
        defer s.deinit();

        s.nextSlice("\x07");

        // Terminal should still be functional after bell
        s.nextSlice("AfterBell");
        const str = try t.plainString(testing.allocator);
        defer testing.allocator.free(str);
        try testing.expectEqualStrings("AfterBell", str);
    }

    t.fullReset();

    // Test bell with a callback
    {
        const S = struct {
            var bell_count: usize = 0;
            fn bell(_: *Handler) void {
                bell_count += 1;
            }
        };
        S.bell_count = 0;

        var handler: Handler = .init(&t);
        handler.effects.bell = &S.bell;

        var s: Stream = .initAlloc(testing.allocator, handler);
        defer s.deinit();

        s.nextSlice("\x07");
        try testing.expectEqual(@as(usize, 1), S.bell_count);

        s.nextSlice("\x07\x07");
        try testing.expectEqual(@as(usize, 3), S.bell_count);
    }
}

test "clipboard_write effect callback" {
    var t: Terminal = try .init(testing.allocator, .{ .cols = 80, .rows = 24 });
    defer t.deinit(testing.allocator);

    // A null callback (the default readonly effects) silently ignores writes.
    {
        var s: Stream = .initAlloc(testing.allocator, .init(&t));
        defer s.deinit();

        s.nextSlice("\x1B]52;c;aGVsbG8=\x1B\\");

        // Terminal should still be functional after the ignored sequence
        s.nextSlice("AfterClipboard");
        const str = try t.plainString(testing.allocator);
        defer testing.allocator.free(str);
        try testing.expectEqualStrings("AfterClipboard", str);
    }

    t.fullReset();

    const S = struct {
        var count: usize = 0;
        var result: clipboard.WriteResult = .success;
        var last_location: clipboard.Location = .standard;
        var last_contents_len: usize = 0;
        var last_mime: ?[]u8 = null;
        var last_data: ?[]u8 = null;

        fn clearCapture() void {
            if (last_mime) |value| testing.allocator.free(value);
            if (last_data) |value| testing.allocator.free(value);
            last_mime = null;
            last_data = null;
            last_contents_len = 0;
        }

        fn clipboardWrite(_: *Handler, write: clipboard.Write) clipboard.WriteResult {
            clearCapture();
            count += 1;
            last_location = write.location;
            last_contents_len = write.contents.len;
            if (write.contents.len > 0) {
                last_mime = testing.allocator.dupe(u8, write.contents[0].mime) catch
                    @panic("failed to capture clipboard MIME type");
                last_data = testing.allocator.dupe(u8, write.contents[0].data) catch
                    @panic("failed to capture clipboard data");
            }
            return result;
        }
    };
    S.count = 0;
    S.result = .denied;
    S.clearCapture();
    defer S.clearCapture();

    var handler: Handler = .init(&t);
    handler.effects.clipboard_write = &S.clipboardWrite;

    var s: Stream = .initAlloc(testing.allocator, handler);
    defer s.deinit();

    // Selectors are normalized and payloads are decoded before the callback.
    const cases = [_]struct {
        sequence: []const u8,
        location: clipboard.Location,
        data: []const u8,
    }{
        .{ .sequence = "\x1B]52;c;aGVsbG8=\x1B\\", .location = .standard, .data = "hello" },
        .{ .sequence = "\x1B]52;s;d29ybGQ=\x07", .location = .selection, .data = "world" },
        .{ .sequence = "\x1B]52;p;cHJpbWFyeQ==\x1B\\", .location = .primary, .data = "primary" },
        .{ .sequence = "\x1B]52;0;Y3V0\x1B\\", .location = .standard, .data = "cut" },
        .{ .sequence = "\x1B]52;x;ZmFsbGJhY2s=\x1B\\", .location = .standard, .data = "fallback" },
        .{ .sequence = "\x1B]52;c;YQBi\x1B\\", .location = .standard, .data = "a\x00b" },
    };

    for (cases, 1..) |case, expected_count| {
        s.nextSlice(case.sequence);
        try testing.expectEqual(expected_count, S.count);
        try testing.expectEqual(case.location, S.last_location);
        try testing.expectEqual(@as(usize, 1), S.last_contents_len);
        try testing.expectEqualStrings("text/plain", S.last_mime.?);
        try testing.expectEqualSlices(u8, case.data, S.last_data.?);
    }

    // Empty data is a clear, represented by an empty contents slice.
    s.nextSlice("\x1B]52;s;\x1B\\");
    try testing.expectEqual(@as(usize, cases.len + 1), S.count);
    try testing.expectEqual(clipboard.Location.selection, S.last_location);
    try testing.expectEqual(@as(usize, 0), S.last_contents_len);
    try testing.expect(S.last_mime == null);
    try testing.expect(S.last_data == null);

    // Reads and malformed base64 are ignored.
    s.nextSlice("\x1B]52;c;?\x1B\\");
    s.nextSlice("\x1B]52;c;***\x1B\\");
    try testing.expectEqual(@as(usize, cases.len + 1), S.count);

    // OSC 1337 Copy shares the normalized clipboard write path.
    s.nextSlice("\x1B]1337;Copy=:aVRlcm0y\x1B\\");
    try testing.expectEqual(@as(usize, cases.len + 2), S.count);
    try testing.expectEqual(clipboard.Location.standard, S.last_location);
    try testing.expectEqualStrings("text/plain", S.last_mime.?);
    try testing.expectEqualStrings("iTerm2", S.last_data.?);

    // Parsing across write boundaries still invokes exactly one atomic write.
    s.nextSlice("\x1B]52;p;ZnJh");
    s.nextSlice("Z21lbnRlZA==\x1B");
    s.nextSlice("\\");
    try testing.expectEqual(@as(usize, cases.len + 3), S.count);
    try testing.expectEqual(clipboard.Location.primary, S.last_location);
    try testing.expectEqualStrings("text/plain", S.last_mime.?);
    try testing.expectEqualStrings("fragmented", S.last_data.?);

    // Callback results are intentionally ignored for protocols without a
    // write acknowledgement. The denied result above did not stop later writes.
    try testing.expectEqual(clipboard.WriteResult.denied, S.result);
}

test "clipboard_write allocation failure is ignored" {
    var t: Terminal = try .init(testing.allocator, .{ .cols = 80, .rows = 24 });
    defer t.deinit(testing.allocator);

    const S = struct {
        var count: usize = 0;

        fn clipboardWrite(_: *Handler, _: clipboard.Write) clipboard.WriteResult {
            count += 1;
            return .success;
        }
    };
    S.count = 0;

    var handler: Handler = .init(&t);
    handler.effects.clipboard_write = &S.clipboardWrite;

    var s: Stream = .initAlloc(testing.allocator, handler);
    defer s.deinit();

    // Only the decoded scratch data uses the terminal allocator here. Swap in
    // an allocator that always fails, then restore it before terminal teardown.
    {
        const alloc = t.screens.active.alloc;
        t.screens.active.alloc = testing.failing_allocator;
        defer t.screens.active.alloc = alloc;
        s.nextSlice("\x1B]52;c;aGVsbG8=\x1B\\");
    }
    try testing.expectEqual(@as(usize, 0), S.count);
    try testing.expect(!s.handler.semantic_failure);
}

test "request mode DECRQM with write_pty callback" {
    var t: Terminal = try .init(testing.allocator, .{ .cols = 80, .rows = 24 });
    defer t.deinit(testing.allocator);

    // Without callback, DECRQM should not crash
    {
        var s: Stream = .initAlloc(testing.allocator, .init(&t));
        defer s.deinit();

        // DECRQM for mode 7 (wraparound) — should be silently ignored
        s.nextSlice("\x1B[?7$p");
    }

    t.fullReset();

    // With callback, DECRQM should produce a response
    {
        const S = struct {
            var last_response: ?[:0]const u8 = null;
            fn writePty(_: *Handler, data: [:0]const u8) void {
                if (last_response) |old| testing.allocator.free(old);
                last_response = testing.allocator.dupeZ(u8, data) catch @panic("OOM");
            }
        };
        S.last_response = null;
        defer if (S.last_response) |old| testing.allocator.free(old);

        var handler: Handler = .init(&t);
        handler.effects.write_pty = &S.writePty;

        var s: Stream = .initAlloc(testing.allocator, handler);
        defer s.deinit();

        // Wraparound mode (7) is set by default
        s.nextSlice("\x1B[?7$p");
        try testing.expectEqualStrings("\x1B[?7;1$y", S.last_response.?);

        // Disable wraparound and query again
        s.nextSlice("\x1B[?7l");
        s.nextSlice("\x1B[?7$p");
        try testing.expectEqualStrings("\x1B[?7;2$y", S.last_response.?);

        // Query an unknown mode
        s.nextSlice("\x1B[?9999$p");
        try testing.expectEqualStrings("\x1B[?9999;0$y", S.last_response.?);
    }
}

test "stream: CSI W with intermediate but no params" {
    // Regression test from AFL++ crash. CSI ? W without
    // parameters caused an out-of-bounds access on input.params[0].
    var t: Terminal = try .init(testing.allocator, .{
        .cols = 80,
        .rows = 24,
        .max_scrollback = 100,
    });
    defer t.deinit(testing.allocator);

    var s: Stream = .initAlloc(testing.allocator, .init(&t));
    defer s.deinit();

    s.nextSlice("\x1b[?W");
}

test "window_title effect is called" {
    var t: Terminal = try .init(testing.allocator, .{ .cols = 80, .rows = 24 });
    defer t.deinit(testing.allocator);

    const S = struct {
        var title_changed_count: usize = 0;
        fn titleChanged(_: *Handler) void {
            title_changed_count += 1;
        }
    };
    S.title_changed_count = 0;

    var handler: Handler = .init(&t);
    handler.effects.title_changed = &S.titleChanged;

    var s: Stream = .initAlloc(testing.allocator, handler);
    defer s.deinit();

    // Set window title via OSC 2
    s.nextSlice("\x1b]2;Hello World\x1b\\");
    try testing.expectEqualStrings("Hello World", t.getTitle().?);
    try testing.expectEqual(@as(usize, 1), S.title_changed_count);
}

test "window_title effect not called without callback" {
    var t: Terminal = try .init(testing.allocator, .{ .cols = 80, .rows = 24 });
    defer t.deinit(testing.allocator);

    var s: Stream = .initAlloc(testing.allocator, .init(&t));
    defer s.deinit();

    // Should not crash when no callback is set
    s.nextSlice("\x1b]2;Hello World\x1b\\");

    // Title should still be set on terminal state
    try testing.expectEqualStrings("Hello World", t.getTitle().?);

    // Terminal should still be functional
    s.nextSlice("Test");
    const str = try t.plainString(testing.allocator);
    defer testing.allocator.free(str);
    try testing.expectEqualStrings("Test", str);
}

test "window_title effect with empty title" {
    var t: Terminal = try .init(testing.allocator, .{ .cols = 80, .rows = 24 });
    defer t.deinit(testing.allocator);

    const S = struct {
        var title_changed_count: usize = 0;
        fn titleChanged(_: *Handler) void {
            title_changed_count += 1;
        }
    };
    S.title_changed_count = 0;

    var handler: Handler = .init(&t);
    handler.effects.title_changed = &S.titleChanged;

    var s: Stream = .initAlloc(testing.allocator, handler);
    defer s.deinit();

    // Set empty window title
    s.nextSlice("\x1b]2;\x1b\\");
    try testing.expect(t.getTitle() == null);
    try testing.expectEqual(@as(usize, 1), S.title_changed_count);
}

test "kitty_keyboard_query" {
    var t: Terminal = try .init(testing.allocator, .{ .cols = 80, .rows = 24 });
    defer t.deinit(testing.allocator);

    const S = struct {
        var written: ?[:0]const u8 = null;
        fn writePty(_: *Handler, data: [:0]const u8) void {
            written = data;
        }
    };
    S.written = null;

    var handler: Handler = .init(&t);
    handler.effects.write_pty = &S.writePty;

    var s: Stream = .initAlloc(testing.allocator, handler);
    defer s.deinit();

    // Default kitty keyboard flags should be 0
    s.nextSlice("\x1b[?u");
    try testing.expectEqualStrings("\x1b[?0u", S.written.?);

    // Push kitty keyboard mode with flags and query again
    S.written = null;
    s.nextSlice("\x1b[>1u"); // push with disambiguate flag
    s.nextSlice("\x1b[?u");
    try testing.expectEqualStrings("\x1b[?1u", S.written.?);
}

test "xtversion default" {
    var t: Terminal = try .init(testing.allocator, .{ .cols = 80, .rows = 24 });
    defer t.deinit(testing.allocator);

    const S = struct {
        var written: ?[:0]const u8 = null;
        fn writePty(_: *Handler, data: [:0]const u8) void {
            written = data;
        }
    };
    S.written = null;

    var handler: Handler = .init(&t);
    handler.effects.write_pty = &S.writePty;

    var s: Stream = .initAlloc(testing.allocator, handler);
    defer s.deinit();

    // Without xtversion effect set, should report "libghostty"
    s.nextSlice("\x1b[>0q");
    try testing.expectEqualStrings("\x1bP>|libghostty\x1b\\", S.written.?);
}

test "xtversion with effect" {
    var t: Terminal = try .init(testing.allocator, .{ .cols = 80, .rows = 24 });
    defer t.deinit(testing.allocator);

    const S = struct {
        var written: ?[:0]const u8 = null;
        fn writePty(_: *Handler, data: [:0]const u8) void {
            written = data;
        }
        fn xtversion(_: *Handler) []const u8 {
            return "ghostty 1.2.3";
        }
    };
    S.written = null;

    var handler: Handler = .init(&t);
    handler.effects.write_pty = &S.writePty;
    handler.effects.xtversion = &S.xtversion;

    var s: Stream = .initAlloc(testing.allocator, handler);
    defer s.deinit();

    s.nextSlice("\x1b[>0q");
    try testing.expectEqualStrings("\x1bP>|ghostty 1.2.3\x1b\\", S.written.?);
}

test "xtversion with empty string effect" {
    var t: Terminal = try .init(testing.allocator, .{ .cols = 80, .rows = 24 });
    defer t.deinit(testing.allocator);

    const S = struct {
        var written: ?[:0]const u8 = null;
        fn writePty(_: *Handler, data: [:0]const u8) void {
            written = data;
        }
        fn xtversion(_: *Handler) []const u8 {
            return "";
        }
    };
    S.written = null;

    var handler: Handler = .init(&t);
    handler.effects.write_pty = &S.writePty;
    handler.effects.xtversion = &S.xtversion;

    var s: Stream = .initAlloc(testing.allocator, handler);
    defer s.deinit();

    // Empty string from effect should fall back to "libghostty"
    s.nextSlice("\x1b[>0q");
    try testing.expectEqualStrings("\x1bP>|libghostty\x1b\\", S.written.?);
}

test "size report csi_14_t with effect" {
    var t: Terminal = try .init(testing.allocator, .{ .cols = 80, .rows = 24 });
    defer t.deinit(testing.allocator);

    const S = struct {
        var written: ?[]const u8 = null;
        fn writePty(_: *Handler, data: [:0]const u8) void {
            written = testing.allocator.dupe(u8, data) catch @panic("OOM");
        }
        fn getSize(_: *Handler) ?size_report.Size {
            return .{ .rows = 24, .columns = 80, .cell_width = 9, .cell_height = 18 };
        }
    };
    S.written = null;

    var handler: Handler = .init(&t);
    handler.effects.write_pty = &S.writePty;
    handler.effects.size = &S.getSize;

    var s: Stream = .initAlloc(testing.allocator, handler);
    defer s.deinit();

    // CSI 14 t - report text area size in pixels
    s.nextSlice("\x1b[14t");
    defer testing.allocator.free(S.written.?);
    try testing.expectEqualStrings("\x1b[4;432;720t", S.written.?);
}

test "size report csi_16_t with effect" {
    var t: Terminal = try .init(testing.allocator, .{ .cols = 80, .rows = 24 });
    defer t.deinit(testing.allocator);

    const S = struct {
        var written: ?[]const u8 = null;
        fn writePty(_: *Handler, data: [:0]const u8) void {
            written = testing.allocator.dupe(u8, data) catch @panic("OOM");
        }
        fn getSize(_: *Handler) ?size_report.Size {
            return .{ .rows = 24, .columns = 80, .cell_width = 9, .cell_height = 18 };
        }
    };
    S.written = null;

    var handler: Handler = .init(&t);
    handler.effects.write_pty = &S.writePty;
    handler.effects.size = &S.getSize;

    var s: Stream = .initAlloc(testing.allocator, handler);
    defer s.deinit();

    // CSI 16 t - report cell size in pixels
    s.nextSlice("\x1b[16t");
    defer testing.allocator.free(S.written.?);
    try testing.expectEqualStrings("\x1b[6;18;9t", S.written.?);
}

test "size report csi_18_t with effect" {
    var t: Terminal = try .init(testing.allocator, .{ .cols = 80, .rows = 24 });
    defer t.deinit(testing.allocator);

    const S = struct {
        var written: ?[]const u8 = null;
        fn writePty(_: *Handler, data: [:0]const u8) void {
            written = testing.allocator.dupe(u8, data) catch @panic("OOM");
        }
        fn getSize(_: *Handler) ?size_report.Size {
            return .{ .rows = 24, .columns = 80, .cell_width = 9, .cell_height = 18 };
        }
    };
    S.written = null;

    var handler: Handler = .init(&t);
    handler.effects.write_pty = &S.writePty;
    handler.effects.size = &S.getSize;

    var s: Stream = .initAlloc(testing.allocator, handler);
    defer s.deinit();

    // CSI 18 t - report text area size in characters
    s.nextSlice("\x1b[18t");
    defer testing.allocator.free(S.written.?);
    try testing.expectEqualStrings("\x1b[8;24;80t", S.written.?);
}

test "size report no effect callback" {
    var t: Terminal = try .init(testing.allocator, .{ .cols = 80, .rows = 24 });
    defer t.deinit(testing.allocator);

    const S = struct {
        var written: ?[]const u8 = null;
        fn writePty(_: *Handler, data: [:0]const u8) void {
            written = testing.allocator.dupe(u8, data) catch @panic("OOM");
        }
    };
    S.written = null;

    var handler: Handler = .init(&t);
    handler.effects.write_pty = &S.writePty;

    var s: Stream = .initAlloc(testing.allocator, handler);
    defer s.deinit();

    // Without size effect, size reports should be silently ignored
    s.nextSlice("\x1b[14t");
    try testing.expect(S.written == null);
}

test "size report csi_21_t title" {
    var t: Terminal = try .init(testing.allocator, .{ .cols = 80, .rows = 24 });
    defer t.deinit(testing.allocator);

    const S = struct {
        var written: ?[]const u8 = null;
        fn writePty(_: *Handler, data: [:0]const u8) void {
            written = testing.allocator.dupe(u8, data) catch @panic("OOM");
        }
    };
    S.written = null;

    var handler: Handler = .init(&t);
    handler.effects.write_pty = &S.writePty;

    var s: Stream = .initAlloc(testing.allocator, handler);
    defer s.deinit();

    // Set a title first
    s.nextSlice("\x1b]2;My Title\x1b\\");

    // CSI 21 t - report title (no size effect needed)
    s.nextSlice("\x1b[21t");
    defer testing.allocator.free(S.written.?);
    try testing.expectEqualStrings("\x1b]lMy Title\x1b\\", S.written.?);
}

test "enquiry no effect" {
    var t: Terminal = try .init(testing.allocator, .{ .cols = 80, .rows = 24 });
    defer t.deinit(testing.allocator);

    const S = struct {
        var written: ?[]const u8 = null;
        fn writePty(_: *Handler, data: [:0]const u8) void {
            written = testing.allocator.dupe(u8, data) catch @panic("OOM");
        }
    };
    S.written = null;

    var handler: Handler = .init(&t);
    handler.effects.write_pty = &S.writePty;

    var s: Stream = .initAlloc(testing.allocator, handler);
    defer s.deinit();

    // ENQ without enquiry effect should not write anything
    s.nextSlice("\x05");
    try testing.expect(S.written == null);
}

test "enquiry with effect" {
    var t: Terminal = try .init(testing.allocator, .{ .cols = 80, .rows = 24 });
    defer t.deinit(testing.allocator);

    const S = struct {
        var written: ?[]const u8 = null;
        fn writePty(_: *Handler, data: [:0]const u8) void {
            written = testing.allocator.dupe(u8, data) catch @panic("OOM");
        }
        fn enquiry(_: *Handler) []const u8 {
            return "ghostty";
        }
    };
    S.written = null;

    var handler: Handler = .init(&t);
    handler.effects.write_pty = &S.writePty;
    handler.effects.enquiry = &S.enquiry;

    var s: Stream = .initAlloc(testing.allocator, handler);
    defer s.deinit();

    s.nextSlice("\x05");
    defer testing.allocator.free(S.written.?);
    try testing.expectEqualStrings("ghostty", S.written.?);
}

test "enquiry with empty response" {
    var t: Terminal = try .init(testing.allocator, .{ .cols = 80, .rows = 24 });
    defer t.deinit(testing.allocator);

    const S = struct {
        var written: ?[]const u8 = null;
        fn writePty(_: *Handler, data: [:0]const u8) void {
            written = testing.allocator.dupe(u8, data) catch @panic("OOM");
        }
        fn enquiry(_: *Handler) []const u8 {
            return "";
        }
    };
    S.written = null;

    var handler: Handler = .init(&t);
    handler.effects.write_pty = &S.writePty;
    handler.effects.enquiry = &S.enquiry;

    var s: Stream = .initAlloc(testing.allocator, handler);
    defer s.deinit();

    // Empty enquiry response should not write anything
    s.nextSlice("\x05");
    try testing.expect(S.written == null);
}

test "device status: operating status" {
    var t: Terminal = try .init(testing.allocator, .{ .cols = 80, .rows = 24 });
    defer t.deinit(testing.allocator);

    const S = struct {
        var written: ?[]const u8 = null;
        fn writePty(_: *Handler, data: [:0]const u8) void {
            if (written) |old| testing.allocator.free(old);
            written = testing.allocator.dupe(u8, data) catch @panic("OOM");
        }
    };
    S.written = null;
    defer if (S.written) |old| testing.allocator.free(old);

    var handler: Handler = .init(&t);
    handler.effects.write_pty = &S.writePty;

    var s: Stream = .initAlloc(testing.allocator, handler);
    defer s.deinit();

    // CSI 5 n — operating status report
    s.nextSlice("\x1B[5n");
    try testing.expectEqualStrings("\x1B[0n", S.written.?);
}

test "device status: cursor position" {
    var t: Terminal = try .init(testing.allocator, .{ .cols = 80, .rows = 24 });
    defer t.deinit(testing.allocator);

    const S = struct {
        var written: ?[]const u8 = null;
        fn writePty(_: *Handler, data: [:0]const u8) void {
            if (written) |old| testing.allocator.free(old);
            written = testing.allocator.dupe(u8, data) catch @panic("OOM");
        }
    };
    S.written = null;
    defer if (S.written) |old| testing.allocator.free(old);

    var handler: Handler = .init(&t);
    handler.effects.write_pty = &S.writePty;

    var s: Stream = .initAlloc(testing.allocator, handler);
    defer s.deinit();

    // Default position is 0,0 — reported as 1,1
    s.nextSlice("\x1B[6n");
    try testing.expectEqualStrings("\x1B[1;1R", S.written.?);

    // Move cursor to row 5, col 10
    s.nextSlice("\x1B[5;10H");
    s.nextSlice("\x1B[6n");
    try testing.expectEqualStrings("\x1B[5;10R", S.written.?);
}

test "device status: cursor position with origin mode" {
    var t: Terminal = try .init(testing.allocator, .{ .cols = 80, .rows = 24 });
    defer t.deinit(testing.allocator);

    const S = struct {
        var written: ?[]const u8 = null;
        fn writePty(_: *Handler, data: [:0]const u8) void {
            if (written) |old| testing.allocator.free(old);
            written = testing.allocator.dupe(u8, data) catch @panic("OOM");
        }
    };
    S.written = null;
    defer if (S.written) |old| testing.allocator.free(old);

    var handler: Handler = .init(&t);
    handler.effects.write_pty = &S.writePty;

    var s: Stream = .initAlloc(testing.allocator, handler);
    defer s.deinit();

    // Set scroll region rows 5-20
    s.nextSlice("\x1B[5;20r");
    // Enable origin mode
    s.nextSlice("\x1B[?6h");
    // Move to row 3, col 5 within the region
    s.nextSlice("\x1B[3;5H");
    // Query cursor position
    s.nextSlice("\x1B[6n");
    // Should report position relative to the scroll region
    try testing.expectEqualStrings("\x1B[3;5R", S.written.?);
}

test "device status: color scheme dark" {
    var t: Terminal = try .init(testing.allocator, .{ .cols = 80, .rows = 24 });
    defer t.deinit(testing.allocator);

    const S = struct {
        var written: ?[]const u8 = null;
        fn writePty(_: *Handler, data: [:0]const u8) void {
            if (written) |old| testing.allocator.free(old);
            written = testing.allocator.dupe(u8, data) catch @panic("OOM");
        }
        fn colorScheme(_: *Handler) ?device_status.ColorScheme {
            return .dark;
        }
    };
    S.written = null;
    defer if (S.written) |old| testing.allocator.free(old);

    var handler: Handler = .init(&t);
    handler.effects.write_pty = &S.writePty;
    handler.effects.color_scheme = &S.colorScheme;

    var s: Stream = .initAlloc(testing.allocator, handler);
    defer s.deinit();

    // CSI ? 996 n — color scheme query
    s.nextSlice("\x1B[?996n");
    try testing.expectEqualStrings("\x1B[?997;1n", S.written.?);
}

test "device status: color scheme light" {
    var t: Terminal = try .init(testing.allocator, .{ .cols = 80, .rows = 24 });
    defer t.deinit(testing.allocator);

    const S = struct {
        var written: ?[]const u8 = null;
        fn writePty(_: *Handler, data: [:0]const u8) void {
            if (written) |old| testing.allocator.free(old);
            written = testing.allocator.dupe(u8, data) catch @panic("OOM");
        }
        fn colorScheme(_: *Handler) ?device_status.ColorScheme {
            return .light;
        }
    };
    S.written = null;
    defer if (S.written) |old| testing.allocator.free(old);

    var handler: Handler = .init(&t);
    handler.effects.write_pty = &S.writePty;
    handler.effects.color_scheme = &S.colorScheme;

    var s: Stream = .initAlloc(testing.allocator, handler);
    defer s.deinit();

    // CSI ? 996 n — color scheme query
    s.nextSlice("\x1B[?996n");
    try testing.expectEqualStrings("\x1B[?997;2n", S.written.?);
}

test "device status: color scheme without callback" {
    var t: Terminal = try .init(testing.allocator, .{ .cols = 80, .rows = 24 });
    defer t.deinit(testing.allocator);

    const S = struct {
        var written: ?[]const u8 = null;
        fn writePty(_: *Handler, data: [:0]const u8) void {
            if (written) |old| testing.allocator.free(old);
            written = testing.allocator.dupe(u8, data) catch @panic("OOM");
        }
    };
    S.written = null;
    defer if (S.written) |old| testing.allocator.free(old);

    var handler: Handler = .init(&t);
    handler.effects.write_pty = &S.writePty;

    var s: Stream = .initAlloc(testing.allocator, handler);
    defer s.deinit();

    // Without color_scheme effect, query should be silently ignored
    s.nextSlice("\x1B[?996n");
    try testing.expect(S.written == null);
}

test "device status: readonly ignores all" {
    var t: Terminal = try .init(testing.allocator, .{ .cols = 80, .rows = 24 });
    defer t.deinit(testing.allocator);

    var s: Stream = .initAlloc(testing.allocator, .init(&t));
    defer s.deinit();

    // All device status queries should be silently ignored without effects
    s.nextSlice("\x1B[5n");
    s.nextSlice("\x1B[6n");
    s.nextSlice("\x1B[?996n");

    // Terminal should still be functional
    s.nextSlice("Test");
    const str = try t.plainString(testing.allocator);
    defer testing.allocator.free(str);
    try testing.expectEqualStrings("Test", str);
}

test "device attributes: primary DA" {
    var t: Terminal = try .init(testing.allocator, .{ .cols = 80, .rows = 24 });
    defer t.deinit(testing.allocator);

    const S = struct {
        var written: ?[]const u8 = null;
        fn writePty(_: *Handler, data: [:0]const u8) void {
            if (written) |old| testing.allocator.free(old);
            written = testing.allocator.dupe(u8, data) catch @panic("OOM");
        }
        fn da(_: *Handler) device_attributes.Attributes {
            return .{};
        }
    };
    S.written = null;
    defer if (S.written) |old| testing.allocator.free(old);

    var handler: Handler = .init(&t);
    handler.effects.write_pty = &S.writePty;
    handler.effects.device_attributes = &S.da;

    var s: Stream = .initAlloc(testing.allocator, handler);
    defer s.deinit();

    s.nextSlice("\x1B[c");
    try testing.expectEqualStrings("\x1b[?62;22c", S.written.?);
}

test "device attributes: secondary DA" {
    var t: Terminal = try .init(testing.allocator, .{ .cols = 80, .rows = 24 });
    defer t.deinit(testing.allocator);

    const S = struct {
        var written: ?[]const u8 = null;
        fn writePty(_: *Handler, data: [:0]const u8) void {
            if (written) |old| testing.allocator.free(old);
            written = testing.allocator.dupe(u8, data) catch @panic("OOM");
        }
        fn da(_: *Handler) device_attributes.Attributes {
            return .{};
        }
    };
    S.written = null;
    defer if (S.written) |old| testing.allocator.free(old);

    var handler: Handler = .init(&t);
    handler.effects.write_pty = &S.writePty;
    handler.effects.device_attributes = &S.da;

    var s: Stream = .initAlloc(testing.allocator, handler);
    defer s.deinit();

    s.nextSlice("\x1B[>c");
    try testing.expectEqualStrings("\x1b[>1;0;0c", S.written.?);
}

test "device attributes: tertiary DA" {
    var t: Terminal = try .init(testing.allocator, .{ .cols = 80, .rows = 24 });
    defer t.deinit(testing.allocator);

    const S = struct {
        var written: ?[]const u8 = null;
        fn writePty(_: *Handler, data: [:0]const u8) void {
            if (written) |old| testing.allocator.free(old);
            written = testing.allocator.dupe(u8, data) catch @panic("OOM");
        }
        fn da(_: *Handler) device_attributes.Attributes {
            return .{};
        }
    };
    S.written = null;
    defer if (S.written) |old| testing.allocator.free(old);

    var handler: Handler = .init(&t);
    handler.effects.write_pty = &S.writePty;
    handler.effects.device_attributes = &S.da;

    var s: Stream = .initAlloc(testing.allocator, handler);
    defer s.deinit();

    s.nextSlice("\x1B[=c");
    try testing.expectEqualStrings("\x1bP!|00000000\x1b\\", S.written.?);
}

test "device attributes: readonly ignores" {
    var t: Terminal = try .init(testing.allocator, .{ .cols = 80, .rows = 24 });
    defer t.deinit(testing.allocator);

    var s: Stream = .initAlloc(testing.allocator, .init(&t));
    defer s.deinit();

    // All DA queries should be silently ignored without effects
    s.nextSlice("\x1B[c");
    s.nextSlice("\x1B[>c");
    s.nextSlice("\x1B[=c");

    // Terminal should still be functional
    s.nextSlice("Test");
    const str = try t.plainString(testing.allocator);
    defer testing.allocator.free(str);
    try testing.expectEqualStrings("Test", str);
}

test "device attributes: custom response" {
    var t: Terminal = try .init(testing.allocator, .{ .cols = 80, .rows = 24 });
    defer t.deinit(testing.allocator);

    const S = struct {
        var written: ?[]const u8 = null;
        fn writePty(_: *Handler, data: [:0]const u8) void {
            if (written) |old| testing.allocator.free(old);
            written = testing.allocator.dupe(u8, data) catch @panic("OOM");
        }
        fn da(_: *Handler) device_attributes.Attributes {
            return .{
                .primary = .{
                    .conformance_level = .vt420,
                    .features = &.{ .ansi_color, .clipboard },
                },
                .secondary = .{
                    .device_type = .vt420,
                    .firmware_version = 100,
                },
            };
        }
    };
    S.written = null;
    defer if (S.written) |old| testing.allocator.free(old);

    var handler: Handler = .init(&t);
    handler.effects.write_pty = &S.writePty;
    handler.effects.device_attributes = &S.da;

    var s: Stream = .initAlloc(testing.allocator, handler);
    defer s.deinit();

    s.nextSlice("\x1B[c");
    try testing.expectEqualStrings("\x1b[?64;22;52c", S.written.?);

    s.nextSlice("\x1B[>c");
    try testing.expectEqualStrings("\x1b[>41;100;0c", S.written.?);
}

test "kitty graphics APC response" {
    if (comptime !build_options.kitty_graphics) return error.SkipZigTest;

    var t: Terminal = try .init(testing.allocator, .{ .cols = 10, .rows = 10 });
    defer t.deinit(testing.allocator);

    const S = struct {
        var written: ?[]const u8 = null;
        fn writePty(_: *Handler, data: [:0]const u8) void {
            if (written) |old| testing.allocator.free(old);
            written = testing.allocator.dupe(u8, data) catch @panic("OOM");
        }
    };
    S.written = null;
    defer if (S.written) |old| testing.allocator.free(old);

    var handler: Handler = .init(&t);
    handler.effects.write_pty = &S.writePty;

    var s: Stream = .initAlloc(testing.allocator, handler);
    defer s.deinit();

    // Send a kitty graphics transmit command with image id 1
    s.nextSlice("\x1b_Ga=t,t=d,f=24,i=1,s=1,v=2,c=10,r=1;////////\x1b\\");

    // Should have written a response back
    try testing.expectEqualStrings("\x1b_Gi=1;OK\x1b\\", S.written.?);
}

test "kitty graphics via APC" {
    if (comptime !build_options.kitty_graphics) return error.SkipZigTest;

    var t: Terminal = try .init(testing.allocator, .{ .cols = 10, .rows = 10 });
    defer t.deinit(testing.allocator);

    const handler: Handler = .init(&t);
    var s: Stream = .initAlloc(testing.allocator, handler);
    defer s.deinit();

    // Send a kitty graphics transmit command via APC:
    // ESC _ G <payload> ESC \
    // a=t,t=d,f=24,i=1,s=1,v=2,c=10,r=1;//////// (1x2 RGB direct)
    s.nextSlice("\x1b_Ga=t,t=d,f=24,i=1,s=1,v=2,c=10,r=1;////////\x1b\\");

    const storage = &t.screens.active.kitty_images;
    const img = storage.imageById(1).?;
    try testing.expectEqual(.rgb, img.format);
}
