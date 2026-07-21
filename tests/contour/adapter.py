#!/usr/bin/env python3

import difflib
import json
import sys
import time
from pathlib import Path


TESTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TESTS))

from harness import Zutty


PROMPTS = (
    ("Enter choice number", "menu", None),
    ("Bad choice, try again", "menu", None),
    ("Push <RETURN>", "hold", None),
    ("Push the RETURN key", "key", "RETURN"),
    ("Press the backspace key", "key", "BACKSPACE"),
    ("press return to continue", "type", b"\n"),
    ("Enter 0 to exit", "type", b"0\n"),
    ("Repeat a key to quit", "type", b"aa"),
    ("press any key twice to quit", "type", b"aa"),
    ("Finish with TAB", "type", b"\t"),
    ("Finish with RETURN", "type", b"\n"),
    ("Press 'q' to quit", "type", b"q"),
    # This vttest build writes the menu prompt through buffered stdio and then
    # blocks in read(2), so the visible, already-complete menu title is the
    # causal marker available through a terminal screen.
    ("Choose test type:", "menu", None),
    ("0. Exit", "menu", None),
)

MODE_NAMES = (
    "IRM", "SRM", "LNM", "DECCKM", "DECCOLM", "DECSCLM", "DECSCNM",
    "DECOM", "DECAWM", "DECARM", "DECTCEM", "DECNKM", "DECBKM",
    "DECLRMM",
)


def section_rule(title):
    return (f"---- {title} ").ljust(60, "-") + "\n"


def cell_text(cell):
    if cell.double_width_continuation:
        return ""
    if cell.char == "\0":
        return "·"
    if cell.char == " ":
        return "␣"
    if cell.grapheme:
        return "".join(chr(codepoint) for codepoint in cell.grapheme)
    return cell.char


def rendition(cell):
    flags = []
    if cell.bold:
        flags.append("Bold")
    if cell.faint:
        flags.append("Faint")
    if cell.italic:
        flags.append("Italic")
    underline = {
        1: "Underline",
        2: "DoublyUnderlined",
        3: "CurlyUnderlined",
        4: "DottedUnderline",
        5: "DashedUnderline",
    }.get(cell.underline_style)
    if underline:
        flags.append(underline)
    elif cell.underline:
        flags.append("Underline")
    if cell.blink:
        flags.append("Blinking")
    if cell.inverse:
        flags.append("Inverse")
    if cell.conceal:
        flags.append("Hidden")
    if cell.strike:
        flags.append("CrossedOut")
    if cell.overline:
        flags.append("Overline")
    if cell.protected:
        flags.append("CharacterProtectedISO")
    if cell.foreground_index >= 0:
        flags.append(f"fg={cell.foreground_index}")
    elif cell.foreground_index == -1:
        flags.append("fg=#{:02X}{:02X}{:02X}".format(*cell.foreground))
    if cell.background_index >= 0:
        flags.append(f"bg={cell.background_index}")
    elif cell.background_index == -1:
        flags.append("bg=#{:02X}{:02X}{:02X}".format(*cell.background))
    return " ".join(flags) if flags else "default"


def line_flags(cells):
    result = []
    if any(cell.wrapped for cell in cells):
        result.append("Wrapped")
    attribute = cells[0].line_attribute if cells else 0
    if attribute == 1:
        result.append("DoubleHeightTop")
    elif attribute == 2:
        result.append("DoubleHeightBottom")
    elif attribute == 3:
        result.append("DoubleWidth")
    return ", ".join(result)


def dump_screen(terminal):
    snapshot = terminal.model_snapshot()
    state = terminal.conformance_state()
    output = [
        f"@geometry {snapshot.rows}x{snapshot.columns}\n",
        f"@screen {state['screen']}\n",
        f"@cursor line={snapshot.cursor_y + 1} column={snapshot.cursor_x + 1} "
        f"visible={'yes' if state['DECTCEM'] else 'no'}\n",
        "@modes " + " ".join(
            f"{name}={'on' if state[name] else 'off'}" for name in MODE_NAMES
        ) + "\n",
        section_rule("text"),
    ]

    rows = []
    for row in range(snapshot.rows):
        cells = snapshot.cells[
            row * snapshot.columns : (row + 1) * snapshot.columns
        ]
        rows.append(cells)
        output.append(f"{row + 1:02}|{''.join(map(cell_text, cells))}\n")

    output.append(section_rule("attributes"))
    legend = []
    attribute_rows = []
    alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"
    for cells in rows:
        encoded = []
        for cell in cells:
            value = rendition(cell)
            if value == "default":
                encoded.append(".")
                continue
            if value not in legend:
                legend.append(value)
            index = legend.index(value)
            encoded.append(alphabet[index] if index < len(alphabet) else "?")
        attribute_rows.append("".join(encoded))
    for row, encoded in enumerate(attribute_rows, 1):
        output.append(f"{row:02}|{encoded}\n")

    output.append(section_rule("legend"))
    output.append(". = default\n")
    for index, value in enumerate(legend):
        label = alphabet[index] if index < len(alphabet) else "?"
        output.append(f"{label} = {value}\n")

    output.append(section_rule("lineflags"))
    for row, cells in enumerate(rows, 1):
        value = line_flags(cells)
        if value:
            output.append(f"{row:02}|{value}\n")
    return "".join(output)


