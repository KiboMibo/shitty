#!/usr/bin/env bash

set -eu

data="$HOME/shitty-20m.perf.data"
report="$HOME/shitty-20m.perf.txt"
binary="$(dirname "$0")/.build/st"

perf record \
    -e cycles:u \
    -F 999 \
    --call-graph dwarf,16384 \
    -o "$data" \
    -- "$binary" -e /bin/sh -c 'head -c 20971520 /dev/random'

perf report \
    -i "$data" \
    --stdio \
    --comms st \
    --no-children \
    --percent-limit 0.2 \
    -g graph,0.5,caller \
    > "$report"

printf '%s\n' "$data" "$report"
