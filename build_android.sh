#!/usr/bin/env bash
# ============================================================================
#  FT891Remote - Android APK build
#
#  Configures, compiles, packages, signs and optionally installs the QML
#  client, from Ubuntu 24.04. Every check here corresponds to a real failure
#  met while getting the first APK out; the comments say which.
#
#  On a machine that has never built for Android, --setup installs everything
#  first: JDK, git, Qt for Android and the desktop Qt it needs, the Android SDK
#  and NDK, adb's udev rules, and a signing key.
#
#  Usage:
#    ./build_android.sh --setup --install   first time: set up, build, install
#    ./build_android.sh                 build and sign
#    ./build_android.sh --install       ... then push it to the phone
#    ./build_android.sh --reinstall     ... after removing the previous install
#    ./build_android.sh --logcat        ... and follow the audio log
#    ./build_android.sh --clean         start from scratch
#    ./build_android.sh --no-build      package and sign what is already built
#    ./build_android.sh --no-sign       stop at the unsigned APK
#    ./build_android.sh --help
# ============================================================================
set -uo pipefail

SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SRC_DIR/build-android"

# ------------------------------------------------------- paths, adjust freely
QT_VERSION="${QT_VERSION:-6.11.2}"
QT_ROOT="${QT_ROOT:-$HOME/Qt}"
QT_ANDROID="${QT_ANDROID:-$QT_ROOT/$QT_VERSION/android_arm64_v8a}"
QT_HOST="${QT_HOST:-$QT_ROOT/$QT_VERSION/gcc_64}"
ANDROID_SDK_ROOT="${ANDROID_SDK_ROOT:-$HOME/Android/Sdk}"
NDK_VERSION="${NDK_VERSION:-27.2.12479018}"
SDK_PLATFORM="${SDK_PLATFORM:-36}"
BUILD_TOOLS="${BUILD_TOOLS:-36.0.0}"
KEYSTORE="${KEYSTORE:-$HOME/ft891remote.keystore}"
KEY_ALIAS="${KEY_ALIAS:-ft891remote}"
# Android command-line tools (sdkmanager), used by --setup only.
CMDLINE_TOOLS="${CMDLINE_TOOLS:-11076708}"
AQT_VENV="${AQT_VENV:-$HOME/.local/share/ft891remote-aqt}"

# The version comes from CMakeLists.txt: one place to update, and the APK name
# follows. Without it, two successive builds carry the same name.
VERSION="$(sed -n 's/^project(FT891Remote VERSION \([0-9.]*\).*/\1/p' \
           "$SRC_DIR/CMakeLists.txt" | head -1)"
[ -n "$VERSION" ] || VERSION="0.0.0"

DO_SETUP=0
DO_CLEAN=0
DO_BUILD=1
DO_SIGN=1
DO_INSTALL=0
DO_LOGCAT=0
DO_REINSTALL=0

say()  { printf '  %s\n' "$*"; }
step() { printf '\n== %s\n' "$*"; }
die()  { printf '\n[X] %s\n' "$*" >&2; exit 1; }

usage() {
    cat <<'HELP'
FT891Remote - Android APK build

  ./build_android.sh --setup         install the toolchain first (Ubuntu 24.04)
  ./build_android.sh                 build and sign
  ./build_android.sh --install       ... then push it to the phone
  ./build_android.sh --reinstall     ... after removing the previous install
  ./build_android.sh --logcat        ... and follow the audio log
  ./build_android.sh --clean         start from scratch
  ./build_android.sh --no-build      package and sign what is already built
  ./build_android.sh --no-sign       stop at the unsigned APK
  ./build_android.sh --help          this text

Paths come from the variables at the top of the file, each overridable from
the environment: QT_VERSION, QT_ROOT, QT_ANDROID, QT_HOST, ANDROID_SDK_ROOT,
NDK_VERSION, SDK_PLATFORM, BUILD_TOOLS, KEYSTORE, KEY_ALIAS, CMDLINE_TOOLS.

--setup downloads Qt with aqtinstall (in a Python virtual environment under
~/.local/share/ft891remote-aqt) and the Android command-line tools from
Google, then asks sdkmanager for the platform, build tools, NDK and
platform-tools. Nothing is installed twice: what is already there is kept.

Set QT_ANDROID_KEYSTORE_STORE_PASS to have Qt sign during the build instead
of running apksigner afterwards.
HELP
    exit 0
}

