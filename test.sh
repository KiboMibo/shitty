#!/bin/sh
# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

exec "$HOME/monorepo/ix/ix" run bld/perl set/pg/libs -- ./build "$@"
