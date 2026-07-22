#!/bin/sh
exec "$HOME/monorepo/ix/ix" run bld/perl set/pg/libs -- ./build st "$@"