while [ $# -gt 0 ]; do
    case "$1" in
        --setup)   DO_SETUP=1 ;;
        --clean)   DO_CLEAN=1 ;;
        --no-build) DO_BUILD=0 ;;
        --no-sign) DO_SIGN=0 ;;
        --install) DO_INSTALL=1 ;;
        --reinstall) DO_INSTALL=1; DO_REINSTALL=1 ;;
        --logcat)  DO_INSTALL=1; DO_LOGCAT=1 ;;
        -h|--help) usage ;;
        *) die "Unknown option: $1  (try --help)" ;;
    esac
    shift
done


# ==================================================================== setup
# Everything a fresh Ubuntu 24.04 lacks to build the APK. Each part checks
# first and only installs what is missing, so --setup can be run again.
setup_toolchain() {
    step "Setting up the Android toolchain"
    command -v apt-get >/dev/null 2>&1 || die "--setup expects Ubuntu or Debian (apt)."

    local sudo_cmd=""
    [ "$(id -u)" -eq 0 ] || sudo_cmd="sudo"

    # JDK 21: Qt 6.11 drives Android Gradle Plugin 9, which needs 17 or later.
    # git: CMake fetches Oboe and Opus with it. The platform-tools-common
    # package brings the udev rules adb needs to see a phone without root.
    local pkgs="openjdk-21-jdk-headless git cmake ninja-build unzip curl
                python3-venv android-sdk-platform-tools-common"
    local miss=""
    for p in $pkgs; do
        dpkg-query -W -f='${Status}' "$p" 2>/dev/null | grep -q "ok installed" || miss="$miss $p"
    done
    if [ -n "$miss" ]; then
        say "Installing:$miss"
        $sudo_cmd apt-get update || say "apt-get update failed, carrying on anyway"
        # shellcheck disable=SC2086
        $sudo_cmd apt-get install -y $miss || die "apt could not install:$miss"
    else
        say "System packages already installed"
    fi
    # adb talks to the phone through the plugdev group on Ubuntu.
    if ! id -nG | tr ' ' '\n' | grep -qx plugdev; then
        $sudo_cmd usermod -a -G plugdev "$(id -un)" 2>/dev/null \
            && say "Added $(id -un) to plugdev: log out and in again before --install"
    fi

    # ---------------------------------------------------------------- Qt
    # aqtinstall fetches the official Qt archives. Ubuntu 24.04 refuses pip
    # installs into the system Python, hence the virtual environment.
    if [ ! -x "$QT_ANDROID/bin/qt-cmake" ] || [ ! -x "$QT_HOST/bin/androiddeployqt" ]; then
        if [ ! -x "$AQT_VENV/bin/aqt" ]; then
            say "Installing aqtinstall into $AQT_VENV"
            python3 -m venv "$AQT_VENV" || die "Could not create $AQT_VENV"
            "$AQT_VENV/bin/pip" install --quiet --upgrade pip aqtinstall \
                || die "pip could not install aqtinstall"
        fi
        # aqt writes aqtinstall.log into the current directory: run it from
        # the Qt folder, not from the sources.
        mkdir -p "$QT_ROOT"
        if [ ! -x "$QT_HOST/bin/androiddeployqt" ]; then
            say "Qt $QT_VERSION for the desktop (hosts androiddeployqt), a few hundred MB..."
            ( cd "$QT_ROOT" && "$AQT_VENV/bin/aqt" install-qt linux desktop "$QT_VERSION" linux_gcc_64 -O "$QT_ROOT" ) \
                || die "aqt could not install the desktop Qt $QT_VERSION (log: $QT_ROOT/aqtinstall.log)"
        fi
        if [ ! -x "$QT_ANDROID/bin/qt-cmake" ]; then
            # No -m switch: Qt Quick is part of the base package, and adding
            # qtdeclarative as a module does not work.
            say "Qt $QT_VERSION for Android arm64-v8a, a few hundred MB..."
            ( cd "$QT_ROOT" && "$AQT_VENV/bin/aqt" install-qt linux android "$QT_VERSION" android_arm64_v8a -O "$QT_ROOT" ) \
                || die "aqt could not install Qt $QT_VERSION for Android (log: $QT_ROOT/aqtinstall.log)"
        fi
    else
        say "Qt $QT_VERSION already installed under $QT_ROOT"
    fi

    # --------------------------------------------------------- Android SDK
    local sdkm="$ANDROID_SDK_ROOT/cmdline-tools/latest/bin/sdkmanager"
    if [ ! -x "$sdkm" ]; then
        say "Android command-line tools $CMDLINE_TOOLS"
        local zip="/tmp/commandlinetools-linux-${CMDLINE_TOOLS}_latest.zip"
        curl -fL -o "$zip" \
            "https://dl.google.com/android/repository/commandlinetools-linux-${CMDLINE_TOOLS}_latest.zip" \
            || die "Download of the command-line tools failed. Check CMDLINE_TOOLS against
    https://developer.android.com/studio#command-line-tools-only"
        # The archive holds cmdline-tools/; sdkmanager expects it as
        # cmdline-tools/latest/, or it refuses to run.
        local tmpd
        tmpd="$(mktemp -d)"
        unzip -q "$zip" -d "$tmpd" || die "Could not unpack $zip"
        mkdir -p "$ANDROID_SDK_ROOT/cmdline-tools"
        rm -rf "$ANDROID_SDK_ROOT/cmdline-tools/latest"
        mv "$tmpd/cmdline-tools" "$ANDROID_SDK_ROOT/cmdline-tools/latest"
        rm -rf "$tmpd" "$zip"
    fi
    say "Accepting the SDK licences"
    yes 2>/dev/null | "$sdkm" --sdk_root="$ANDROID_SDK_ROOT" --licenses >/dev/null 2>&1 || true
    say "SDK platform $SDK_PLATFORM, build tools $BUILD_TOOLS, NDK $NDK_VERSION, platform-tools"
    "$sdkm" --sdk_root="$ANDROID_SDK_ROOT" \
        "platforms;android-$SDK_PLATFORM" "build-tools;$BUILD_TOOLS" \
        "ndk;$NDK_VERSION" "platform-tools" \
        || die "sdkmanager could not install the SDK packages"

    # --------------------------------------------------------- signing key
    if [ "$DO_SIGN" -eq 1 ] && [ ! -f "$KEYSTORE" ]; then
        step "Signing key"
        say "Android installs only signed APKs, and an update must carry the"
        say "same key as the version it replaces: keep $KEYSTORE safe."
        say "keytool asks for a password and a few certificate fields; they end up"
        say "readable in the APK."
        keytool -genkey -v -keystore "$KEYSTORE" -alias "$KEY_ALIAS" \
            -keyalg RSA -keysize 2048 -validity 10000 || die "keytool failed."
    fi
    say "Toolchain ready."
}

