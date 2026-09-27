# Tests

## CAT engine against a simulated radio

`ft891_sim.py` opens a pseudo-terminal that behaves like an FT-891 as the CAT
reference describes it: frequencies, modes, TX, meters, band stack, VFO keys,
clarifier, keyer memories, power switch, and every setting of
`data/cat/ft891.json` from a table of values.

```sh
cmake -B build -DFT891_BUILD_TESTS=ON && cmake --build build
python3 test/ft891_sim.py --link /tmp/ft891 &
build/ft891-cattest /tmp/ft891
```

`ft891-cattest` opens the port with the server's own controller and checks
the link, the start-up reads, tuning and coalescing, modes, band keys,
two-parameter settings, the refusals (unknown value, reset without
confirmation, MOX outside the PTT path, TX typed in the terminal), menu reads,
the clarifier, PTT and meters, and CW through the keyer memory.

The simulator follows the manual, not the firmware: passing against it says
the engine does what it was designed to do, nothing about the radio.

## Recognition of the FT-891's serial ports

```sh
build/ft891-portstest
```

Checks, on port descriptions as Windows and Linux report them, that a CP2105
is not called an FT-891 until the radio has answered on it — an SCU-17 uses
the same chip — that the Enhanced and Standard ports are told apart (by their
description on Windows, by their USB interface number on Linux), that an
FT-891 and an SCU-17 plugged in together keep their own labels, and that the
lists come in natural order.

## Field test on the real radio

```sh
ft891-fieldtest /dev/ttyUSB0 38400                 # no transmission
ft891-fieldtest /dev/ttyUSB0 38400 --power         # also switch the radio off and on
ft891-fieldtest /dev/ttyUSB0 38400 --tx            # also transmit (dummy load!)
ft891-fieldtest /dev/ttyUSB0 38400 --checklist     # then the checks outside CAT
ft891-fieldtest /dev/ttyUSB0 38400 --restore ft891-snapshot-….json
```

Stop the server first: the test opens the CAT port itself. On Windows the
port is `COM5` or the like.

Every check of the application on a real radio, in one guided run:

1. **Snapshot.** Read all (every setting, menu and memory, timed), FAST and
   the memory channel, the front-panel and function settings in each of the
   eight modes the test visits (the radio may keep them per mode), every band
   stack, and the VFOs, mode, clarifier and split. Saved to
   `ft891-snapshot-<date>.json` before anything is changed.
2. **Checks.** Identity and versions; FA, FB and the mode families; band keys
   BS00–BS12, BS02 refused, 60 m, BU, BD; SPLIT with ST and RIC, quick split;
   WIDTH by mode and NARROW, IF SHIFT; AGC AUTO answers; the real range of RF
   GAIN; the clarifier with menu 05-18, RU, RD, RC; the keyer — KM, KY2,
   typed text, a paddle message — in the sidetone only, BREAK-IN off; the
   memories MR, MW, MC, MA, AM, CH, V/M, and what the radio answers for an
   empty channel; FAST, AI, SCAN. What only the radio can show or sound is
   asked: answer y, n or s (skip).
3. **Options.** `--power`: PS off and on. `--tx`: PTT and power meter, TUNE,
   DVS playback, the keyer with BREAK-IN — after confirming a dummy load.
   `--checklist`: seventeen checks outside CAT (client, audio, safety,
   WSJT-X, Windows, Android), asked one by one. `--mem CH`: the memory channel
   the test may write (default: the last empty one). `--batch`: ask nothing.
4. **Restore**, also after an error or Ctrl+C: band stacks, the settings of
   each mode, menus, keyer memories, clarifier, split, the test's memory
   channel, VFO or memory mode. Then everything is read again and compared
   with the snapshot. If the program was stopped hard, `--restore FILE`
   replays the restore.

The report, `ft891-fieldtest-<date>.md`, is written as the test goes. Its
"To do by hand" part lists what CAT cannot undo: a memory channel that was
empty keeps the test's data (no CAT command clears a memory), and only the
first register of each band stack is noted. The CAT RATE menu (05-06) and
the reset (17-01) are never touched.

## rigctld port

```sh
build/ft891-rigctldtest
```

Speaks to the client's rigctld port as fldigi does — the VFO named in every
command, PTT 3 for data — and as a plain client does, and checks the answers
and what is actually requested of the radio.

## Frequency parser

```sh
build/ft891-freqtest
```

The forms an operator types — 14.074, 14074, 14074000, "7100 kHz" — and the
radio's grouped form the client shows, 7.100.000, which the memory editor
writes back.

## Client pages

```sh
sudo apt install qml6-module-qttest     # once
QT_QPA_PLATFORM=offscreen qmltestrunner -import test/qml/mock \
    -input test/qml/tst_settingrow_text.qml
```

Runs the client's setting rows against a stand-in for the C++ bridge
(`test/qml/mock`). The text settings — the CW keyer memories — must send what
was typed, with Set as with Enter, keep the typing when the radio's value is
read meanwhile, and show the radio's value again when left without sending.

## Against the radio

The same program runs against a real FT-891:

```sh
build/ft891-cattest /dev/ttyUSB0 38400            # keys the radio: dummy load!
build/ft891-cattest /dev/ttyUSB0 38400 --no-tx    # everything but transmission
build/ft891-cattest /dev/ttyUSB0 38400 --verbose  # every frame in and out
```

Its "Read all" section times the reading of every setting and lists the ones
that got no value from the radio — refused or unanswered commands, if any.

It is the quickest way to check a radio, or a new firmware, against the
description. It changes settings (AGC, IF shift, beep level, clarifier,
frequency, mode): note yours first.

## The whole chain

Simulator, headless server and client on one machine:

```sh
python3 test/ft891_sim.py --link /tmp/ft891 &
ft891remote-server --headless --config station.ini &   # catPort=/tmp/ft891
ft891remote                                           # connect to 127.0.0.1
```

`FT891_SCREENSHOT=out.png FT891_SIZE=400x800 FT891_TAB=3 ft891remote` grabs
the client's window after a few seconds and quits, to check a layout on a
machine without a display (`QT_QPA_PLATFORM=offscreen`).
