# ============================================================================
#  Build dependencies, shared by the Linux build scripts.
#
#  One place to keep them up to date: the scripts compile the same sources,
#  and a duplicated list always ends up diverging.
#
#  The caller defines say(), step() and die(), and sets WITH_SERVER and
#  WITH_CLIENT to ON or OFF. This file is sourced, not run.
#
#  Package names are Debian's and Ubuntu's, hence Raspberry Pi OS's too.
#  No Hamlib: FT891Remote speaks the FT-891's CAT protocol itself.
# ============================================================================

RR_COMMON_DEPS="build-essential cmake qt6-base-dev portaudio19-dev libopus-dev"
RR_SERVER_DEPS="qt6-serialport-dev"
RR_CLIENT_DEPS="qt6-declarative-dev qml6-module-qtquick qml6-module-qtquick-controls
                qml6-module-qtquick-layouts qml6-module-qtquick-templates
                qml6-module-qtquick-window qml6-module-qtqml-workerscript"

# Prints the packages of the list that are not installed, nothing if all are.
rr_missing_packages() {
    local want="$1" miss=""
    for pkg in $want; do
        dpkg-query -W -f='${Status}' "$pkg" 2>/dev/null | grep -q "ok installed" \
            || miss="$miss $pkg"
    done
    printf '%s' "$miss"
}

# Installs what is missing. RR_DEPS_MODE is ask, yes or no; RR_EXTRA_DEPS lets
# the caller add packages of its own (dpkg-dev, for instance).
# Returns 0 if the build can go ahead, 1 otherwise.
rr_install_deps() {
    [ "${RR_DEPS_MODE:-ask}" = "no" ] && return 0

    command -v apt-get >/dev/null 2>&1 || {
        say "Not a Debian-based system; install the dependencies yourself."
        return 1
    }
    command -v dpkg-query >/dev/null 2>&1 || return 1

    local want="$RR_COMMON_DEPS ${RR_EXTRA_DEPS:-}"
    [ "${WITH_SERVER:-ON}" = "ON" ] && want="$want $RR_SERVER_DEPS"
    [ "${WITH_CLIENT:-ON}" = "ON" ] && want="$want $RR_CLIENT_DEPS"

    local miss
    miss="$(rr_missing_packages "$want")"
    if [ -z "$miss" ]; then
        say "All build dependencies are already installed."
        return 0
    fi

    step "Build dependencies"
    say "Missing:$miss"

    if [ "${RR_DEPS_MODE:-ask}" = "ask" ]; then
        # No terminal to answer from: do not block, say what to do.
        if [ ! -t 0 ]; then
            say "Install them with: sudo apt install$miss"
            return 1
        fi
        printf "  Install them now? [Y/n] "
        read -r reply
        case "$reply" in [nN]*) say "Skipped."; return 1 ;; esac
    fi

    local sudo_cmd=""
    [ "$(id -u)" -eq 0 ] || sudo_cmd="sudo"
    if [ -n "$sudo_cmd" ] && ! command -v sudo >/dev/null 2>&1; then
        say "No sudo available. Install them with: apt install$miss"
        return 1
    fi

    $sudo_cmd apt-get update || say "apt-get update failed, carrying on anyway"
    # shellcheck disable=SC2086
    $sudo_cmd apt-get install -y $miss || die "Installing the dependencies failed."
    say "Done."
    return 0
}

# Number of parallel compile jobs, bounded by memory and not only by cores:
# compiling Qt code takes about 700 MiB per job, and a 2 GiB Raspberry Pi
# running four would be killed by the kernel halfway through.
rr_build_jobs() {
    local jobs mem_kb by_mem
    jobs="$(nproc 2>/dev/null || echo 1)"
    mem_kb="$(awk '/MemTotal/ {print $2}' /proc/meminfo 2>/dev/null || echo 0)"
    if [ "$mem_kb" -gt 0 ]; then
        by_mem=$(( mem_kb / 700000 ))
        [ "$by_mem" -lt 1 ] && by_mem=1
        if [ "$by_mem" -lt "$jobs" ]; then
            say "Limiting to $by_mem parallel job(s): $(( mem_kb / 1024 )) MiB of RAM" >&2
            jobs="$by_mem"
        fi
    fi
    printf '%s' "$jobs"
}
