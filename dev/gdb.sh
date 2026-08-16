#!/usr/bin/env bash
# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "usage: $0 PID" >&2
    exit 2
fi

pid=$1
output="gdb-${pid}.txt"
gdb=${GDB:-gdb}

"$gdb" \
    -q \
    -batch \
    -ex "set pagination off" \
    -ex "set print thread-events off" \
    -ex "thread apply all bt full" \
    -ex "thread apply all disassemble /m \$pc-64,\$pc+64" \
    -p "$pid" 2>&1 | tee "$output"

echo "saved to $output"
