#!/usr/bin/env python3

from dataclasses import dataclass

from harness import Zutty


@dataclass(frozen=True)
class Scrollback:
    history_lines: int
    total_lines: int
    screen_lines: int
    viewport_offset: int


@dataclass(frozen=True)
class Capabilities:
    reflow: bool


class Backend:
    """Local Termless-shaped backend over Zutty's offline control API."""

    def __init__(self, columns=80, rows=24):
        self.terminal = Zutty(
            columns=columns,
            rows=rows,
            glyph_px=8,
            glyph_py=16,
            extra_arguments=("-allowWindowOps", "true"),
        )
        self.title = ""
        self.capabilities = Capabilities(reflow=False)

    def close(self):
        self.terminal.close()

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()

    def feed(self, data):
        if isinstance(data, str):
            data = data.encode()
        self.terminal.write(data)
        for action in self.terminal.read_actions():
            if action.startswith("OSC 2 "):
                self.title = bytes.fromhex(action[6:]).decode(errors="replace")

    def resize(self, columns, rows):
        self.terminal.resize(columns, rows)

    def reset(self):
        self.feed(b"\x1bc")

    def snapshot(self):
        return self.terminal.model_snapshot()

    def text(self):
        return "\n".join(line.rstrip() for line in self.snapshot().lines)

    def cell(self, row, column):
        return self.snapshot().cell(column, row)

    def cursor(self):
        snapshot = self.snapshot()
        return snapshot.cursor_x, snapshot.cursor_y

    def mode(self, name):
        conformance = self.terminal.conformance_state()
        if name == "altScreen":
            return conformance["screen"] == "Alternate"
        if name == "applicationCursor":
            return conformance["DECCKM"]
        if name == "autoWrap":
            return conformance["DECAWM"]
        if name == "mouseTracking":
            return self.terminal.state()[0] != 0
        if name == "focusTracking":
            return self.terminal.state()[2] != 0
        raise ValueError(f"unsupported Termless mode: {name}")

    def scrollback(self):
        return Scrollback(*self.terminal.scrollback_state())

    def response(self, query):
        self.feed(query)
        return self.terminal.read_input()

    def encode_key(self, key, ctrl=False):
        self.terminal.read_input()
        if ctrl:
            self.terminal.frontend_control(key.upper())
        elif key == "Enter":
            self.terminal.key("RETURN")
        elif key == "Escape":
            self.terminal.char(0x1b)
        elif key == "ArrowUp":
            self.terminal.key("UP")
        else:
            raise ValueError(f"unsupported Termless key: {key}")
        return self.terminal.read_input()

    def bracketed_paste_enabled(self):
        marker = b"termless"
        self.terminal.paste(marker)
        return self.terminal.read_input() == b"\x1b[200~" + marker + b"\x1b[201~"


def snapshots_equal(left, right):
    return left.model_digest() == right.model_digest()