[ "$DO_SETUP" -eq 1 ] && setup_toolchain

# ============================================================ prerequisites
step "Checking prerequisites"

[ -f "$SRC_DIR/CMakeLists.txt" ] || die "Run this from the project directory."
say "Version       $VERSION"
# The version code derives from the version, as in CMakeLists.txt: it is what
# Android compares from one release to the next.
VERSION_CODE="$(echo "$VERSION" | awk -F. '{printf "%d", $1 * 10000 + $2 * 100 + $3}')"
say "Version code  $VERSION_CODE"

command -v cmake >/dev/null 2>&1 || die "cmake not found: sudo apt install cmake  (or run --setup)"
command -v java  >/dev/null 2>&1 || die "No JDK: sudo apt install openjdk-21-jdk-headless  (or run --setup)"
command -v javac >/dev/null 2>&1 || die "A JRE is not enough, Gradle needs the JDK:
    sudo apt install openjdk-21-jdk-headless"
JAVA_MAJOR="$(java -version 2>&1 | sed -n 's/.*version "\([0-9]*\).*/\1/p' | head -1)"
[ -n "$JAVA_MAJOR" ] && [ "$JAVA_MAJOR" -lt 17 ] && die "JDK $JAVA_MAJOR is too old: Android Gradle Plugin 9 needs 17 or later.
    sudo apt install openjdk-21-jdk-headless"
