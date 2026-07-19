# Zutty integration tests

Build Zutty and run the black-box tests through the build graph:

```sh
./build
```

The harness starts the regular `zutty` binary in headless test mode. It sends
terminal output and control events over an inherited Unix socket and reads
logical screen snapshots from the same socket. The simulated child side is a
real raw PTY, so replies, keyboard input and terminal resizing follow the same
path as in an interactive session.

Every file in this directory is deliberately kept at one level. Test modules
are grouped by behavior (`test_parser.py`, `test_modes.py`, and so on), without
nested fixtures or protocol directories.

Snapshots expose the terminal's canonical cell grid: Unicode code point,
double-width markers, wrap state, text attributes, foreground/background RGB,
hyperlink id, cursor, selection, scroll offset and refresh count. Other control
commands cover resize, keyboard and kitty-key events, paste, focus, selection,
OSC/bell actions, hyperlink lookup, PTY replies and mode state.
