# Building from Source

Requirements: CMake 3.19+, a C++17 compiler, **Qt 6.4 or later** (Core, Gui,
Network, SerialPort, Widgets, Quick, QuickControls2), **PortAudio**, and
**Opus** (optional but recommended). No Hamlib.

## Linux

The simplest way is the Debian package script:

```sh
./make_deb.sh --deps
```

| Option | Effect |
|---|---|
| `--server-only` | the station side only: no Qt Quick needed |
| `--client-only` | the operating side only: no serial port module needed |
| `--check` | run lintian on the result |
| `--deps` / `--no-deps` | install missing build dependencies without asking / never |
| `--clean` | start from an empty build directory |

To build without packaging:

```sh
sudo apt install build-essential cmake qt6-base-dev qt6-serialport-dev \
    qt6-declarative-dev qml6-module-qtquick qml6-module-qtquick-controls \
    qml6-module-qtquick-layouts qml6-module-qtquick-templates \
    qml6-module-qtquick-window qml6-module-qtqml-workerscript \
    portaudio19-dev libopus-dev
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

CMake options: `-DFT891_BUILD_SERVER=OFF` or `-DFT891_BUILD_CLIENT=OFF` to
build one side only, `-DFT891_BUILD_TESTS=ON` for the [test programs](Testing).

## Windows

Install once: **Visual Studio 2022 or 2026** with the "Desktop development
with C++" workload, **Qt for MSVC 64-bit** (with Qt Quick and, under
Additional Libraries, Qt Serial Port), **git**, and **Inno Setup 6**. Then,
in an **x64 Native Tools Command Prompt**, from the project folder:

```bat
build_all.bat /deps
```

The script installs vcpkg with PortAudio and Opus if they are missing,
configures and compiles, gathers the programs with the Qt runtime, the QML
modules, PortAudio, Opus and the Visual C++ redistributable into
`installer\dist`, and compiles `installer\FT891Remote.iss` into
`installer\output\FT891Remote-<version>-setup.exe`.

| Option | Effect |
|---|---|
| `/deps` | install vcpkg, PortAudio and Opus first if missing |
| `/clean` | wipe the build directory first |
| `/nobuild` | skip configure and compile; deploy and package only |
| `/noinstaller` | stop after staging `installer\dist` |

Edit `QT_DIR` and `VCPKG_ROOT` at the top of the script if yours differ.
The script names that Qt to CMake explicitly (`Qt6_DIR`): a Qt that vcpkg
holds for another project, often without Qt Quick, is never picked up in its
place. It checks before configuring that the Qt at `QT_DIR` has Qt Quick and
Qt Serial Port.

## Android

From Ubuntu 24.04, the first time:

```sh
./build_android.sh --setup --install
```

`--setup` installs the JDK, git and adb's udev rules with apt, Qt for
Android and the desktop Qt it needs with aqtinstall (in a Python virtual
environment), the Android SDK platform, build tools, NDK and platform-tools
with sdkmanager, and creates the signing key — keep it: an update must carry
the same key as the version it replaces. Oboe and Opus are fetched and built
from source.

Afterwards:

```sh
./build_android.sh --install       # build, sign, install
./build_android.sh --reinstall     # ... removing the previous install first
./build_android.sh --logcat        # ... and follow the application's log
./build_android.sh --no-sign       # stop at the unsigned APK
```

Paths and versions (Qt 6.11.2, NDK 27.2.12479018, SDK platform 36) are
variables at the top of the script, each overridable from the environment.

## Version numbers

`./bump_version.sh patch "What changed."` raises the version in
`CMakeLists.txt`, adds the entry to `CHANGELOG.md`, the Debian changelog and
the AppStream release list. `./bump_version.sh --show` prints the current
version.