say "JDK           ${JAVA_MAJOR:-unknown}"
# Oboe and Opus are fetched by CMake from GitHub on the first build.
command -v git >/dev/null 2>&1 || die "git not found: sudo apt install git  (or run --setup)"

# Qt ships two halves. The Android one holds the libraries, the desktop one
# holds androiddeployqt and qmlimportscanner, which run on the build machine.
[ -x "$QT_ANDROID/bin/qt-cmake" ] || die "Qt for Android not found at $QT_ANDROID  (run --setup)
    aqt install-qt linux android $QT_VERSION android_arm64_v8a -O $QT_ROOT
    (no -m switch: Qt Quick is part of the base package, not an add-on)"
say "Qt Android    $QT_ANDROID"

[ -x "$QT_HOST/bin/androiddeployqt" ] || die "Host Qt not found at $QT_HOST
    aqt install-qt linux desktop $QT_VERSION linux_gcc_64 -O $QT_ROOT"
say "Qt host       $QT_HOST"

[ -d "$QT_ANDROID/lib/cmake/Qt6Quick" ] || die "Qt Quick missing from the Android install.
    Reinstall the base package; adding qtdeclarative with -m does not work."

[ -d "$ANDROID_SDK_ROOT" ] || die "Android SDK not found at $ANDROID_SDK_ROOT  (run --setup)"

# API 36 is not a preference. Qt 6.11 drives Android Gradle Plugin 9, which
# pulls androidx.core 1.17, which refuses to compile against anything older.
if [ ! -d "$ANDROID_SDK_ROOT/platforms/android-$SDK_PLATFORM" ]; then
    die "Android platform $SDK_PLATFORM missing:
    sdkmanager \"platforms;android-$SDK_PLATFORM\" \"build-tools;$BUILD_TOOLS\""
fi
say "SDK platform  android-$SDK_PLATFORM"

ANDROID_NDK_ROOT="$ANDROID_SDK_ROOT/ndk/$NDK_VERSION"
# A mismatched NDK does not produce a readable error, only undefined symbols
# at link time. Check it up front.
[ -d "$ANDROID_NDK_ROOT" ] || die "NDK $NDK_VERSION missing:
    sdkmanager \"ndk;$NDK_VERSION\"
    It must match the NDK your Qt version was built with."
say "NDK           $NDK_VERSION"

BT_DIR="$ANDROID_SDK_ROOT/build-tools/$BUILD_TOOLS"
if [ "$DO_SIGN" -eq 1 ] && [ ! -x "$BT_DIR/apksigner" ]; then
    say "Build tools $BUILD_TOOLS absent; Gradle will fetch what it needs,"
    say "but signing then falls back to Qt. Install them to sign here:"
    say "  sdkmanager \"build-tools;$BUILD_TOOLS\""
fi

export ANDROID_SDK_ROOT ANDROID_NDK_ROOT

