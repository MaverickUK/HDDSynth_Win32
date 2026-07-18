#!/bin/sh
# Rebuilds mingw-w64's CRT startup objects and static support libraries
# (crt2.o, libmingw32.a, libmingwex.a, ...) targeting -march=pentium.
#
# Why this exists: Homebrew's mingw-w64 bottle ships these prebuilt for a
# P6+ CPU baseline (they contain CMOV instructions). Our own -march=pentium
# flag only affects code *we* compile -- it can't touch these prebuilt
# archives. Linking against the stock ones produces a binary that GPFs with
# "invalid instruction" the moment it's run on a real Pentium (or an
# emulator/VM configured as one), inside CRT startup, before main() even
# runs. This script produces a drop-in replacement set built for Pentium.
#
# Run once; the Makefile picks the result up automatically via -B/-L.
set -e

MINGW_VERSION=14.0.0
MINGW_PREFIX=/opt/homebrew/Cellar/mingw-w64/${MINGW_VERSION}_1/toolchain-i686
OUT=/tmp/mingw-pentium-sysroot
SRC_DIR=/tmp/mingw-w64-v${MINGW_VERSION}
BUILD_DIR=/tmp/build-crt-pentium

if [ ! -d "$SRC_DIR" ]; then
    curl -sL -o /tmp/mingw-w64.tar.bz2 \
        "https://downloads.sourceforge.net/project/mingw-w64/mingw-w64/mingw-w64-release/mingw-w64-v${MINGW_VERSION}.tar.bz2"
    tar xjf /tmp/mingw-w64.tar.bz2 -C /tmp
fi

rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

export PATH="$MINGW_PREFIX/bin:$PATH"

"$SRC_DIR/mingw-w64-crt/configure" \
    --host=i686-w64-mingw32 \
    --prefix="$OUT" \
    --with-sysroot="$MINGW_PREFIX/i686-w64-mingw32" \
    --enable-lib32 --disable-lib64 \
    --with-default-msvcrt=msvcrt-os \
    CC="i686-w64-mingw32-gcc -march=pentium -mtune=pentium"

make -j"$(sysctl -n hw.ncpu 2>/dev/null || nproc)"
make install

echo "Pentium-safe CRT installed to $OUT"
