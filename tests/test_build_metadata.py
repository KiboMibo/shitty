# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

import importlib.util
import subprocess
import sys
import tempfile
import unittest
from importlib.machinery import SourceFileLoader
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class BuildMetadataTests(unittest.TestCase):
    def run_build(self, build_file, *arguments):
        return subprocess.run(
            [
                sys.executable,
                ROOT / "build",
                "--build-file",
                build_file,
                *arguments,
            ],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )

    def run_build_with_descr(self, descr):
        with tempfile.TemporaryDirectory() as directory:
            build_file = Path(directory) / "build.py"
            build_file.write_text(
                "target = command(\n"
                "    outputs=['$(B)/result'],\n"
                "    cmd=['true'],\n"
                f"    descr={descr!r},\n"
                ")\n"
                "install(target)\n"
            )
            return self.run_build(build_file, "--list")

    def test_build_accepts_exactly_two_ascii_letters_in_descr(self):
        result = self.run_build_with_descr("OK")

        self.assertEqual(result.returncode, 0, result.stderr)

    def test_build_rejects_every_other_descr_shape(self):
        for descr in ("A", "ABC", "A1", "A ", "ÄB"):
            with self.subTest(descr=descr):
                result = self.run_build_with_descr(descr)

                self.assertNotEqual(result.returncode, 0)
                self.assertIn(
                    "descr must be exactly two ASCII letters",
                    result.stderr,
                )

    def test_groups_are_additive_cli_aliases(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            build_file = root / "build.py"
            build_file.write_text(
                "one = command(\n"
                "    outputs=['$(B)/one'],\n"
                "    cmd=['python3', '-c', "
                "\"from pathlib import Path; Path(r'$(B)/one').touch()\"],\n"
                ")\n"
                "two = command(\n"
                "    outputs=['$(B)/two'],\n"
                "    cmd=['python3', '-c', "
                "\"from pathlib import Path; Path(r'$(B)/two').touch()\"],\n"
                ")\n"
                "group('batch', one)\n"
                "group('batch', two)\n"
                "group('install', one)\n"
            )

            listed = self.run_build(build_file, "--list")
            self.assertEqual(listed.returncode, 0, listed.stderr)
            self.assertEqual(
                listed.stdout.splitlines(),
                ["batch", "install", "one", "two"],
            )

            result = self.run_build(build_file, "-B", ".out", "batch")
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertTrue((root / ".out" / "one").exists())
            self.assertTrue((root / ".out" / "two").exists())
            self.assertFalse((root / "one").exists())
            self.assertFalse((root / "two").exists())

    def test_install_group_is_the_default(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            build_file = root / "build.py"
            build_file.write_text(
                "target = command(\n"
                "    outputs=['$(B)/result'],\n"
                "    cmd=['python3', '-c', "
                "\"from pathlib import Path; Path(r'$(B)/result').touch()\"],\n"
                ")\n"
                "group('install', target)\n"
            )

            result = self.run_build(build_file, "-B", ".out")
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertTrue((root / ".out" / "result").exists())

    def test_group_name_cannot_conflict_with_target_name(self):
        with tempfile.TemporaryDirectory() as directory:
            build_file = Path(directory) / "build.py"
            build_file.write_text(
                "same = command(\n"
                "    outputs=['$(B)/result'],\n"
                "    cmd=['/usr/bin/touch', '$(B)/result'],\n"
                ")\n"
                "group('same', same)\n"
            )

            result = self.run_build(build_file, "--list")
            self.assertNotEqual(result.returncode, 0)
            self.assertIn(
                "group name conflicts with target name: same",
                result.stderr,
            )

    def test_libstd_is_bundled_as_source(self):
        self.assertFalse((ROOT / ".gitmodules").exists())
        self.assertTrue((ROOT / "third_party/libstd/build.py").is_file())
        self.assertTrue((ROOT / "third_party/libstd/std/lib/buffer.cpp").is_file())

    def test_readme_builds_without_submodule_setup(self):
        readme = (ROOT / "README.md").read_text()
        self.assertNotIn("git submodule", readme)
        self.assertIn("third_party/libstd", readme)

    def test_unit_test_suites_have_twenty_shard_nodes(self):
        result = self.run_build(ROOT / "build.py", "--list")
        self.assertEqual(result.returncode, 0, result.stderr)

        targets = result.stdout.splitlines()
        self.assertIn("test_suite", targets)
        self.assertIn("test_suite_prod_parser", targets)
        for prefix in (
            "unit_tests_group_",
            "test_suite_group_",
            "test_suite_prod_parser_group_",
        ):
            with self.subTest(prefix=prefix):
                self.assertEqual(
                    [target for target in targets if target.startswith(prefix)],
                    [f"{prefix}{group:02}" for group in range(20)],
                )

    def test_test_partitions_are_deterministic_complete_and_disjoint(self):
        loader = SourceFileLoader("shitty_build_runner", str(ROOT / "build"))
        spec = importlib.util.spec_from_loader(loader.name, loader)
        self.assertIsNotNone(spec)
        runner = importlib.util.module_from_spec(spec)
        sys.modules[loader.name] = runner
        loader.exec_module(runner)

        with tempfile.TemporaryDirectory() as directory:
            def test_ids(values, suffix):
                context = runner.BuildContext(
                    ROOT,
                    Path(directory) / suffix,
                    runner.Flags(values),
                )
                context.load(ROOT / "build.py")
                return {
                    target.name or target.output or "\0".join(target.outputs)
                    for target in context.groups["test"]
                }

            full = test_ids({}, "full")
            partitions = [
                test_ids(
                    {"group": str(group), "group_count": "5"},
                    f"group-{group}",
                )
                for group in range(5)
            ]

            self.assertEqual(set().union(*partitions), full)
            self.assertEqual(sum(map(len, partitions)), len(full))
            self.assertEqual(
                test_ids({"group": "2", "group_count": "5"}, "repeat"),
                partitions[2],
            )


if __name__ == "__main__":
    unittest.main()
