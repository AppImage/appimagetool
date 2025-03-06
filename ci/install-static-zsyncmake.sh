#! /bin/bash

set -euxo pipefail

if [[ "${2:-}" == "" ]]; then
    echo "Usage: $0 <version> <hash>"
    exit 2
fi

version="$1"
hash="$2"

build_dir="$(mktemp -d -t zsyncmake-build-XXXXXX)"

cleanup () {
    if [ -d "$build_dir" ]; then
        rm -rf "$build_dir"
    fi
}
trap cleanup EXIT

pushd "$build_dir"

# the original zsync homepage has been shut down apparently, but we can fetch the source code from most distros
wget http://deb.debian.org/debian/pool/main/z/zsync/zsync_"$version".orig.tar.bz2
echo "${hash}  zsync_${version}.orig.tar.bz2" | sha256sum -c

tar xf zsync_"$version"*.tar.bz2

cd zsync-*/

# custom patch to at least ignore flags passed after the first parameter
# see https://github.com/AppImage/appimagetool/pull/93 for more information
cat > argparser.patch <<\EOF
diff --git a/make.c b/make.c
index 191b527..d86f130 100644
--- a/make.c
+++ b/make.c
@@ -564,6 +564,14 @@ off_t get_len(FILE * f) {
  * Main program
  */
 int main(int argc, char **argv) {
+    if (getenv("VERBOSE") != 0) {
+        fprintf(stderr, "Parameters:");
+        for (int i = 0; i < argc; ++i) {
+            fprintf(stderr, " %s", argv[i]);
+        }
+        fprintf(stderr, "\n\n");
+    }
+
     FILE *instream;
     char *fname = NULL, *zfname = NULL;
     char **url = NULL;
@@ -641,7 +649,7 @@ int main(int argc, char **argv) {
         }
 
         /* Open data to create .zsync for - either it's a supplied filename, or stdin */
-        if (optind == argc - 1) {
+        if (optind <= argc - 1) {
             infname = strdup(argv[optind]);
             instream = fopen(infname, "rb");
             if (!instream) {
EOF

for i in *.patch; do
    echo "Applying patch $i..."
    patch -p1 < "$i"
done

find . -type f -exec sed -i -e 's|off_t|size_t|g' {} \;

./configure LDFLAGS=-static --prefix=/usr --build=$(arch)-unknown-linux-gnu

if [[ "${GITHUB_ACTIONS:-}" != "" ]]; then
    jobs="$(nproc)"
else
    jobs="$(nproc --ignore=1)"
fi

make -j"$jobs" install
