#!/bin/sh

set -eu

project_root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
profile=${SHITTY_HEAP_PROFILE:-"$project_root/.build/heap.prof"}
report=${SHITTY_HEAP_REPORT:-"$project_root/.build/heap.txt"}

cd "$project_root"
"$HOME/monorepo/ix/ix" run bld/perl set/pg/libs lib/simd/utf lib/gperftools/profile -- ./build st_memprofile
SHITTY_HEAP_PROFILE="$profile" ./.build/st_memprofile "$@"
"$HOME/monorepo/ix/ix" run bin/go/lang -- go tool pprof -top -cum -lines -nodefraction=0 -nodecount=100 -sample_index=inuse_space ./.build/st_memprofile "$profile" > "$report"
cat "$report"
