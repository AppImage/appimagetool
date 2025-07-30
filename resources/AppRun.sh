#! /bin/sh

set -e

this_dir="$(dirname -- "$(readlink -f -- "$0")")"

# make appimagetool prefer the bundled mksquashfs
export PATH="$this_dir"/usr/bin:"$PATH"

exec "$this_dir"/usr/bin/appimagetool "$@"
