#!/usr/bin/env bash
# ============================================================================
#  FT891Remote - Debian package
#
#  Produces a .deb for the machine it runs on: amd64 on a PC, arm64 on a
#  64-bit Raspberry Pi, armhf on a 32-bit one. A .deb carries compiled code,
#  so one architecture equals one package; there is no universal build.
#  Written for Ubuntu 24.04 (Qt 6.4); any Debian-based system with Qt 6.4 or
#  later works the same way.
#
#  Dependencies are not written by hand: dpkg-shlibdeps reads the libraries
#  actually linked into the binaries and names the packages that provide them.
#  The QML modules, loaded at run time and never linked, are listed in
#  CMakeLists.txt.
#
#  Usage:
#    ./make_deb.sh                server and client in one package
#    ./make_deb.sh --server-only  the station side only (no Qt Quick needed)
#    ./make_deb.sh --client-only  the front panel only (no serial port needed)
#    ./make_deb.sh --check        run lintian on the result
#    ./make_deb.sh --deps         install the build dependencies first (sudo)
#    ./make_deb.sh --no-deps      never check them, and do not ask
#    ./make_deb.sh --clean        start from an empty build directory
#    ./make_deb.sh --help
# ============================================================================
set -uo pipefail

SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SRC_DIR/build-deb"
WITH_SERVER=ON
WITH_CLIENT=ON
DO_CHECK=0
DO_CLEAN=0
DO_DEPS=ask          # ask | yes | no

say()  { printf '  %s\n' "$*"; }
step() { printf '\n== %s\n' "$*"; }
die()  { printf '\n[X] %s\n' "$*" >&2; exit 1; }

while [ $# -gt 0 ]; do
    case "$1" in
        --server-only) WITH_CLIENT=OFF ;;
        --client-only) WITH_SERVER=OFF ;;
        --check)   DO_CHECK=1 ;;
        --clean)   DO_CLEAN=1 ;;
        --deps)    DO_DEPS=yes ;;
        --no-deps) DO_DEPS=no ;;
        -h|--help)
            sed -n '2,25p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
            exit 0 ;;
        *) die "Unknown option: $1  (try --help)" ;;
    esac
    shift
done
[ "$WITH_SERVER" = OFF ] && [ "$WITH_CLIENT" = OFF ] && \
    die "--server-only and --client-only together leave nothing to package."

step "Checking prerequisites"
[ -f "$SRC_DIR/CMakeLists.txt" ] || die "Run this from the project directory."

# ------------------------------------------------------------- dependencies
# Shared with the other Linux scripts: one list to keep up to date.
# shellcheck source=packaging/build-deps.sh
. "$SRC_DIR/packaging/build-deps.sh"

RR_DEPS_MODE="$DO_DEPS"
RR_EXTRA_DEPS="dpkg-dev"     # dpkg-shlibdeps, to work out the dependencies
rr_install_deps || true

command -v cmake >/dev/null 2>&1 || die "cmake missing: sudo apt install cmake"
command -v cpack >/dev/null 2>&1 || die "cpack missing: it ships with cmake"
command -v dpkg-shlibdeps >/dev/null 2>&1 || \
    die "dpkg-shlibdeps missing: sudo apt install dpkg-dev"
command -v gzip >/dev/null 2>&1 || die "gzip missing: sudo apt install gzip"

VERSION="$(sed -n 's/^project(FT891Remote VERSION \([0-9.]*\).*/\1/p' \
           "$SRC_DIR/CMakeLists.txt" | head -1)"
ARCH="$(dpkg --print-architecture)"
say "Version       ${VERSION:-unknown}"
say "Architecture  $ARCH"
say "Server        $WITH_SERVER"
say "Client        $WITH_CLIENT"
if [ -r /etc/os-release ]; then
    # shellcheck disable=SC1091
    . /etc/os-release
    say "System        ${PRETTY_NAME:-unknown}"
fi

# The maintainer scripts must be executable inside the package; the bit gets
# lost when the sources are copied through some file systems.
chmod 755 "$SRC_DIR/packaging/deb/postinst" "$SRC_DIR/packaging/deb/postrm" 2>/dev/null || true

if [ "$DO_CLEAN" -eq 1 ] && [ -d "$BUILD_DIR" ]; then
    step "Cleaning"
    rm -rf "$BUILD_DIR"
    say "$BUILD_DIR removed"
fi
# A package built for another selection would keep the other side's files.
rm -f "$BUILD_DIR"/*.deb 2>/dev/null

step "Building"
cmake -S "$SRC_DIR" -B "$BUILD_DIR" \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=/usr \
      -DFT891_BUILD_SERVER="$WITH_SERVER" \
      -DFT891_BUILD_CLIENT="$WITH_CLIENT" \
      -DFT891_BUILD_TESTS=OFF >/dev/null || die "Configuration failed."
cmake --build "$BUILD_DIR" -j"$(rr_build_jobs)" || die "Compilation failed."

step "Packaging"
# cpack runs from the build directory: dpkg-shlibdeps looks for the binaries
# there.
( cd "$BUILD_DIR" && cpack -G DEB ) || die "Packaging failed."

DEB="$(ls -t "$BUILD_DIR"/*.deb 2>/dev/null | head -1)"
[ -n "$DEB" ] || die "No .deb produced."

# A package that only holds the client, or only the server, says so in its
# file name: both would otherwise be called ft891remote_<version>_<arch>.deb.
SUFFIX=""
[ "$WITH_SERVER" = OFF ] && SUFFIX="-client"
[ "$WITH_CLIENT" = OFF ] && SUFFIX="-server"
if [ -n "$SUFFIX" ]; then
    NEW="${DEB%.deb}"
    NEW="${NEW/ft891remote_/ft891remote${SUFFIX}-only_}.deb"
    mv -f "$DEB" "$NEW" && DEB="$NEW"
fi
cp -f "$DEB" "$SRC_DIR/" && DEB="$SRC_DIR/$(basename "$DEB")"

step "Result"
say "File          $DEB"
say "Size          $(du -h "$DEB" | cut -f1)"
say "Programs:"
dpkg-deb -c "$DEB" | awk '$6 ~ /usr\/bin\/./ {print "    " $6}'
say "Dependencies:"
dpkg-deb -f "$DEB" Depends | tr ',' '\n' | sed 's/^ */    /'

if [ "$DO_CHECK" -eq 1 ]; then
    step "lintian"
    if ! command -v lintian >/dev/null 2>&1; then
        if [ "$DO_DEPS" != "no" ] && command -v apt-get >/dev/null 2>&1; then
            say "Installing lintian..."
            { [ "$(id -u)" -eq 0 ] && apt-get install -y lintian; } \
                || sudo apt-get install -y lintian \
                || die "lintian missing: sudo apt install lintian"
        else
            die "lintian missing: sudo apt install lintian"
        fi
    fi
    lintian --tag-display-limit 0 "$DEB" || true
fi

step "Install with"
say "sudo apt install $DEB"
say "(apt, not dpkg -i: it pulls the dependencies on its own)"
if [ "$WITH_SERVER" = ON ]; then
    say ""
    say "Then, for the server:"
    say "  sudo usermod -a -G dialout \$USER     serial port access, log in again"
    say "  systemctl --user enable --now ft891remote-server    to start it with the session"
fi
