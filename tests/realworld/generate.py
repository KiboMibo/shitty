#!/usr/bin/env python3

import json
import os
import subprocess
import sys
import tempfile
import time
from compression import zstd
from pathlib import Path


TESTS = Path(__file__).resolve().parents[1]
ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(TESTS))

from harness import Shitty
from corpus import canonical_snapshot, encode_snapshot, verify_snapshot_contract
from scenarios import SCENARIOS


def make_fixture(root, with_git):
    (root / "empty").mkdir()
    (root / ".config" / "micro").mkdir(parents=True)
    (root / ".config" / "lazygit").mkdir(parents=True)
    (root / ".config" / "lazygit" / "config.yml").write_text(
        "disableStartupPopups: true\n"
    )
    (root / "src").mkdir()
    (root / "src" / "nested").mkdir()
    (root / ".hidden").write_text("hidden\n")
    (root / "sample.py").write_text(
        "#!/usr/bin/env python3\n"
        "def greet(name: str) -> str:\n"
        "    return f\"hello, {name}\"\n\n"
        "for value in (\"world\", \"λ\", \"界\"):\n"
        "    print(greet(value))\n"
    )
    (root / "data.json").write_text(
        '{\n  "enabled": true,\n  "count": 3,\n'
        '  "items": ["alpha", "βeta", "界"]\n}\n'
    )
    (root / "unicode.txt").write_text(
        "ASCII\nλ Ελληνικά\n界 日本語\né composed-ish\n👩🏽‍💻 emoji ZWJ\n"
    )
    (root / "words.txt").write_text("pear\napple\npear\nbanana\napple\npear\n")
    (root / ".nanorc").write_text(
        'syntax "python" "\\.py$"\n'
        'color brightblue "\\<(def|for|return|in)\\>"\n'
        'color brightgreen "\"[^\"]*\""\n'
        'color brightcyan "^#!.*"\n'
        'syntax "c" "\\.[ch]$"\n'
        'color brightcyan "^#[[:space:]]*(include|define).*"\n'
        'color brightblue "\\<(int|void|return)\\>"\n'
        'color brightgreen "\"[^\"]*\""\n'
    )
    (root / "long.txt").write_text("".join(
        f"line {number:03d} | The quick brown fox jumps over the lazy dog | "
        f"λ={number * 7}\n"
        for number in range(1, 121)
    ))
    (root / "long_lines.txt").write_text("".join(
        f"{number:03d}:" + "abcdefghijklmnopqrstuvwxyz" * 5 + "\n"
        for number in range(1, 31)
    ))
    (root / "table.tsv").write_text(
        "name\tcount\tnote\nalpha\t10\tfirst\nbeta\t20\tsecond\n界\t30\twide\n"
    )
    (root / "before.txt").write_text("alpha\nbeta\ngamma\ndelta\n")
    (root / "after.txt").write_text("alpha\nBETA\ngamma\nepsilon\n")
    (root / "ansi.txt").write_bytes(
        b"plain\n\x1b[1;31mred bold\x1b[0m\n"
        b"\x1b[4:3;38;2;20;180;240mcurly cyan\x1b[0m\n"
    )
    (root / "color_grid.py").write_text(
        "for row in range(6):\n"
        "    for column in range(12):\n"
        "        red = column * 21\n"
        "        green = row * 42\n"
        "        blue = 255 - column * 17\n"
        "        print(f'\\x1b[48;2;{red};{green};{blue}m  ', end='')\n"
        "    print('\\x1b[0m')\n"
    )
    (root / "color_items.py").write_text(
        "colors = [('red', 196), ('green', 46), ('blue', 21), ('orange', 208)]\n"
        "for name, color in colors:\n"
        "    print(f'\\x1b[38;5;{color}m{name} item\\x1b[0m')\n"
    )
    (root / "src" / "main.c").write_text(
        "#include <stdio.h>\nint main(void) { puts(\"hello\"); }\n"
    )
    (root / "src" / "nested" / "notes.md").write_text(
        "# Notes\n\n- one\n- two\n"
    )
    (root / "binary.dat").write_bytes(bytes(range(256)))
    (root / "name with spaces.txt").write_text("spaces\n")
    (root / "line\nbreak").write_text("newline in filename\n")
    os.symlink("sample.py", root / "sample-link")
    os.chmod(root / "sample.py", 0o755)
    if with_git:
        environment = os.environ.copy()
        environment.update({
            "GIT_AUTHOR_NAME": "Trace Author",
            "GIT_AUTHOR_EMAIL": "trace@example.invalid",
            "GIT_COMMITTER_NAME": "Trace Committer",
            "GIT_COMMITTER_EMAIL": "trace@example.invalid",
            "GIT_AUTHOR_DATE": "2020-01-02T03:04:05Z",
            "GIT_COMMITTER_DATE": "2020-01-02T03:04:05Z",
        })
        subprocess.run(["git", "init", "-q", "-b", "main", root], check=True, env=environment)
        subprocess.run(["git", "-C", root, "add", "."], check=True, env=environment)
        subprocess.run(["git", "-C", root, "commit", "-qm", "initial fixture"], check=True, env=environment)
        (root / "before.txt").write_text("alpha\nbeta changed\ngamma\ndelta\n")
        subprocess.run(["git", "-C", root, "add", "before.txt"], check=True, env=environment)
        environment["GIT_AUTHOR_DATE"] = "2020-02-03T04:05:06Z"
        environment["GIT_COMMITTER_DATE"] = "2020-02-03T04:05:06Z"
        subprocess.run(["git", "-C", root, "commit", "-qm", "update before"], check=True, env=environment)
        subprocess.run(["git", "-C", root, "tag", "-a", "v1.0", "-m", "fixture tag"], check=True, env=environment)


