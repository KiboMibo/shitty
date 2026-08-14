#!/usr/bin/env python3
"""Build an Arch package sysroot used only by the glibc bridge experiment."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
from collections import deque
from pathlib import Path


MIRROR = "https://geo.mirror.pkgbuild.com"
REPOSITORIES = ("core", "extra")
DEFAULT_PACKAGES = ("vulkan-icd-loader", "vulkan-radeon")
VIRTUAL_PACKAGES = {
    "glibc",             # Supplied by glibc_shim.c, never mapped.
    "filesystem",        # Package-manager filesystem ownership only.
    "linux-api-headers", # Compile-time headers.
    "tzdata",            # Not needed for the Vulkan experiment.
}


def fields(path: Path) -> dict[str, list[str]]:
    result: dict[str, list[str]] = {}
    current: str | None = None
    for line in path.read_text().splitlines():
        if len(line) >= 3 and line.startswith("%") and line.endswith("%"):
            current = line[1:-1]
            result[current] = []
        elif current and line:
            result[current].append(line)
    return result


def dependency_name(value: str) -> str:
    return re.split(r"[<>=]", value, maxsplit=1)[0]


def run(*arguments: str) -> None:
    subprocess.run(arguments, check=True)


def ensure_databases(root: Path) -> None:
    database_root = root / "db"
    database_root.mkdir(parents=True, exist_ok=True)
    for repository in REPOSITORIES:
        archive = database_root / f"{repository}.db"
        extracted = database_root / repository
        if not archive.exists():
            run("curl", "--fail", "--location", "--output", str(archive),
                f"{MIRROR}/{repository}/os/x86_64/{repository}.db")
        if not extracted.exists():
            extracted.mkdir()
            run("bsdtar", "-xf", str(archive), "-C", str(extracted))


def package_index(root: Path):
    packages: dict[str, dict] = {}
    providers: dict[str, list[str]] = {}
    for repository in REPOSITORIES:
        for description in sorted((root / "db" / repository).glob("*/desc")):
            data = fields(description)
            name = data["NAME"][0]
            package = {
                "name": name,
                "repository": repository,
                "filename": data["FILENAME"][0],
                "version": data["VERSION"][0],
                "sha256": data["SHA256SUM"][0],
                "compressed_size": int(data["CSIZE"][0]),
                "installed_size": int(data["ISIZE"][0]),
                "depends": data.get("DEPENDS", []),
                "provides": data.get("PROVIDES", []),
            }
            packages[name] = package
            for provided in package["provides"]:
                providers.setdefault(provided, []).append(name)
                unversioned = dependency_name(provided)
                if unversioned != provided:
                    providers.setdefault(unversioned, []).append(name)
    return packages, providers


def resolve(requested: list[str], packages: dict[str, dict],
            providers: dict[str, list[str]]) -> list[dict]:
    queue = deque(requested)
    selected: dict[str, dict] = {}
    while queue:
        requirement = queue.popleft()
        dependency = dependency_name(requirement)
        if dependency in VIRTUAL_PACKAGES or dependency in selected:
            continue
        package = packages.get(dependency)
        if package is None:
            candidates = providers.get(requirement, providers.get(dependency, []))
            already_selected = [name for name in candidates if name in selected]
            if already_selected:
                continue
            if len(candidates) != 1:
                raise RuntimeError(
                    f"cannot choose provider for {dependency!r}: {candidates}")
            package = packages[candidates[0]]
        selected[package["name"]] = package
        queue.extend(package["depends"])
    return sorted(selected.values(), key=lambda package: package["name"])


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        while chunk := source.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def fetch_and_extract(root: Path, package: dict) -> None:
    archive = root / "pkgs" / package["filename"]
    if not archive.exists():
        run("curl", "--fail", "--location", "--output", str(archive),
            f"{MIRROR}/{package['repository']}/os/x86_64/{package['filename']}")
    actual_hash = sha256(archive)
    if actual_hash != package["sha256"]:
        raise RuntimeError(
            f"SHA-256 mismatch for {archive}: {actual_hash} != {package['sha256']}")
    run("bsdtar", "-xpf", str(archive), "-C", str(root / "root"))


def main() -> None:
    monorepo = Path(__file__).resolve().parents[3]
    parser = argparse.ArgumentParser()
    parser.add_argument("packages", nargs="*", default=DEFAULT_PACKAGES)
    parser.add_argument("--root", type=Path, default=monorepo / "arch")
    arguments = parser.parse_args()

    root = arguments.root.resolve()
    for directory in (root / "pkgs", root / "root"):
        directory.mkdir(parents=True, exist_ok=True)
    ensure_databases(root)
    packages, providers = package_index(root)
    closure = resolve(arguments.packages, packages, providers)

    compressed = sum(package["compressed_size"] for package in closure)
    installed = sum(package["installed_size"] for package in closure)
    print(f"Arch closure: {len(closure)} packages, "
          f"{compressed / 1024 / 1024:.1f} MiB compressed, "
          f"{installed / 1024 / 1024:.1f} MiB installed")
    for index, package in enumerate(closure, 1):
        print(f"[{index:02}/{len(closure):02}] {package['name']} {package['version']}")
        fetch_and_extract(root, package)

    manifest = {
        "mirror": MIRROR,
        "requested": arguments.packages,
        "virtual_packages": sorted(VIRTUAL_PACKAGES),
        "packages": closure,
    }
    (root / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")


if __name__ == "__main__":
    main()
