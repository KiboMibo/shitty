#!/usr/bin/env python3

import inspect
import signal
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
TESTS = ROOT / "tests"
PORTED = Path(__file__).resolve().parent / "ported"
sys.path.insert(0, str(TESTS))

from harness import Zutty

sys.path.insert(0, str(PORTED))

import esc
import escargs
import esccmd
import escio
import esclog
import esctypes
import escutil
import tests as upstream_tests


class ControlBackend:
    def __init__(self, terminal):
        self.terminal = terminal
        self.responses = bytearray()

    @staticmethod
    def encode(value):
        try:
            return value.encode("latin1")
        except UnicodeEncodeError:
            return value.encode("utf-8")

    def write(self, value, sideChannelOk=True):
        del sideChannelOk
        self.terminal.write(self.encode(value))
        self.responses.extend(self.terminal.read_input())

    def read(self, count):
        self.responses.extend(self.terminal.read_input())
        if len(self.responses) < count:
            raise esctypes.InternalError(
                f"Missing terminal response: wanted {count} bytes, "
                f"have {bytes(self.responses)!r}"
            )
        result = bytes(self.responses[:count])
        del self.responses[:count]
        return result.decode("latin1")

    def snapshot(self):
        return self.terminal.model_snapshot()


backend = None


def install_backend(control):
    global backend
    backend = control
    escio.Init = lambda: None
    escio.Shutdown = lambda: None
    escio.Write = control.write
    escio.read = control.read

    def get_screen_size():
        snapshot = control.snapshot()
        return esctypes.Size(snapshot.columns, snapshot.rows)

    def checksum(rect):
        snapshot = control.snapshot()
        result = 0
        for point in rect.points():
            cell = snapshot.cell(point.x() - 1, point.y() - 1)
            result = (result + ord(cell.char)) & 0xffff
        return result

    def assert_screen(rect, expected_lines):
        escutil.gHaveAsserted = True
        if rect.height() != len(expected_lines):
            raise esctypes.InternalError("screen assertion height mismatch")
        snapshot = control.snapshot()
        actual_lines = []
        for row in range(rect.top() - 1, rect.bottom()):
            actual = ""
            for column in range(rect.left() - 1, rect.right()):
                cell = snapshot.cell(column, row)
                if cell.double_width_continuation:
                    continue
                if cell.grapheme:
                    actual += "".join(map(chr, cell.grapheme))
                else:
                    actual += cell.char
            actual_lines.append(actual)
        if actual_lines != expected_lines:
            raise esctypes.TestFailure(actual_lines, expected_lines)

    replacements = {
        escutil.GetScreenSize: get_screen_size,
        escutil.GetChecksumOfRect: checksum,
        escutil.AssertScreenCharsInRectEqual: assert_screen,
    }
    for module in tuple(sys.modules.values()):
        namespace = getattr(module, "__dict__", None)
        if namespace is None:
            continue
        for name, value in tuple(namespace.items()):
            for original, replacement in replacements.items():
                if value is original:
                    namespace[name] = replacement
                    break


def configure_args(case_id):
    escargs.args = escargs.parser.parse_args([])
    escargs.args.expected_terminal = "xterm"
    escargs.args.annotation_terminal = "zutty"
    escargs.args.xterm_checksum = 334
    escargs.args.max_vt_level = 5
    escargs.args.options = [escargs.XTERM_WINOPS_ENABLED]
    log_directory = ROOT / ".build" / "esctest-logs"
    log_directory.mkdir(parents=True, exist_ok=True)
    escargs.args.logfile = str(log_directory / f"{case_id}.log")
    escargs.args.v = 1
    esc.vtLevel = 5
    esclog.log = ""
    esclog.gLogFile = None


def reset(terminal):
    terminal.resize(80, 25)
    terminal.write(b"\x1bc\x1b[?7h\x1b[?69l\x1b[4l\x1b[20l\x1b[2J\x1b[H")
    terminal.read_input()
    escio.use8BitControls = False
    escutil.gHaveAsserted = False
    escutil.gNextId = 1


def find_case(case_id):
    class_name, method_name = case_id.split(".", 1)
    for test_class in upstream_tests.tests:
        if test_class.__name__ == class_name:
            method = getattr(test_class(), method_name)
            if not inspect.ismethod(method):
                break
            return method
    raise RuntimeError(f"unknown esctest case {case_id}")


def run(case_id):
    configure_args(case_id)
    terminal = Zutty(
        columns=80,
        rows=25,
        save_lines=500,
        extra_arguments=("-allowWindowOps", "true"),
    )
    try:
        install_backend(ControlBackend(terminal))
        reset(terminal)
        find_case(case_id)()
        if not escutil.gHaveAsserted:
            raise esctypes.InternalError("upstream test made no assertion")
    except BaseException:
        terminal.process.kill()
        terminal.process.wait()
        terminal.stream.close()
        terminal.socket.close()
        raise
    terminal.close()


def main():
    if len(sys.argv) != 4:
        raise SystemExit("usage: adapter.py CASE XFAIL_FILE STAMP")
    case_id = sys.argv[1]
    xfails = {
        line.strip() for line in Path(sys.argv[2]).read_text().splitlines()
        if line.strip() and not line.startswith("#")
    }
    stamp = Path(sys.argv[3])
    def timeout_handler(_signal, _frame):
        raise TimeoutError("case exceeded 15 seconds")

    signal.signal(signal.SIGALRM, timeout_handler)
    signal.alarm(15)
    mismatch = ""
    try:
        run(case_id)
    except Exception as error:
        summary = str(error).splitlines()[0] if str(error) else ""
        if len(summary) > 500:
            summary = summary[:500] + "..."
        mismatch = f"{type(error).__name__}: {summary}"
    finally:
        signal.alarm(0)
    if case_id in xfails:
        if not mismatch:
            print(f"XPASS esctest/{case_id}", file=sys.stderr)
            return 1
        print(f"XFAIL esctest/{case_id}: {mismatch}")
    elif mismatch:
        print(f"FAIL esctest/{case_id}: {mismatch}", file=sys.stderr)
        return 1
    else:
        print(f"PASS esctest/{case_id}")
    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.touch()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