def difference(expected, actual, name):
    if expected == actual:
        return None
    lines = list(difflib.unified_diff(
        expected.splitlines(keepends=True),
        actual.splitlines(keepends=True),
        fromfile=name,
        tofile="zutty.dump",
    ))
    if len(lines) > 120:
        lines = lines[:120] + ["... diff truncated ...\n"]
    return "".join(lines)


def run_scenario(binary, scenario, keys, data):
    differences = []
    errors = []
    expected_goldens = {
        path.name: path for path in data.glob(f"golden/{scenario}.step*.dump")
    }
    compared = set()
    keys = list(keys)
    step = 0
    armed_after_refresh = -1
    deadline = time.monotonic() + 30
    screen = ""

    with Zutty(columns=80, rows=24, save_lines=2000) as terminal:
        terminal.spawn(binary, "24x80.132")
        while time.monotonic() < deadline:
            status, screen = terminal.poll_child()
            snapshot = terminal.snapshot()
            if status is not None:
                if status != 0:
                    errors.append(f"vttest exited with status {status}")
                break
            if snapshot.refresh_count <= armed_after_refresh:
                time.sleep(0.002)
                continue

            match = next((prompt for prompt in PROMPTS if prompt[0] in screen), None)
            if match is None:
                time.sleep(0.002)
                continue

            _, action, value = match
            if action == "hold":
                name = f"{scenario}.step{step:02}.dump"
                path = expected_goldens.get(name)
                if path is None:
                    differences.append(
                        f"unexpected visual hold at step {step:02}"
                    )
                else:
                    mismatch = difference(path.read_text(), dump_screen(terminal), name)
                    if mismatch:
                        differences.append(mismatch)
                    compared.add(name)
                terminal.input(b"\n")
            elif action == "menu":
                answer = keys.pop(0) if keys else "0"
                terminal.input((answer + "\n").encode())
            elif action == "key":
                terminal.key(value)
            else:
                terminal.input(value)
            step += 1
            armed_after_refresh = snapshot.refresh_count
            deadline = time.monotonic() + 30
        else:
            errors.append(
                f"vttest timed out after {step} prompts; screen was:\n{screen}"
            )

    missing = sorted(set(expected_goldens) - compared)
    if missing:
        differences.append(
            "missing golden checkpoints: " + ", ".join(missing)
        )
    return differences, errors


def main():
    if len(sys.argv) != 5:
        raise SystemExit("usage: adapter.py VTTEST CASE XFAIL_FILE STAMP")
    binary, scenario = sys.argv[1:3]
    xfail_path = Path(sys.argv[3])
    stamp = Path(sys.argv[4])
    data = Path(__file__).resolve().parent
    scenarios = json.loads((data / "scenarios.json").read_text())
    known_failures = {
        line.strip() for line in xfail_path.read_text().splitlines()
        if line.strip() and not line.startswith("#")
    }

    try:
        differences, errors = run_scenario(
            binary, scenario, scenarios[scenario], data
        )
    except Exception as error:
        differences, errors = [], [f"adapter/vttest error: {error}"]

    if errors:
        print(f"ERROR Contour/{scenario}", file=sys.stderr)
        print("\n".join(errors), file=sys.stderr)
        return 1
    if scenario in known_failures:
        if not differences:
            print(f"XPASS Contour/{scenario}", file=sys.stderr)
            return 1
        print(f"XFAIL Contour/{scenario}")
    elif differences:
        print(f"FAIL Contour/{scenario}", file=sys.stderr)
        print("\n".join(differences[:5]), file=sys.stderr)
        if len(differences) > 5:
            print(
                f"... {len(differences) - 5} differences omitted",
                file=sys.stderr,
            )
        return 1
    else:
        print(f"PASS Contour/{scenario}")

    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.touch()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
