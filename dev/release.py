#!/usr/bin/env python3
# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

import argparse
import gzip
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import tarfile
import tempfile
from pathlib import Path


def run(
    arguments: list[str],
    *,
    cwd: Path | None = None,
    capture: bool = False,
) -> str:
    location = f" (in {cwd})" if cwd is not None else ""
    print(f"+ {shlex.join(arguments)}{location}", file=sys.stderr)
    result = subprocess.run(
        arguments,
        cwd=cwd,
        check=True,
        stdout=subprocess.PIPE if capture else None,
        text=True,
    )
    return result.stdout.strip() if capture else ""


def verify_tools() -> None:
    for tool in ("git", "gh", "file"):
        if not shutil.which(tool):
            raise RuntimeError(f"required tool is not available: {tool}")


def release_tags(repository: str) -> list[int]:
    releases = json.loads(run(
        [
            "gh",
            "release",
            "list",
            "--repo",
            repository,
            "--limit",
            "1000",
            "--json",
            "tagName",
        ],
        capture=True,
    ))
    return [
        int(release["tagName"])
        for release in releases
        if re.fullmatch(r"[1-9][0-9]*", release["tagName"])
    ]


def release_exists(repository: str, tag: str) -> bool:
    return subprocess.run(
        ["gh", "release", "view", tag, "--repo", repository],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    ).returncode == 0


def remote_refs(remote: str, tag: str) -> dict[str, str]:
    output = run(
        [
            "git",
            "ls-remote",
            remote,
            f"refs/heads/{tag}",
            f"refs/tags/{tag}",
            f"refs/tags/{tag}^{{}}",
        ],
        capture=True,
    )
    refs = {}
    for line in output.splitlines():
        sha, name = line.split()
        refs[name] = sha
    return refs


def verify_remote_refs(refs: dict[str, str], tag: str, sha: str) -> tuple[bool, bool]:
    branch_name = f"refs/heads/{tag}"
    tag_name = f"refs/tags/{tag}"
    peeled_tag_name = f"{tag_name}^{{}}"
    branch_exists = branch_name in refs
    tag_exists = tag_name in refs
    if branch_exists and refs[branch_name] != sha:
        raise RuntimeError(f"remote branch {tag} points to {refs[branch_name]}, not {sha}")
    tag_commit = refs.get(peeled_tag_name, refs.get(tag_name))
    if tag_commit is not None and tag_commit != sha:
        raise RuntimeError(f"remote tag {tag} points to {tag_commit}, not {sha}")
    return branch_exists, tag_exists


def tar_info(
    archive: tarfile.TarFile,
    path: Path,
    archive_name: str,
    timestamp: int,
) -> tarfile.TarInfo:
    info = archive.gettarinfo(path, archive_name)
    info.uid = 0
    info.gid = 0
    info.uname = ""
    info.gname = ""
    info.mtime = timestamp
    return info


def write_tar(
    output: Path,
    timestamp: int,
    write_contents,
) -> None:
    with output.open("wb") as compressed_file:
        with gzip.GzipFile(
            filename="",
            mode="wb",
            fileobj=compressed_file,
            compresslevel=9,
            mtime=timestamp,
        ) as gzip_file:
            with tarfile.open(
                fileobj=gzip_file,
                mode="w",
                format=tarfile.GNU_FORMAT,
            ) as archive:
                write_contents(archive)


def create_source_archive(
    checkout: Path,
    output: Path,
    prefix: str,
    timestamp: int,
) -> None:
    tracked = subprocess.check_output(
        ["git", "ls-files", "-z"],
        cwd=checkout,
    ).split(b"\0")
    paths = sorted(
        Path(os.fsdecode(name))
        for name in tracked
        if name
    )

    def write_contents(archive: tarfile.TarFile) -> None:
        root = tarfile.TarInfo(f"{prefix}/")
        root.type = tarfile.DIRTYPE
        root.mode = 0o755
        root.uid = 0
        root.gid = 0
        root.mtime = timestamp
        archive.addfile(root)
        for relative in paths:
            if relative.is_absolute() or ".." in relative.parts:
                raise RuntimeError(f"unsafe tracked path: {relative}")
            source = checkout / relative
            info = tar_info(
                archive,
                source,
                f"{prefix}/{relative.as_posix()}",
                timestamp,
            )
            if info.isreg():
                with source.open("rb") as input_file:
                    archive.addfile(info, input_file)
            else:
                archive.addfile(info)

    write_tar(output, timestamp, write_contents)


