import sys
import unittest

from harness import Zutty


class StartupTest(unittest.TestCase):
    def test_child_environment_winsize_and_sigwinch(self):
        program = r'''
import fcntl, os, signal, struct, termios

def size():
    rows, columns, _, _ = struct.unpack(
        "HHHH", fcntl.ioctl(0, termios.TIOCGWINSZ, b"\0" * 8)
    )
    return f"{columns}x{rows}"

def emit(prefix):
    os.write(1, f"{prefix}|{size()}\r\n".encode())

def resized(signum, frame):
    emit("SIGWINCH")
    raise SystemExit(0)

signal.signal(signal.SIGWINCH, resized)
os.write(
    1,
    f"{os.environ.get('TERM')}|{os.environ.get('ZUTTY_VERSION')}|{size()}\r\n".encode(),
)
signal.pause()
'''
        with Zutty(columns=80, rows=4) as terminal:
            terminal.spawn(sys.executable, "-c", program)
            terminal.wait_read_pty()
            self.assertEqual(
                terminal.snapshot().lines[0].rstrip(),
                "xterm-256color|0.14|80x4",
            )

            terminal.resize(80, 5)
            terminal.wait_read_pty()
            status, _ = terminal.wait_child()
            self.assertEqual(status, 0)
            self.assertEqual(
                terminal.snapshot().lines[1].rstrip(),
                "SIGWINCH|80x5",
            )


if __name__ == "__main__":
    unittest.main()
