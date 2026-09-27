# Testing

Build the test programs with `-DFT891_BUILD_TESTS=ON`.

## The simulator

`test/ft891_sim.py` opens a pseudo-terminal that behaves like an FT-891 on
CAT — including its oddities (the `MR` channel field, `?;` for a TEXT memory
played with `KY1`, the tuner answering 2 while it tunes…).

```sh
python3 test/ft891_sim.py --link /tmp/ft891 &
```

| Option | Effect |
|---|---|
| `--link PATH` | a symlink to the pseudo-terminal at this path |
| `-v`, `--verbose` | print every frame |
| `--latency S` | answer delay (8 ms by default) |
| `--slow-memory N` | answer the first N memory reads late, after 1.8 s |

The server and the client run against it as against the radio: give the
server `/tmp/ft891` as its CAT port.

## ft891-cattest

Drives the server's own CAT engine against a serial port and checks what
comes back — about seventy checks: frequency and modes, band keys, split,
WIDTH and IF SHIFT by mode, AGC, clarifier, keyer, memories, Read all, a
tuning cycle, transmission.

```sh
build/ft891-cattest /tmp/ft891                     # the simulator
build/ft891-cattest /dev/ttyUSB0 38400 --no-tx     # a radio, without transmitting
build/ft891-cattest /dev/ttyUSB0 38400             # keys the radio: dummy load!
build/ft891-cattest /dev/ttyUSB0 38400 --verbose   # every frame in and out
```

Against a radio it changes settings: note yours first — or use
`ft891-fieldtest`, which puts them back.

## ft891-fieldtest

A guided test on a real radio. It saves the radio first — every setting and
menu, the settings of each mode it visits, every memory and band stack, the
VFOs, mode, clarifier and split — to a JSON file, runs the checks (automatic
where CAT can measure, a question to the operator where only the radio's
display or sound can tell), then **puts everything back**, even after an
error or Ctrl+C, reads it all again and compares.

```sh
ft891-fieldtest /dev/ttyUSB0 38400                 # no transmission
ft891-fieldtest /dev/ttyUSB0 38400 --power         # also switch the radio off and on
ft891-fieldtest /dev/ttyUSB0 38400 --tx            # also transmit (dummy load!)
ft891-fieldtest /dev/ttyUSB0 38400 --checklist     # then the checks outside CAT
ft891-fieldtest /dev/ttyUSB0 38400 --restore ft891-snapshot-<date>.json
```

`--mem CH` chooses the memory channel the test may write (by default the
last empty one; no CAT command clears a memory, so an empty one keeps the
test's data), `--batch` asks nothing. The report, a Markdown file, is written
as the test goes. Stop the server first: the test opens the CAT port itself.

## The other test programs

| Program | What it checks |
|---|---|
| `ft891-rigctldtest` | the client's rigctld port, spoken to as fldigi (VFO names, PTT 3) and as a plain client |
| `ft891-freqtest` | the frequency parser, forms typed and forms shown (`7.100.000`) |
| `ft891-portstest` | the recognition of the FT-891's serial ports, next to an SCU-17 |

And the QML tests of the setting rows:

```sh
QT_QPA_PLATFORM=offscreen qmltestrunner -import test/qml/mock \
    -input test/qml/tst_settingrow_text.qml
```
