#!/bin/sh

set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: $0 PREFIX" >&2
    exit 2
fi

prefix=$1

install -Dm755 st "$prefix/bin/st"
install -Dm755 pt "$prefix/bin/pt"
install -Dm644 bin/st/shitty.desktop "$prefix/share/applications/shitty.desktop"
install -Dm644 bin/pt/pretty.desktop "$prefix/share/applications/pretty.desktop"
install -Dm644 bin/st/shitty.svg "$prefix/share/icons/hicolor/scalable/apps/shitty.svg"
install -Dm644 bin/pt/pretty.svg "$prefix/share/icons/hicolor/scalable/apps/pretty.svg"