# ------------------------------------------------------------ signing key
SIGN_IN_CMAKE=0
if [ "$DO_SIGN" -eq 1 ]; then
    if [ ! -f "$KEYSTORE" ]; then
        say "No keystore at $KEYSTORE. Create one once with:"
        say "  keytool -genkey -v -keystore $KEYSTORE -alias $KEY_ALIAS \\"
        say "          -keyalg RSA -keysize 2048 -validity 10000"
        say "Note: the certificate fields end up readable in the APK."
        die "Signing key missing. Create it, or pass --no-sign."
    fi
    # Qt signs during the build when these are set, which avoids a second pass.
    if [ -n "${QT_ANDROID_KEYSTORE_STORE_PASS:-}" ]; then
        export QT_ANDROID_KEYSTORE_PATH="$KEYSTORE"
        export QT_ANDROID_KEYSTORE_ALIAS="$KEY_ALIAS"
        export QT_ANDROID_KEYSTORE_KEY_PASS="${QT_ANDROID_KEYSTORE_KEY_PASS:-$QT_ANDROID_KEYSTORE_STORE_PASS}"
        SIGN_IN_CMAKE=1
        say "Signing during the build, password taken from the environment"
    else
        say "Signing after the build with apksigner (it will ask for the password)"
        say "  Set QT_ANDROID_KEYSTORE_STORE_PASS to have Qt sign directly."
    fi
fi

# ================================================================== cleaning
if [ "$DO_CLEAN" -eq 1 ]; then
    step "Cleaning"
    rm -rf "$BUILD_DIR"
    say "$BUILD_DIR removed"
fi

# ================================================================ configure
if [ "$DO_BUILD" -eq 1 ]; then
    step "Configuring"
    CFG_ARGS=(
        -B "$BUILD_DIR"
        -DCMAKE_BUILD_TYPE=Release
        -DQT_HOST_PATH="$QT_HOST"
        -DANDROID_SDK_ROOT="$ANDROID_SDK_ROOT"
        -DANDROID_NDK_ROOT="$ANDROID_NDK_ROOT"
        -DRR_ANDROID_TARGET_SDK="$SDK_PLATFORM"
    )
    [ "$SIGN_IN_CMAKE" -eq 1 ] && CFG_ARGS+=(-DQT_ANDROID_SIGN_APK:BOOL=ON)

    "$QT_ANDROID/bin/qt-cmake" "${CFG_ARGS[@]}" || die "Configuration failed."

    step "Compiling"
    say "Oboe and Opus are fetched and built from source on the first run."
    cmake --build "$BUILD_DIR" -j"$(nproc)" || die "Compilation failed."
fi

# ================================================================ packaging
# androiddeployqt copies android/ into android-build/ but never deletes: an
# outdated manifest or a missing icon survives a source update. Wiping the
# directory is the only reliable way to pick up changes.
step "Packaging"
rm -rf "$BUILD_DIR/android-build"
cmake --build "$BUILD_DIR" --target apk || die "APK packaging failed."

APK_DIR="$BUILD_DIR/android-build/build/outputs/apk/release"
# Careful with the pattern: "unsigned" contains "signed". Looking for
# *signed*.apk finds the unsigned APK, and the script thinks it is done.
UNSIGNED="$(ls "$APK_DIR"/*-unsigned.apk 2>/dev/null | head -1)"
SIGNED="$(ls "$APK_DIR"/*.apk 2>/dev/null | grep -v -- '-unsigned\.apk$' | head -1)"

# ================================================================== signing
FINAL=""
if [ -n "$SIGNED" ]; then
    # Qt signed during the build: renamed so that the file carries its
    # version, as in the other branch.
    FINAL="$SRC_DIR/ft891remote_v${VERSION}.apk"
    cp -f "$SIGNED" "$FINAL" || die "Could not copy the signed APK."
elif [ "$DO_SIGN" -eq 1 ] && [ -n "$UNSIGNED" ]; then
    step "Signing"
    [ -x "$BT_DIR/apksigner" ] || die "apksigner missing: sdkmanager \"build-tools;$BUILD_TOOLS\""
    FINAL="$SRC_DIR/ft891remote_v${VERSION}.apk"
    # zipalign first, always: realigning a signed package breaks its signature.
    "$BT_DIR/zipalign" -p -f 4 "$UNSIGNED" "$BUILD_DIR/aligned.apk" \
        || die "zipalign failed."
    "$BT_DIR/apksigner" sign --ks "$KEYSTORE" --ks-key-alias "$KEY_ALIAS" \
        --out "$FINAL" "$BUILD_DIR/aligned.apk" || die "apksigner failed."
    say "$(basename "$FINAL")"
