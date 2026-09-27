# Troubleshooting

## The radio does not answer

- Is the CAT port the **Enhanced** COM port of the FT-891? The Standard one
  does not carry CAT.
- Does the server's **Speed** match menu **05-06 CAT RATE**?
- Is another program holding the port — a test program, a logging program,
  a second server?
- On Linux, is your user in the `dialout` group (log in again after adding
  it)?
- Turn on **Log every CAT frame (diagnostics)** on the server's Radio tab:
  the log shows every frame sent and every answer.

## An SCU-17 is connected too

The FT-891 and the SCU-17 use the same USB chip with the same identifiers.
Until the radio has answered, the server shows both as "CP2105" ports with
their serial number and selects none: pick the Enhanced port of the FT-891.
Once it has answered, it is recognised by its serial number from then on.

## "The radio refused …" or "?;"

The FT-891 refuses a command that does not apply in its current state — an
FM setting outside FM, a WIDTH in AM, a keyer memory outside CW. The client
offers only what applies, but a setting changed on the radio itself can
make a row temporarily inapplicable.

## No audio, or garbled device names

- The server's input and output should be the FT-891's **USB Audio CODEC**.
- **Look for devices again** after plugging the radio in.
- Device names with accents are shown correctly from version 0.1.27 on; a
  device saved under its garbled name by an earlier version is still found.

## Transmission refused: OUT OF BAND

The band-edge guard refuses to transmit outside the amateur bands of the
server's IARU region, judging the emitted spectrum — the frequency, the
sideband, split and clarifier. Check the region on the server's Network tab.

## CW memories do not play

- The radio must be in CW, with the **KEYER** on; with **BK-IN** off, the
  keyer plays in the sidetone only.
- A memory set to **TEXT** (menus 04-07 to 04-11) plays with `KY6`–`KYA`, a
  **MESSAGE** memory with `KY1`–`KY5`; the client chooses by the memory's
  type, which it reads from the radio.

## The memory list does not match the radio

Press **READ** on the MEMORY page. Memories are read one at a time, each
answer filed under the channel asked for. A channel shown "…" could not be
read; the server's log says why.

## fldigi or WSJT-X cannot change frequency or transmit

- Enable the **rigctld interface** in the client's SETUP page.
- Point the program at `127.0.0.1:4532` with **Hamlib NET rigctl**.
- See [Digital Modes](Digital-Modes).

## Windows build: "Failed to find required Qt component Quick"

A Qt installed in vcpkg for another project was found instead of the one at
`QT_DIR`. Current versions of `build_all.bat` name the right Qt explicitly;
run `build_all.bat /clean` once.

## The client shows DISCONNECT but is not connected

Fixed in 0.1.21: a disconnection asked for is now shown like a lost one, and
**Connect** works again afterwards.
