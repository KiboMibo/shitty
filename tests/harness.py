import socket
import subprocess
import os
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ZUTTY = Path(os.environ.get("ZUTTY_TEST_BINARY", ROOT / "zutty"))


@dataclass
class Cell:
    char: str
    double_width: bool
    double_width_continuation: bool
    bold: bool
    italic: bool
    underline: bool
    inverse: bool
    wrapped: bool
    foreground: tuple[int, int, int]
    background: tuple[int, int, int]
    hyperlink: int


@dataclass
class Snapshot:
    columns: int
    rows: int
    cursor_x: int
    cursor_y: int
    cursor_style: int
    view_offset: int
    refresh_count: int
    selection: tuple[int, int, int, int]
    rectangular_selection: bool
    cells: list[Cell]
    lines: list[str]

    def cell(self, column, row):
        return self.cells[row * self.columns + column]


class Zutty:
    def __init__(self, columns=80, rows=24, save_lines=500):
        parent, child = socket.socketpair()
        self.socket = parent
        self.stream = parent.makefile("rwb", buffering=0)
        self.process = subprocess.Popen(
            [
                str(ZUTTY),
                "--test-fd",
                str(child.fileno()),
                "-geometry",
                f"{columns}x{rows}",
                "-saveLines",
                str(save_lines),
                "-quiet",
            ],
            pass_fds=(child.fileno(),),
        )
        child.close()
        if self._readline() != "READY":
            raise RuntimeError("zutty test mode did not become ready")

    def close(self):
        if self.process.poll() is None:
            try:
                self.command("QUIT")
            finally:
                self.process.wait(timeout=5)
        self.stream.close()
        self.socket.close()

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()

    def _readline(self):
        line = self.stream.readline()
        if not line:
            raise RuntimeError(f"zutty exited with {self.process.poll()}")
        return line.decode("ascii").rstrip("\n")

    def command(self, command):
        self.stream.write(command.encode("ascii") + b"\n")
        response = self._readline()
        if response.startswith("ERR "):
            raise RuntimeError(response[4:])
        if response != "OK":
            raise RuntimeError(f"unexpected response: {response}")

    def write(self, output):
        self.command("WRITE " + output.hex())

    def write_chunks(self, *chunks):
        for chunk in chunks:
            self.write(chunk)

    def page_up(self):
        self.command("PAGE_UP")

    def page_down(self):
        self.command("PAGE_DOWN")

    def resize(self, columns, rows):
        self.command(f"RESIZE {columns} {rows}")

    def key(self, name, modifiers=0):
        self.command(f"KEY {name} {modifiers}")

    def char(self, character, modifiers=0):
        if isinstance(character, str):
            character = ord(character)
        self.command(f"CHAR {character} {modifiers}")

    def kitty_key(self, key, shifted=0, base=0, modifiers=0, event=1):
        self.command(
            f"KITTY_KEY {key} {shifted} {base} {modifiers} {event}"
        )

    def paste(self, data):
        self.command("PASTE " + data.hex())

    def focus(self, focused):
        self.command(f"FOCUS {int(focused)}")

    def select_start(self, column, row):
        self.command(f"SELECT_START {column} {row}")

    def select_update(self, column, row):
        self.command(f"SELECT_UPDATE {column} {row}")

    def select_rectangular(self):
        self.command("SELECT_RECTANGULAR")

    def _read_hex_response(self, command):
        self.stream.write(command.encode("ascii") + b"\n")
        response = self._readline().split(" ", 1)
        if response[0] != "OK":
            raise RuntimeError(f"invalid response to {command}")
        return bytes.fromhex(response[1]) if len(response) == 2 else b""

    def select_finish(self):
        return self._read_hex_response("SELECT_FINISH")

    def hyperlink(self, column, row):
        return self._read_hex_response(f"HYPERLINK {column} {row}").decode()

    def read_actions(self):
        return self._read_hex_response("READ_ACTIONS").decode().splitlines()

    def state(self):
        self.stream.write(b"STATE\n")
        response = self._readline().split()
        if len(response) != 5 or response[0] != "OK":
            raise RuntimeError("invalid state response")
        return tuple(map(int, response[1:]))

    def read_input(self):
        return self._read_hex_response("READ_INPUT")

    def snapshot(self):
        self.stream.write(b"SNAPSHOT\n")
        response = self._readline().split(" ", 13)
        if len(response) != 14 or response[0] != "OK":
            raise RuntimeError("invalid snapshot response")
        (
            columns,
            rows,
            cursor_x,
            cursor_y,
            style,
            offset,
            refresh_count,
            selection_tl_x,
            selection_tl_y,
            selection_br_x,
            selection_br_y,
            rectangular_selection,
        ) = map(
            int, response[1:13]
        )
        encoded_cells = response[13]
        record_size = 26
        expected = columns * rows * record_size
        if len(encoded_cells) != expected:
            raise RuntimeError("invalid snapshot cell count")
        cells = []
        for offset_in_cells in range(0, len(encoded_cells), record_size):
            record = encoded_cells[
                offset_in_cells : offset_in_cells + record_size
            ]
            flags = int(record[4:6], 16)
            cells.append(
                Cell(
                    char=chr(int(record[0:4], 16)),
                    double_width=bool(flags & 1),
                    double_width_continuation=bool(flags & 2),
                    bold=bool(flags & 4),
                    italic=bool(flags & 8),
                    underline=bool(flags & 16),
                    inverse=bool(flags & 32),
                    wrapped=bool(flags & 64),
                    foreground=tuple(
                        int(record[k : k + 2], 16) for k in (6, 8, 10)
                    ),
                    background=tuple(
                        int(record[k : k + 2], 16) for k in (12, 14, 16)
                    ),
                    hyperlink=int(record[18:26], 16),
                )
            )
        text = "".join(cell.char for cell in cells)
        lines = [
            text[row * columns : (row + 1) * columns]
            for row in range(rows)
        ]
        return Snapshot(
            columns,
            rows,
            cursor_x,
            cursor_y,
            style,
            offset,
            refresh_count,
            (selection_tl_x, selection_tl_y, selection_br_x, selection_br_y),
            bool(rectangular_selection),
            cells,
            lines,
        )
