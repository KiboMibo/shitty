#!/bin/sh

set -eu

source_dir=$1
output=$2
output_dir=${output%/*}

mkdir -p "$output_dir"
work=$(mktemp -d "$output_dir/build.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

cp -R "$source_dir/." "$work/"
cd "$work"
./configure --enable-leaks >/dev/null
make >/dev/null
cp tack "$output"
