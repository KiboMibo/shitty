#!/usr/bin/env python3
# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

"""Run upstream vttest through Shitty's real PTY and headless screen."""

import argparse
import os
import shutil
import time

from harness import Shitty


def run(binary, rounds, log_path=None):
    with Shitty(columns=80, rows=24, save_lines=2000) as terminal:
        command = [binary, "-q", "-u"]
        if log_path:
            command.extend(("-l", log_path))
        terminal.spawn(*command)
        started = False
        left_main_menu = False
        saw_completion = False
        answered_screen = None
        answered_round = -25
        for round_number in range(rounds):
            status, screen = terminal.poll_child()
            screen_lower = screen.lower()
            main_menu = "Choose test type:" in screen
            saw_completion |= "That's all, folks!" in screen
            answer = None
            if main_menu and not started:
                answer = b"*\n"
                started = True
            elif started:
                if not main_menu:
                    left_main_menu = True
                if main_menu and left_main_menu:
                    answer = b"0\n"
                elif "Enter choice number" in screen or "0. Exit" in screen:
                    answer = b"0\n"
                elif (
                    "push <return>" in screen_lower
                    or "press return to continue" in screen_lower
                ):
                    answer = b"\n"
            if answer is not None and (
                screen != answered_screen or
                round_number - answered_round >= 25
            ):
                terminal.input(answer)
                answered_screen = screen
                answered_round = round_number
            elif screen != answered_screen:
                answered_screen = None
            if status is not None:
                if status != 0:
                    raise RuntimeError(f"vttest exited with status {status}")
                if not saw_completion:
                    raise RuntimeError("vttest exited without completing its suite")
                return
            # poll_child() is non-blocking. Yield to the freshly spawned
            # vttest process instead of exhausting the round budget before it
            # has had a chance to produce its first menu.
            time.sleep(0.005)
        raise RuntimeError(
            "vttest did not complete before the round limit:\n" +
            terminal.screen_text()
        )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", default=os.environ.get("VTTEST_BINARY", "vttest"))
    parser.add_argument("--rounds", type=int, default=3000)
    parser.add_argument("--log")
    arguments = parser.parse_args()
    binary = shutil.which(arguments.binary)
    if binary is None:
        print("SKIP: upstream vttest is not installed")
        return
    run(binary, arguments.rounds, arguments.log)


if __name__ == "__main__":
    main()
