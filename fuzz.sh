#!/bin/sh

set -eu

env -u CPPFLAGS -u CFLAGS -u CXXFLAGS -u LDFLAGS -u CTRFLAGS -u BUILD_EXTRA_CFLAGS -u CC -u CXX -u AR -u PKG_CONFIG_PATH \
    nix-shell --run 'CFLAGS="-fsanitize=address,fuzzer-no-link -fno-omit-frame-pointer -g" LDFLAGS="-fsanitize=address,fuzzer" ./build main_fuzz'

exec ./main_fuzz "$@"