def drain(terminal, trace, require_exit, timeout=10, quiet=0.08, minimum=0):
    deadline = time.monotonic() + timeout
    last_output = time.monotonic()
    status = None
    while time.monotonic() < deadline:
        status, _screen = terminal.poll_child()
        output = terminal.read_child_output()
        if output:
            trace.extend(output)
            last_output = time.monotonic()
        if status is not None:
            output = terminal.read_child_output()
            trace.extend(output)
            return status
        if (not require_exit and len(trace) >= minimum
                and time.monotonic() - last_output >= quiet):
            return None
        time.sleep(0.005)
    raise TimeoutError("child did not reach capture checkpoint")


def case_metadata(scenario):
    return {
        "columns": scenario["columns"],
        "rows": scenario["rows"],
        "save_lines": 500,
        "terminal_environment": {
            "TERM": "xterm-256color",
            "COLORTERM": "truecolor",
            "LANG": "C.UTF-8",
        },
        "source_command": scenario["command"] or "bash --noprofile --norc -i",
        "checkpoint": scenario["checkpoint"],
        "expected_text": list(scenario.get("expected_text", ())),
        "minimum_color_styles": scenario.get("minimum_color_styles", 0),
    }


def capture(name, scenario):
    columns = scenario["columns"]
    rows = scenario["rows"]
    command = scenario["command"]
    actions = scenario["actions"]
    checkpoint = scenario["checkpoint"]
    minimum = scenario.get("minimum", 0)
    cleanup = scenario.get("cleanup")
    trace = bytearray()

    def send_action(terminal, action):
        if isinstance(action, bytes):
            terminal.input(action)
        elif (
            isinstance(action, tuple) and len(action) == 3
            and action[0] == "key"
        ):
            terminal.key(action[1], action[2])
        else:
            raise TypeError(f"unsupported capture action: {action!r}")

    with tempfile.TemporaryDirectory(prefix="shitty-realworld-") as directory:
        fixture = Path(directory)
        make_fixture(
            fixture, name.startswith(("git_", "tig_", "lazygit_")))
        (fixture / "tmp").mkdir()
        environment = [
            "env", "-i",
            f"HOME={fixture}",
            f"TMPDIR={fixture / 'tmp'}",
            f"PATH={os.environ['PATH']}",
            "LANG=C.UTF-8",
            "LC_ALL=C.UTF-8",
            "COLORTERM=truecolor",
            "TERM=xterm-256color",
        ]
        with Shitty(columns=columns, rows=rows, save_lines=500) as terminal:
            if command is not None:
                terminal.spawn(
                    *environment,
                    "bash", "--noprofile", "--norc", "-c",
                    'cd -- "$1" && test -t 0 && test -t 1 && test -t 2 '
                    '&& test "$TERM" = xterm-256color '
                    '&& test "$COLORTERM" = truecolor || exit 120; '
                    'exec bash --noprofile --norc -c "$2"',
                    "capture", str(fixture), command,
                )
                initial_exit = checkpoint == "exit" and not actions
                status = drain(
                    terminal, trace, initial_exit, minimum=minimum)
                if checkpoint == "quiet" and status is not None:
                    raise RuntimeError(
                        f"{name}: child exited before checkpoint with {status}")
                for index, action in enumerate(actions):
                    action_exit = (
                        checkpoint in ("exit", "action_exit")
                        and index + 1 == len(actions)
                    )
                    send_action(terminal, action)
                    status = drain(
                        terminal, trace, action_exit, minimum=minimum)
                    if checkpoint == "quiet" and status is not None:
                        raise RuntimeError(
                            f"{name}: child exited before checkpoint with {status}")
                if checkpoint in ("exit", "action_exit") and status != 0:
                    raise RuntimeError(f"{name}: child exited with {status}")
            else:
                terminal.spawn(
                    *environment,
                    "PS1=trace$ ", "PS2=more> ",
                    "bash", "--noprofile", "--norc", "-c",
                    'cd -- "$HOME" && test -t 0 && test -t 1 && test -t 2 '
                    '&& test "$TERM" = xterm-256color '
                    '&& test "$COLORTERM" = truecolor || exit 120; '
                    'exec bash --noprofile --norc -i',
                )
                drain(terminal, trace, False)
                for index, action in enumerate(actions):
                    send_action(terminal, action)
                    status = drain(
                        terminal, trace, index + 1 == len(actions))
                if status != 0:
                    raise RuntimeError(f"{name}: shell exited with {status}")
            snapshot = canonical_snapshot(
                terminal.model_snapshot(), terminal.render_state())
            verify_snapshot_contract(
                snapshot,
                scenario.get("expected_text", ()),
                scenario.get("minimum_color_styles", 0),
            )
            if cleanup is not None:
                if isinstance(cleanup, bytes):
                    terminal.input(cleanup)
                else:
                    cleanup_environment = os.environ.copy()
                    cleanup_environment["TMPDIR"] = str(fixture / "tmp")
                    subprocess.run(
                        cleanup, check=True, cwd=fixture,
                        env=cleanup_environment,
                        stdout=subprocess.DEVNULL,
                        stderr=subprocess.DEVNULL,
                    )
                drain(terminal, bytearray(), True, timeout=3)
    return bytes(trace), snapshot, case_metadata(scenario)
