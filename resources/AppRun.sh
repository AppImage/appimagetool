#! /bin/sh

set -e

this_dir="$(readlink -f "$(dirname "$0")")"

# make appimagetool prefer the bundled mksquashfs
export PATH="$this_dir"/usr/bin:"$PATH"

exec "$this_dir"/usr/bin/appimagetool "$@"
