# Behavior coverage

This suite treats the headless control protocol as a black-box terminal client.
Assertions target externally visible state and bytes, not private `Vterm`
methods.

Covered now:

- streaming parser boundaries for CSI, private CSI, OSC, DCS and UTF-8;
- C0 controls, cursor movement, save/restore, tabs and scrolling regions;
- autowrap, alternate screen, synchronized output and cursor modes;
- insert/delete/erase operations and insert mode;
- scrollback on primary and alternate screens, including output while scrolled;
- full, top-anchored and middle scrolling regions, with fixed outer rows;
- scroll-up capture, reverse scroll, history capacity rollover and zero capacity;
- linefeed/index capture at a region boundary and large-count clamping;
- scrollback cell attributes, hyperlinks, selection lifetime and viewport anchoring;
- fractional frontend wheel accumulation, both axes, local/reporting
  transitions and Shift override;
- shrink-to-history and grow-from-history resize behavior, including complete
  wide/grapheme cell invariants across width and height changes;
- Codex-style synchronized redraws plus a slow reference scrolling model;
- SGR flags, truecolor, wide cells, wrap markers and DEC line drawing;
- ISO 2022 G0-G3 designation, GL/GR locking and single shifts, complete NRC
  replacement variants, DEC Special/Technical/Supplemental and VT52 changes;
- Unicode 17 extended grapheme boundaries, including Prepend, SpacingMark,
  Indic linkers, Hangul, regional indicators and emoji ZWJ sequences;
- DA, DSR, DECRQSS, palette and dynamic-color replies;
- renderer input state for blink phases, cursor blink, screen reverse video,
  dynamic palette, selection colors and grapheme payloads;
- OSC actions, bell, OSC 7 paths, OSC 8 hyperlinks and OSC 52 clipboard data;
- legacy/application keyboard, modifiers, function keys, paste and kitty keys;
- mouse/focus negotiation plus default, UTF-8, SGR and URXVT encodings;
- frontend pointer/button policy, content scale, motion dedupe, locator updates,
  local selection and click snapping with virtual time;
- linear/rectangular selection, soft-wrapped logical-line copying (including
  wide pre-wrap), reverse drags and selection while scrolled;
- growing/shrinking both primary and alternate screens.

The optional compatibility tier additionally drives the complete top-level
upstream `vttest` suite to a clean exit on Zutty's real PTY. It also records and
validates the same query set under Zutty, xterm, foot and kitty when those
terminals are installed.

Still requiring a platform boundary before it can be tested headlessly:

- GLFW event translation, including physical keyboard layout and IME input;
- clipboard ownership and OSC 52 integration;
- Vulkan raster output, font fallback and glyph metrics;
- Wayland/X11 window state, scale and grid-snapped interactive resizing.

Those frontend behaviors should be moved into platform-neutral components and
driven by the same control socket. A smaller renderer tier can then compare
offscreen images only where cell snapshots cannot express the contract.