def main():
    manifest_only = sys.argv[1:] == ["--manifest-only"]
    selected = [] if manifest_only else (sys.argv[1:] or list(SCENARIOS))
    unknown = set(selected) - SCENARIOS.keys()
    if unknown:
        raise SystemExit(f"unknown scenarios: {', '.join(sorted(unknown))}")
    input_root = ROOT / "input"
    screen_root = ROOT / "screen"
    input_root.mkdir(parents=True, exist_ok=True)
    screen_root.mkdir(parents=True, exist_ok=True)
    manifest_path = ROOT / "cases.json"
    manifest = json.loads(manifest_path.read_text()) if manifest_path.exists() else {}
    if manifest_only:
        for path in input_root.glob("*.input.zst"):
            name = path.name.removesuffix(".input.zst")
            if name in SCENARIOS and (screen_root / f"{name}.screen.json").exists():
                manifest[name] = case_metadata(SCENARIOS[name])
    for name in selected:
        try:
            trace, snapshot, case = capture(name, SCENARIOS[name])
        except Exception as error:
            raise RuntimeError(f"capture failed for {name}: {error}") from error
        (input_root / f"{name}.input.zst").write_bytes(zstd.compress(trace, level=19))
        (screen_root / f"{name}.screen.json").write_text(encode_snapshot(snapshot))
        manifest[name] = case
        print(f"captured {name}: {len(trace)} bytes")
        manifest_path.write_text(
            json.dumps(manifest, indent=2, sort_keys=True) + "\n")
        (ROOT / "file_names.txt").write_text("".join(
            case_name + "\n" for case_name in SCENARIOS if case_name in manifest
        ))
    for name in tuple(manifest):
        manifest[name] = case_metadata(SCENARIOS[name])
    manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")
    (ROOT / "file_names.txt").write_text("".join(
        name + "\n" for name in SCENARIOS if name in manifest
    ))


if __name__ == "__main__":
    main()
