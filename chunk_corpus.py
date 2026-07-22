#!/usr/bin/env python3

"""Split source directories into MD5-addressed chunks.

Usage:
    chunk_corpus.py CHUNK_SIZE SOURCE [SOURCE ...] DESTINATION
"""

import hashlib
import os
import sys
import tempfile
from pathlib import Path


def usage() -> str:
    return f"usage: {Path(sys.argv[0]).name} CHUNK_SIZE SOURCE [SOURCE ...] DESTINATION"


def same_contents(path: Path, data: bytes) -> bool:
    if path.stat().st_size != len(data):
        return False
    with path.open("rb") as existing:
        return existing.read() == data


def store_chunk(destination: Path, digest: str, data: bytes) -> bool:
    """Store data under digest, returning true only when it was newly written."""
    target = destination / digest
    if target.exists():
        if not same_contents(target, data):
            raise RuntimeError(f"existing chunk has different contents: {target}")
        return False

    fd, temporary_name = tempfile.mkstemp(prefix=".chunk-", dir=destination)
    temporary = Path(temporary_name)
    try:
        with os.fdopen(fd, "wb") as output:
            output.write(data)
        try:
            # link(2) publishes a fully-written chunk without overwriting an
            # already-published chunk from another invocation.
            os.link(temporary, target)
        except FileExistsError:
            if not same_contents(target, data):
                raise RuntimeError(f"existing chunk has different contents: {target}")
            return False
        return True
    finally:
        temporary.unlink(missing_ok=True)


def main() -> int:
    if len(sys.argv) < 4:
        print(usage(), file=sys.stderr)
        return 2

    try:
        chunk_size = int(sys.argv[1])
    except ValueError:
        print(f"invalid chunk size: {sys.argv[1]!r}", file=sys.stderr)
        return 2
    if chunk_size <= 0:
        print("chunk size must be positive", file=sys.stderr)
        return 2

    sources = [Path(argument).resolve() for argument in sys.argv[2:-1]]
    destination = Path(sys.argv[-1]).resolve()
    if not sources:
        print(usage(), file=sys.stderr)
        return 2
    for source in sources:
        if not source.is_dir():
            print(f"source is not a directory: {source}", file=sys.stderr)
            return 2
        try:
            destination.relative_to(source)
        except ValueError:
            pass
        else:
            print(
                f"destination must not be inside source directory: {destination}",
                file=sys.stderr,
            )
            return 2

    destination.mkdir(parents=True, exist_ok=True)
    files = chunks = written = reused = 0
    for source in sources:
        for directory, _, names in os.walk(source):
            for name in names:
                path = Path(directory, name)
                if not path.is_file():
                    continue
                files += 1
                with path.open("rb") as input_file:
                    while data := input_file.read(chunk_size):
                        chunks += 1
                        digest = hashlib.md5(data).hexdigest()
                        if store_chunk(destination, digest, data):
                            written += 1
                        else:
                            reused += 1

    print(
        f"files={files} chunks={chunks} written={written} reused={reused}",
        file=sys.stderr,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
