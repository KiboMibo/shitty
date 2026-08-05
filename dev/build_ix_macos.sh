#!/bin/sh
# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

exec "$HOME/monorepo/ix/ix" run bin/ragel/6 bin/glslang bin/spirv/cross lib/utf8/proc --target=arm64 -- ./build --target=aarch64-apple-darwin -B .build-darwin st pt
