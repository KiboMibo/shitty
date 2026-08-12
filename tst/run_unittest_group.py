# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

"""Discover the Python test suite and run one deterministic shard."""

import argparse
import sys
import time
import unittest


class TimedTextTestResult(unittest.TextTestResult):
    def startTest(self, test):
        self.test_started = time.monotonic()
        super().startTest(test)

    def stopTest(self, test):
        elapsed = time.monotonic() - self.test_started
        if elapsed >= 1:
            self.stream.writeln(f"slow test: {elapsed:.3f}s {test.id()}")
        super().stopTest(test)


def iter_tests(suite):
    for test in suite:
        if isinstance(test, unittest.TestSuite):
            yield from iter_tests(test)
        else:
            yield test


def select_group(suite, group, group_count):
    if group_count <= 0:
        raise ValueError("group_count must be positive")
    if group < 0 or group >= group_count:
        raise ValueError("require 0 <= group < group_count")

    tests = list(iter_tests(suite))
    selected = unittest.TestSuite(
        test
        for test_index, test in enumerate(tests)
        if test_index % group_count == group
    )

    return selected, len(tests)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--group", type=int, required=True)
    parser.add_argument("--group-count", type=int, required=True)
    arguments = parser.parse_args()

    try:
        suite, total = select_group(
            unittest.defaultTestLoader.discover("tst"),
            arguments.group,
            arguments.group_count,
        )
    except ValueError as error:
        parser.error(str(error))

    selected = suite.countTestCases()
    print(
        f"Python tests group {arguments.group}/{arguments.group_count}: "
        f"{selected}/{total}",
        flush=True,
    )
    result = unittest.TextTestRunner(
        verbosity=2,
        resultclass=TimedTextTestResult,
    ).run(suite)

    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main())
