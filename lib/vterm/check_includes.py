# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

"""The lib/vterm boundary: the VT core includes only its own headers,
plt, libstd and system headers. Any mention of lib/shitty - by full
path or by a quoted include that only resolves there - is a violation.
The whole point of the split is that the core cannot see the GUI, the
fonts or the renderers; this check is the guarantee."""

import re
import sys
from pathlib import Path


# Headers generated into the build directory; a quoted include cannot
# resolve inside lib/vterm, and they are ours, not lib/shitty's.
GENERATED = {
    "parser.rl.h",
    "utf8_dfa.h",
    "input_keys.h",
    "unicode_data.h",
}

INCLUDE = re.compile(r'^\s*#\s*include\s+(<[^>]+>|"[^"]+")')


# The crossings the boundary carries today, and only those. Until G15 this
# check was all or nothing, so the six crossings it then held - which no task
# in wave 6 was allowed to close - held the whole node red. The node is inside
# add_test(), so `./build test` stopped here and ran no test at all: Tests
# Alpine and Tests Fedora were blind from eb016aec (M6b) through four merges,
# on run 33862433804 and the three before it. A guard that cannot be told
# "this one, this many, and here is why" is a guard that gets deleted or,
# worse, left red until nobody reads it.
#
# Empty, and that is the whole of the news. It held six crossings until the
# window's insets, the in-band resize and the pane list's cell count started
# reaching the core through VtHost (contentInsets, surfaceResized,
# cellCapacityExcept): vterm.cpp lost composer.h, session.h and
# grid_geometry.h with them, and vt_headless.cpp - an embedder's adapter that
# had been standing on the lib/shitty side of the boundary while living in
# lib/vterm - moved to lib/shitty, which is where its other three went.
#
# Kept as an empty dict rather than deleted, because the shape is the
# guarantee and the next crossing will want to be written down here rather
# than argued about. What made it an allowance and not a pardon still holds
# and is worth keeping written:
#
# 1. It is keyed by file *and* by the include, not by a count per file. The
#    four guards in build.py can only key on a path, because what they meter
#    is a call and a call has no name of its own; G1 wrote that weakness down
#    (invariants 3.4, point 1): "composer.cpp: 8" means any eight, so removing
#    a legitimate call and adding an illegitimate one keeps the count and the
#    green. Here the subject *does* have a name - the include - so the key
#    carries it, and substitution is caught, not just growth.
#
# 2. The number is a count, and it meters the file it names. A second
#    #include of an already-allowed header is one more than the key says, and
#    one more is red. Duplicate includes are how a header arrives twice under
#    two spellings and how a crossing survives the file that legitimised it.
#
# 3. Every key says what would close it. A key with no way out is a key that
#    outlives its reason, and the next reader cannot tell an allowance from a
#    surrender.
#
# What is deliberately *not* red: an include listed here with no hits left. A
# count may fall to zero with the key kept, which is what T5.8 did to
# mouse_geometry_allowance - zero is the tightest number, and dropping the key
# with it would also drop the stale-file check that proves the guard can still
# see where the form would come back. The stale check below is over files, so
# a zeroed include keeps its file reachable and its guarantee alive.
ALLOWANCE = {}


def check(root):
    sources = sorted(
        path
        for pattern in ("*.h", "*.cpp")
        for path in root.glob(pattern)
    )
    local = {path.name for path in sources}
    seen = set()
    counted = {}
    violations = []
    for path in sources:
        seen.add(path.name)
        allowed = ALLOWANCE.get(path.name, {})
        for number, line in enumerate(path.read_text().splitlines(), 1):
            match = INCLUDE.match(line)
            if match is None:
                continue
            spec = match.group(1)
            name = spec[1:-1]
            where = f"{path.name}:{number}"
            if "lib/shitty" in name:
                complaint = f"{where}: {spec} crosses into lib/shitty"
            elif spec.startswith('"'):
                if name in local or name in GENERATED:
                    continue
                complaint = f"{where}: {spec} does not resolve inside lib/vterm"
            elif name.startswith("lib/") and not name.startswith("lib/vterm/"):
                complaint = f"{where}: {spec} reaches outside the core"
            else:
                continue
            # The count is spent in file order, so the allowed crossing is the
            # first one and every repeat of it is reported.
            counted[path.name, name] = counted.get((path.name, name), 0) + 1
            if counted[path.name, name] > allowed.get(name, 0):
                violations.append(complaint)
    return violations, sorted(set(ALLOWANCE) - seen)


def main():
    root = Path(sys.argv[1])
    stamp = Path(sys.argv[2])
    violations, stale = check(root)
    if violations:
        print(
            "the VT core cannot see the GUI: these crossings are not in the "
            "allowance at the top of this file, and closing one is cheaper "
            "than widening it.",
            file=sys.stderr,
        )
        for violation in violations:
            print(f"  {violation}", file=sys.stderr)
        return 1
    if stale:
        # The trap T2.1 built for the four guards in build.py, and the reason
        # it exists: a key naming a file the scan no longer reaches is a
        # pardon nothing can spend, so the guard would go green over a file it
        # has stopped reading. It caught an orphaned key on M6b and went red
        # from both sides at once on M6e.
        print(
            "the boundary allowance names files this check never read, so it "
            "is guarding a core that no longer exists: re-key the allowance "
            "onto where these live now, or drop them.",
            file=sys.stderr,
        )
        for name in stale:
            print(f"  {name}", file=sys.stderr)
        return 1
    stamp.write_text("ok\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
