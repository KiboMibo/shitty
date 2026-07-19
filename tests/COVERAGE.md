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
- SGR flags, truecolor, wide cells, wrap markers and DEC line drawing;
- DA, DSR, DECRQSS, palette and dynamic-color replies;
- OSC actions, bell, OSC 7 paths, OSC 8 hyperlinks and OSC 52 clipboard data;
- legacy/application keyboard, modifiers, function keys, paste and kitty keys;
- mouse/focus negotiation plus default, UTF-8, SGR and URXVT encodings;
- linear/rectangular selection and selection while scrolled;
- growing/shrinking both primary and alternate screens.

Still requiring a platform boundary before it can be tested headlessly:

- GLFW event translation, including physical keyboard layout and IME input;
- GLFW mouse event policy (selection override, click counting and cell dedupe);
- clipboard ownership and OSC 52 integration;
- Vulkan raster output, font fallback and glyph metrics;
- Wayland/X11 window state, scale and grid-snapped interactive resizing.

Those frontend behaviors should be moved into platform-neutral components and
driven by the same control socket. A smaller renderer tier can then compare
offscreen images only where cell snapshots cannot express the contract.
