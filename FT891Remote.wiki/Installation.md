# Installation

FT891Remote has two programs:

| Program | Where it runs | What it needs |
|---|---|---|
| `ft891remote-server` | the computer connected to the FT-891 | the radio's USB cable |
| `ft891remote` | wherever you operate from | a network path to the server |

Both can run on the same machine, which is how the screenshots of this wiki
were taken.

## Windows

Run `FT891Remote-<version>-setup.exe`. The installer lets you choose the
server, the client or both, installs the Visual C++ runtime when it is
missing, and can open the firewall ports the server listens on (TCP 7300,
UDP 7301).

To produce the installer yourself, see
[Building from Source](Building-from-Source#windows).

## Linux: Ubuntu 24.04 and Raspberry Pi OS

The project builds a Debian package on the machine it runs on:

```sh
./make_deb.sh --deps
sudo apt install ./ft891remote_<version>_<arch>.deb
sudo usermod -a -G dialout $USER      # serial port access; then log in again
```

The package holds both programs, their menu entries and icons, manual pages,
and a systemd user unit for the server. Its dependencies come from the
libraries actually linked, so the same script gives a correct package on
Ubuntu (amd64) and on a Raspberry Pi (arm64, armhf).

`--server-only` builds the station side only (no Qt Quick needed on a
headless Raspberry Pi); `--client-only` the operating side only.

## Android

The client runs on Android 8.0 (API 26) and later. Build, sign and install it from an
Ubuntu 24.04 machine with the phone connected by USB and USB debugging
enabled:

```sh
./build_android.sh --setup --install     # the first time
./build_android.sh --install             # afterwards
```

Details in [Building from Source](Building-from-Source#android). The
application opens full screen; swipe from the edge of the screen to show the
system bars for a moment.

## After installing

1. [Prepare the radio](Radio-Setup).
2. [Configure and start the server](Server).
3. [Connect the client](Client).
