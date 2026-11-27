#!/usr/bin/env bash
# The Windows program as the installer carries it (packaging/windows/
# seventhslide.iss): a folder with seventhslide.exe, the server's console
# program, Qt (its plugins and QML modules, from windeployqt) and every DLL
# from MSYS2 that any of them loads (mpv, FFmpeg, OpenSSL, the compiler's
# runtime and what those load in turn), so it runs on a Windows without
# MSYS2. Qt WebEngine is not in MSYS2's Qt, so web pages show nothing here.
#
# Run from the MSYS2 UCRT64 shell after building with the windows-exe preset.
#
# Usage: packaging/windows/deploy.sh <build dir> <out dir>
set -euo pipefail

build=$1
out=$2
source_dir=$(cd "$(dirname "$0")/../.." && pwd)

rm -rf "$out"
mkdir -p "$out"
cp "$build/bin/seventhslide.exe" "$out/"
if [ -f "$build/bin/seventhslide-server.exe" ]; then
    cp "$build/bin/seventhslide-server.exe" "$out/"
fi
# The licence and NOTICE.md, with names Windows opens.
cp "$source_dir/LICENSE" "$out/LICENSE.txt"
cp "$source_dir/NOTICE.md" "$out/NOTICE.txt"

# MSYS2 names Qt 6's windeployqt for its version.
windeployqt=$(command -v windeployqt6 || command -v windeployqt-qt6 || command -v windeployqt || true)
if [ -z "$windeployqt" ]; then
    echo "deploy.sh: no windeployqt (install mingw-w64-ucrt-x86_64-qt6-tools)" >&2
    exit 1
fi
"$windeployqt" --release --no-compiler-runtime \
    --qmldir "$source_dir/src/ui" --qmldir "$source_dir/src/app" \
    "$out/seventhslide.exe"

# What ldd finds in MSYS2's folder, beside the program, until a pass finds
# nothing new: a plugin's DLLs are looked for there too.
prefix=${MINGW_PREFIX:-/ucrt64}
while :; do
    added=0
    while IFS= read -r binary; do
        while IFS= read -r dll; do
            name=$(basename "$dll")
            if [ ! -e "$out/$name" ]; then
                cp "$dll" "$out/"
                added=1
            fi
        done < <(ldd "$binary" | awk -v p="$prefix/" 'index($3, p) == 1 { print $3 }')
    done < <(find "$out" -type f \( -iname '*.exe' -o -iname '*.dll' \))
    [ "$added" -eq 0 ] && break
done

# Anything still named but missing would stop the program at start.
missing=$(find "$out" -type f \( -iname '*.exe' -o -iname '*.dll' \) -print0 \
    | xargs -0 -n 1 ldd 2>/dev/null | grep -i 'not found' | sort -u || true)
if [ -n "$missing" ]; then
    echo "deploy.sh: these are still missing:" >&2
    echo "$missing" >&2
    exit 1
fi

echo "deploy.sh: $(find "$out" -type f | wc -l) files in $out"
