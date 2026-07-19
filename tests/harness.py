import socket
import subprocess
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


@dataclass
class Snapshot:
    columns: int
    rows: int
    cursor_x: int
    cursor_y: int
    cursor_style: int
    view_offset: int
    lines: list[str]


class Zutty:
    def __init__(self, columns=80, rows=24, save_lines=500):
        parent, child = socket.socketpair()
        self.socket = parent
        self.stream = parent.makefile("rwb", buffering=0)
        self.process = subprocess.Popen(
            [
                str(ROOT / "zutty"),
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

    def page_up(self):
        self.command("PAGE_UP")

    def page_down(self):
        self.command("PAGE_DOWN")

    def snapshot(self):
        self.stream.write(b"SNAPSHOT\n")
        response = self._readline().split(" ", 7)
        if len(response) != 8 or response[0] != "OK":
            raise RuntimeError("invalid snapshot response")
        columns, rows, cursor_x, cursor_y, style, offset = map(
            int, response[1:7]
        )
        cells = response[7]
        expected = columns * rows * 4
        if len(cells) != expected:
            raise RuntimeError("invalid snapshot cell count")
        text = "".join(
            chr(int(cells[k : k + 4], 16))
            for k in range(0, len(cells), 4)
        )
        lines = [
            text[row * columns : (row + 1) * columns]
            for row in range(rows)
        ]
        return Snapshot(
            columns, rows, cursor_x, cursor_y, style, offset, lines
        )
