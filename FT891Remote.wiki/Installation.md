# Installation

FT891Remote has two programs:

| Program | Where it runs | What it needs |
|---|---|---|
| `ft891remote-server` | the computer connected to the FT-891 | the radio's USB cable |
| `ft891remote` | wherever you operate from | a network path to the server |

Both can run on the same machine, which is how the screenshots of this wiki
were taken.

## Download

Ready-made packages are on the [Releases page](https://github.com/F4JTV/FT891Remote/releases). The
[latest release](https://github.com/F4JTV/FT891Remote/releases/latest) is **0.1.28**:

| Platform | Package | Contents |
|---|---|---|
| Windows 10 and 11, 64-bit | [FT891Remote-0.1.28-setup.exe](https://github.com/F4JTV/FT891Remote/releases/download/0.1.28/FT891Remote-0.1.28-setup.exe) | installer: server, client, or both |
| Ubuntu 24.04, amd64 | [ft891remote_0.1.28_amd64.deb](https://github.com/F4JTV/FT891Remote/releases/download/0.1.28/ft891remote_0.1.28_amd64.deb) | server and client |
| Android 8.0 and later | [ft891remote_v0.1.28.apk](https://github.com/F4JTV/FT891Remote/releases/download/0.1.28/ft891remote_v0.1.28.apk) | client |

For another system — a Raspberry Pi, another Linux distribution — build the
programs yourself: see [Building from Source](Building-from-Source).

## Windows

Run `FT891Remote-0.1.28-setup.exe`. The installer lets you choose the server,
the client or both, installs the Visual C++ runtime when it is missing, and
can open the firewall ports the server listens on (TCP 7300, UDP 7301).

The installer is not code-signed: Windows SmartScreen may show "Windows
protected your PC". Choose **More info**, then **Run anyway**.

To produce the installer yourself, see
[Building from Source](Building-from-Source#windows).

## Linux: Ubuntu 24.04

Install the downloaded package with apt, which fetches the Qt, PortAudio and
Opus libraries it needs:

```sh
sudo apt install ./ft891remote_0.1.28_amd64.deb
sudo usermod -a -G dialout $USER      # serial port access; then log in again
```

The package holds both programs, their menu entries and icons, manual pages,
and a systemd user unit for the server.

### Raspberry Pi OS and other distributions

The project builds a Debian package on the machine it runs on:

```sh
./make_deb.sh --deps
sudo apt install ./ft891remote_<version>_<arch>.deb
```

Its dependencies come from the libraries actually linked, so the same script
gives a correct package on Ubuntu (amd64) and on a Raspberry Pi (arm64,
armhf). `--server-only` builds the station side only (no Qt Quick needed on a
headless Raspberry Pi); `--client-only` the operating side only.

## Android

Download `ft891remote_v0.1.28.apk` on the phone and open it. Android asks
once to allow installing applications from the browser or file manager
used; allow it, then install. The client runs on Android 8.0 (API 26) and
later; it opens full screen — swipe from the edge of the screen to show the
system bars for a moment.

An update installs over the previous version as long as both are signed with
the same key. If Android refuses the update, uninstall the previous version
first.

To build the APK yourself, see
[Building from Source](Building-from-Source#android).

## After installing

1. [Prepare the radio](Radio-Setup).
2. [Configure and start the server](Server).
3. [Connect the client](Client).
