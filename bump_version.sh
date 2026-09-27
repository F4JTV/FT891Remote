#!/usr/bin/env bash
# ============================================================================
#  FT891Remote - version bump
#
#  Every fix and every feature moves the version. It lives in one place,
#  project(FT891Remote VERSION x.y.z) in CMakeLists.txt, and flows from there
#  to the About box, the Windows installer, the Debian package and the Android
#  version code. The changelogs and the AppStream release list do not follow
#  on their own: this script moves them all at once.
#
#  Usage:
#    ./bump_version.sh patch "Fixed the thing that was broken"
#    ./bump_version.sh minor "Added the new thing"
#    ./bump_version.sh major "Changed something incompatible"
#    ./bump_version.sh --show
#
#  patch: a bug fix, nothing new.
#  minor: a new feature, nothing broken for existing users.
#  major: anything an existing setup, or the other end of the link, would
#         have to be changed for (a protocol change is always major).
# ============================================================================
set -euo pipefail

SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CMAKE="$SRC_DIR/CMakeLists.txt"
DEB_CHANGELOG="$SRC_DIR/packaging/deb/changelog"
CHANGELOG="$SRC_DIR/CHANGELOG.md"
METAINFO="$SRC_DIR/packaging/appstream/org.ft891remote.FT891Remote.metainfo.xml.in"

say()  { printf '  %s\n' "$*"; }
die()  { printf '\n[X] %s\n\n' "$*" >&2; exit 1; }

current_version() {
    sed -n 's/^project(FT891Remote VERSION \([0-9.]*\).*/\1/p' "$CMAKE" | head -1
}

[ -f "$CMAKE" ] || die "CMakeLists.txt not found next to this script."
CUR="$(current_version)"
[ -n "$CUR" ] || die "Could not read the version from CMakeLists.txt."

if [ $# -eq 0 ] || [ "${1:-}" = "--show" ]; then
    echo "$CUR"
    exit 0
fi

KIND="$1"
MESSAGE="${2:-}"
[ -n "$MESSAGE" ] || die "Give a one-line description: ./bump_version.sh $KIND \"what changed\""

IFS=. read -r MAJ MIN PAT <<< "$CUR"
case "$KIND" in
    patch) PAT=$((PAT + 1)) ;;
    minor) MIN=$((MIN + 1)); PAT=0 ;;
    major) MAJ=$((MAJ + 1)); MIN=0; PAT=0 ;;
    *)     die "Unknown kind: $KIND  (patch, minor or major)" ;;
esac
NEW="$MAJ.$MIN.$PAT"

# --- CMakeLists.txt: the single source
sed -i "s/^project(FT891Remote VERSION $CUR/project(FT891Remote VERSION $NEW/" "$CMAKE"
[ "$(current_version)" = "$NEW" ] || die "The CMakeLists edit did not take."
say "CMakeLists.txt  $CUR -> $NEW"

# --- Debian changelog: an entry on top, in the format dpkg expects
if [ -f "$DEB_CHANGELOG" ]; then
    DATE="$(date -R)"
    MAINTAINER="$(sed -n 's/^ -- \(.*\)  .*/\1/p' "$DEB_CHANGELOG" | head -1)"
    [ -n "$MAINTAINER" ] || MAINTAINER="FT891Remote Community <noreply@ft891remote.invalid>"
    TMP="$(mktemp)"
    {
        printf 'ft891remote (%s) stable; urgency=medium\n\n' "$NEW"
        printf '  * %s\n\n' "$MESSAGE"
        printf ' -- %s  %s\n\n' "$MAINTAINER" "$DATE"
        cat "$DEB_CHANGELOG"
    } > "$TMP"
    mv "$TMP" "$DEB_CHANGELOG"
    say "packaging/deb/changelog  entry added"
fi

# --- CHANGELOG.md: a section under the title
if [ -f "$CHANGELOG" ]; then
    TMP="$(mktemp)"
    awk -v ver="$NEW" -v day="$(date +%Y-%m-%d)" -v msg="$MESSAGE" '
        !done && /^## / { printf "## %s — %s\n\n- %s\n\n", ver, day, msg; done = 1 }
        { print }
        END { if (!done) printf "\n## %s — %s\n\n- %s\n", ver, day, msg }
    ' "$CHANGELOG" > "$TMP"
    mv "$TMP" "$CHANGELOG"
    say "CHANGELOG.md    section added"
fi

# --- AppStream: a release entry on top, for software centres
if [ -f "$METAINFO" ]; then
    sed -i "s|  <releases>|  <releases>\n    <release version=\"$NEW\" date=\"$(date +%Y-%m-%d)\"/>|" "$METAINFO"
    say "metainfo.xml    release added"
fi

CODE=$(( MAJ * 10000 + MIN * 100 + PAT ))
say "Android versionName $NEW, versionCode $CODE"
say ""
say "Rebuild so that the programs and the packages carry $NEW."