else
    FINAL="$SRC_DIR/ft891remote_v${VERSION}-unsigned.apk"
    cp -f "$UNSIGNED" "$FINAL" 2>/dev/null || FINAL="$UNSIGNED"
    say "Unsigned APK. Android will refuse to install it:"
    say "  INSTALL_PARSE_FAILED_NO_CERTIFICATES"
fi

[ -n "$FINAL" ] && [ -f "$FINAL" ] || die "No APK produced in $APK_DIR"

# The APK must carry the time of this build: if not, an old file was picked
# up somewhere.
step "Package produced"
say "File       $FINAL"
say "Built      $(date -r "$FINAL" '+%Y-%m-%d %H:%M:%S')"
say "Fingerprint $(sha256sum "$FINAL" | cut -c1-16)"

# ============================================================== installing
if [ "$DO_INSTALL" -eq 1 ]; then
    step "Installing"
    # The SDK's own adb first: the distribution's may be older than the phone.
    if [ -x "$ANDROID_SDK_ROOT/platform-tools/adb" ]; then
        adb() { "$ANDROID_SDK_ROOT/platform-tools/adb" "$@"; }
    fi
    command -v adb >/dev/null 2>&1 || type adb >/dev/null 2>&1 \
        || die "adb not found: sdkmanager \"platform-tools\"  (or run --setup)"
    STATE="$(adb get-state 2>/dev/null)"
    if [ "$STATE" != "device" ]; then
        say "No phone ready (adb reports: ${STATE:-nothing})."
        say "Enable Developer options and USB debugging on the phone, plug it in,"
        say "and accept the computer's key on its screen."
        say "On Samsung, Settings > Security and privacy > Auto Blocker must be"
        say "off before USB debugging can even be ticked."
        die "Phone unreachable."
    fi
    if [ "$DO_REINSTALL" -eq 1 ]; then
        say "Removing the previous install first"
        adb uninstall org.ft891remote.client >/dev/null 2>&1
    fi
    adb install -r "$FINAL" || die "Installation refused.
    A different signing key from the one already on the phone gives
    INSTALL_FAILED_UPDATE_INCOMPATIBLE: rerun with --reinstall."
    # Read back what the phone actually registered: the only proof that the
    # new version is in place.
    INSTALLED="$(adb shell dumpsys package org.ft891remote.client 2>/dev/null \
                 | grep -E 'versionName|lastUpdateTime' | tr -d '\r' | sed 's/^ *//')"
    say "installed"
    [ -n "$INSTALLED" ] && printf '%s\n' "$INSTALLED" | sed 's/^/    /'
fi

# ==================================================================== report
step "Done."
say "APK        $FINAL"
say "Size       $(du -h "$FINAL" | cut -f1)"
if [ "$DO_LOGCAT" -eq 1 ]; then
    step "Audio log, Ctrl-C to stop"
    say "Oboe reports the stream it actually obtained: rate, buffer, latency path."
    adb logcat -c
    adb shell am start -n org.ft891remote.client/org.ft891remote.client.Ft891RemoteActivity >/dev/null 2>&1
    sleep 2
    # Filtering by tag misses what the application itself logs: follow its
    # process instead, plus the system's audio tags.
    APP_PID="$(adb shell pidof org.ft891remote.client 2>/dev/null | tr -d '\r')"
    if [ -n "$APP_PID" ]; then
        say "Following pid $APP_PID"
        adb logcat --pid="$APP_PID" 2>/dev/null || adb logcat | grep -E "oboe|AAudio|AudioRecord|FT891Remote|Qt"
    else
        adb logcat | grep -E "oboe|AAudio|AudioRecord|FT891Remote|Qt"
    fi
else
    say "Next:      ./build_android.sh --logcat   to watch the audio layer start"
fi
