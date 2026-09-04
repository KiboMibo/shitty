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
# check was all or nothing, so the six lines below - which no task in wave 6
# is allowed to close - held the whole node red. The node is inside
# add_test(), so `./build test` stopped here and ran no test at all: Tests
# Alpine and Tests Fedora were blind from eb016aec (M6b) through four merges,
# on run 33862433804 and the three before it. A guard that cannot be told
# "this one, this many, and here is why" is a guard that gets deleted or,
# worse, left red until nobody reads it.
#
# An allowance is not a pardon. Three things make it one and not the other.
#
# 1. It is keyed by file *and* by the include, not by a count per file. The
#    four guards in build.py can only key on a path, because what they meter
#    is a call and a call has no name of its own; G1 wrote that weakness down
#    (invariants 3.4, point 1): "composer.cpp: 8" means any eight, so removing
#    a legitimate call and adding an illegitimate one keeps the count and the
#    green. Here the subject *does* have a name - the include - so the key
#    carries it, and substitution is caught, not just growth. Swapping
#    "composer.h" for "renderer.h" in vterm.cpp is a key nobody wrote down.
#
# 2. The number is a count, and it meters the file it names. A second
#    #include "composer.h" in vterm.cpp is one more than the one below, and
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
ALLOWANCE = {
    # vt_headless.cpp is ours and stays ours until T5.9 replaces it with
    # upstream's vt_headless.* wholesale. It cannot be taken today: our
    # Vterm::create takes eleven parameters against upstream's nine, we have
    # no windowResized(), and returning to the upstream signature breaks st's
    # entry point (A1/A8; T5.1 decision 7, "4 lines go with T5.9").
    "vt_headless.cpp": {
        "composer.h": 1,
        "pane_layout.h": 1,
        "grid_geometry.h": 1,
    },
    "vterm.cpp": {
        # The single call composer.sessions->cellCapacityExcept(this) (A11).
        # Closed by T5.4, which needs a new interface inside lib/vterm to
        # ask the question without naming the embedder's session store.
        "session.h": 1,
        # The file holds a Composer& as a member and reads contentInsets()
        # and resize() off it. contentInsets() goes with T5.1 (seven of
        # eleven uses), resize() with M6d (VtHost::requestResize), the last
        # of them with T5.4; the include goes when the last one does.
        "composer.h": 1,
        # The one edit that closes this - moving the four-sided arithmetic
        # into lib/vterm - is forbidden outright by T5.1 decision 2.7,
        # because it would carry Insets out of composer.h. Four executors
        # reached that conclusion independently and the refusal stands.
        "grid_geometry.h": 1,
    },
}


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