def create_binary_archive(
    binary: Path,
    binary_name: str,
    output: Path,
    timestamp: int,
) -> None:
    def write_contents(archive: tarfile.TarFile) -> None:
        info = tar_info(archive, binary, binary_name, timestamp)
        info.mode = 0o755
        with binary.open("rb") as input_file:
            archive.addfile(info, input_file)

    write_tar(output, timestamp, write_contents)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Build and publish a Shitty GitHub release.",
    )
    parser.add_argument("tag", nargs="?", help="numeric release tag; omitted picks the next")
    parser.add_argument("--sha", default="HEAD", help="git commit to release")
    parser.add_argument(
        "--builder",
        choices=("ix", "brew"),
        default="ix",
        help="darwin build environment: the local ix toolchain or the CI brew one",
    )
    parser.add_argument(
        "--generate-notes",
        action="store_true",
        help="let GitHub generate the release notes instead of reading stdin",
    )
    parser.add_argument(
        "--artifacts-directory",
        type=Path,
        help="directory in which to retain the generated release artifacts",
    )
    parser.add_argument(
        "--draft",
        action="store_true",
        help="create a draft release instead of publishing it",
    )
    parser.add_argument(
        "--tag-file",
        type=Path,
        help="write the resolved tag to this file after creating the release",
    )
    arguments = parser.parse_args()

    if arguments.tag is not None and not re.fullmatch(r"[1-9][0-9]*", arguments.tag):
        parser.error("tag must be a positive decimal integer without leading zeroes")

    verify_tools()
    notes = "" if arguments.generate_notes else sys.stdin.read().strip()
    if not notes and not arguments.generate_notes:
        parser.error("release notes must be provided on stdin")

    project_root = Path(__file__).resolve().parent.parent
    remote = run(
        ["git", "remote", "get-url", "origin"],
        cwd=project_root,
        capture=True,
    )
    repository = run(
        ["gh", "repo", "view", "--json", "nameWithOwner", "--jq", ".nameWithOwner"],
        cwd=project_root,
        capture=True,
    )
    local_sha = run(
        ["git", "rev-parse", "--verify", f"{arguments.sha}^{{commit}}"],
        cwd=project_root,
        capture=True,
    )

    numeric_tags = release_tags(repository)
    expected_tag = max(numeric_tags, default=0) + 1
    if arguments.tag is None:
        arguments.tag = str(expected_tag)
    if release_exists(repository, arguments.tag):
        raise RuntimeError(f"release {arguments.tag} already exists")
    if int(arguments.tag) != expected_tag:
        raise RuntimeError(f"next release tag is {expected_tag}, not {arguments.tag}")

    with tempfile.TemporaryDirectory(prefix=f"shitty-release-{arguments.tag}-") as temporary_name:
        temporary = Path(temporary_name)
        checkout = temporary / "checkout"
        if arguments.artifacts_directory is None:
            artifacts = temporary / "artifacts"
        else:
            artifacts = arguments.artifacts_directory.resolve()
        artifacts.mkdir(parents=True)

        run(["git", "clone", "--no-checkout", remote, os.fspath(checkout)])
        resolved_sha = run(
            ["git", "rev-parse", "--verify", f"{local_sha}^{{commit}}"],
            cwd=checkout,
            capture=True,
        )
        run(["git", "checkout", "--detach", resolved_sha], cwd=checkout)
        timestamp = int(run(
            ["git", "show", "-s", "--format=%ct", resolved_sha],
            cwd=checkout,
            capture=True,
        ))
        source_archive = artifacts / f"{arguments.tag}.tar.gz"
        shitty_binary_archive = artifacts / "st-darwin-arm64.tar.gz"
        pretty_binary_archive = artifacts / "pt-darwin-arm64.tar.gz"
        notes_file = artifacts / "release-notes.md"
        if not arguments.generate_notes:
            notes_file.write_text(f"{notes}\n")

        create_source_archive(
            checkout,
            source_archive,
            f"shitty-{arguments.tag}",
            timestamp,
        )
        builder = "build_ix_macos.sh" if arguments.builder == "ix" else "build_brew_macos.sh"
        run([os.fspath(checkout / "dev" / builder)], cwd=checkout)
        for binary_name, binary_archive in (
            ("st", shitty_binary_archive),
            ("pt", pretty_binary_archive),
        ):
            binary = checkout / ".build-darwin" / binary_name
            if not binary.is_file():
                raise RuntimeError(f"Darwin build did not produce {binary}")
            binary = binary.resolve(strict=True)
            file_description = run(["file", os.fspath(binary)], capture=True)
            if not all(marker in file_description for marker in ("Mach-O 64-bit", "arm64", "executable")):
                raise RuntimeError(f"unexpected Darwin artifact: {file_description}")
            create_binary_archive(binary, binary_name, binary_archive, timestamp)

        refs = remote_refs(remote, arguments.tag)
        branch_exists, tag_exists = verify_remote_refs(
            refs,
            arguments.tag,
            resolved_sha,
        )
        if not tag_exists:
            run(
                [
                    "git",
                    "tag",
                    "-a",
                    arguments.tag,
                    resolved_sha,
                    "-m",
                    f"Release {arguments.tag}",
                ],
                cwd=checkout,
            )
        refspecs = []
        if not branch_exists:
            refspecs.append(f"{resolved_sha}:refs/heads/{arguments.tag}")
        if not tag_exists:
            refspecs.append(f"refs/tags/{arguments.tag}:refs/tags/{arguments.tag}")
        if refspecs:
            run(["git", "push", "--atomic", "origin", *refspecs], cwd=checkout)

        run(
            [
                "gh",
                "release",
                "create",
                arguments.tag,
                os.fspath(source_archive),
                os.fspath(shitty_binary_archive),
                os.fspath(pretty_binary_archive),
                "--repo",
                repository,
                "--verify-tag",
                "--title",
                arguments.tag,
                *(
                    ["--generate-notes"]
                    if arguments.generate_notes
                    else ["--notes-file", os.fspath(notes_file)]
                ),
                *(["--draft"] if arguments.draft else []),
            ],
            cwd=checkout,
        )
        if arguments.tag_file is not None:
            arguments.tag_file.write_text(f"{arguments.tag}\n")
        print(run(
            [
                "gh",
                "release",
                "view",
                arguments.tag,
                "--repo",
                repository,
                "--json",
                "url",
                "--jq",
                ".url",
            ],
            capture=True,
        ))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
