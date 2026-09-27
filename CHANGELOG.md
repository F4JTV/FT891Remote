# Changelog

Every fix and every feature moves the version: `./bump_version.sh patch|minor|major "what changed"`
updates CMakeLists.txt, this file, the Debian changelog and the AppStream release list together.

## 0.1.25 — 2026-09-27

- Every command of the description has now been tried on a real FT-891:
  all are marked verified, the notes on how each was tried are gone, and the
  client no longer shows an "untested" label.
- README: the CAT description section lists what was checked and where the
  radio differs from its CAT reference (RF GAIN range, the ON/OFF digit of
  IF SHIFT and WIDTH, KY for TEXT and MESSAGE memories, the channel field of
  MR answers, band keys), instead of the commands not yet tried. FORMAT.md
  and test/README.md follow.

## 0.1.24 — 2026-09-26

- Client: the IPO badge of the display was lit with AMP and dark with IPO.
  It was lit when PA0 was "on" (1), as the other badges are, but PA0's
  values are 0 IPO and 1 AMP. It is now lit when IPO is selected, as on the
  radio. The other badges were checked: their values are OFF and ON.

## 0.1.23 — 2026-09-26

- Android: the client opens full screen, as RemoteRig does. The activity
  hid the system bars, but the QML window was not declared full screen, and
  Qt showed the bars again for it: the navigation bar stayed at the bottom.
  The window is now Window.FullScreen on Android (unchanged on the desktop),
  and the room kept above the header for the status bar — an empty band once
  the bar is hidden — is gone.

## 0.1.22 — 2026-09-26

- Server: memories were shown under the wrong channel (0.1.20) or left
  unknown (0.1.21). A CAT trace from the radio shows why: the FT-891 does not
  write the channel read in the channel field of its MR answer, but its
  current memory channel — MR002 is answered "MR001007100100…", the fields of
  channel 002, when the radio stands on 001. Filed by that field, every
  memory landed on the current channel, the last one read overwriting the
  others; 0.1.21, which required the field to match, refused every answer
  but the current channel's. The answer now belongs to the read in progress
  (memories are read one at a time) and is filed under the channel asked
  for, its channel field corrected. After a read given up, a pause lets a
  late answer arrive while no read is in progress; it is then ignored.
  Polling between memory reads does not disturb them: in the trace the same
  read gave the same, right answer three times, with polls in between.
- Simulator: MR answers carry the current memory channel, as the radio's
  do.

## 0.1.21 — 2026-09-26

- Client: after Disconnect, the client went on showing the link up — the
  button stayed on DISCONNECT and pressing it only disconnected again, with
  no way back to Connect. A wanted disconnection closed the link without
  announcing it; it is now announced like a lost one. (The same function in
  RemoteRig's client does the same; its window updated its button itself.)
- Server: memories could be shown under the wrong channel, or not at all.
  They were read like settings, many requests queued with the usual 400 ms
  wait; an answer arriving later was taken for the answer to the next read,
  and a slow answer left its channel unknown. They are now read one at a
  time, each with a wait of 1.5 s, and an answer is kept only if it names the
  channel asked for; otherwise the read is made again, twice at most, and the
  channel is left unknown ("…") rather than filled with another's content.
  Failures are logged; with "Log every CAT frame" each retry is too.
- Client, FUNCTION-2: the memory channel up and down keys are removed.
- Serial link: a request can carry its own answer timeout.
- Simulator: --slow-memory N answers the first N memory reads late;
  ft891-cattest checks that every memory lands under its own channel, with a
  normal and with a slow simulator.

## 0.1.20 — 2026-09-26

- rigctld: with fldigi, frequency changes and transmission never reached the
  radio. fldigi's Hamlib names the VFO in every command — "F VFOA 7030000",
  "T VFOA 3" — and the port read "VFOA" as the value: every frequency became
  0 and was dropped, every PTT became "off", and both were answered RPRT 0,
  so fldigi believed them done. The VFO name is now taken out before the
  values are read (VFOA, VFOB, currVFO, Main, Sub…). Frequency and mode act
  on the receive VFO, A, whichever VFO is named: fldigi's Hamlib leaves its
  current VFO on B after probing both at start-up. Split still sets VFO-B
  (I, i). PTT accepts 1 to 3 (fldigi sends 3, data); \set_vfo_opt is
  accepted; an invalid frequency or PTT value is answered RPRT -1 instead of
  RPRT 0. Reproduced with fldigi 4.2.03 and Hamlib 4.5.5: frequency and
  transmit now reach the radio in 0.2 s.
- Tests: ft891-rigctldtest speaks to the port as fldigi and WSJT-X do.

## 0.1.19 — 2026-09-26

- Client: a memory could not be written: "Not a frequency the FT-891
  covers". The editor fills its field in the radio's grouped form,
  7.100.000, which the frequency parser read as a decimal and refused (two
  dots). The parser now reads two dots or more, each followed by three
  digits, as hertz; the frequency entry accepts the same form. It moved to
  the common code (ft891::parseFrequency).
- Tests: ft891-freqtest checks the parser on the forms typed and shown.

## 0.1.18 — 2026-09-26

- Client: the CAT tab is removed. Every CAT command of the radio now has its
  control in the pages, and raw frames from the client are no longer needed.
  Gone with it: the client's CAT log and raw-frame sending, the server's
  "Let the client send raw CAT frames" option (its settings key is erased at
  the next save) and the network command that carried the frames. The CAT
  test line of the server window stays: a local diagnostic, like the test
  programs.

## 0.1.17 — 2026-09-26

- Client, CW page: APF and its frequency offset in one section, like the
  functions of the FUNCTION pages. The CW page did not use the companion
  filter of the other pages; the filter is now in the bridge
  (withoutCompanions), and every page uses it.

## 0.1.16 — 2026-09-26

- Client: the switch of a function and its setting are one section, as IF
  SHIFT and WIDTH are: PRC and its level, MON and its level, NB and its
  level, NCH and its frequency, DNR and its algorithm, CNT and its frequency.
  The switch and the setting's value are in the title line, the setting's
  slider below; the setting has no row of its own any more.
- Description format: `companion` names the setting shown with a switch
  (documented in FORMAT.md). The two commands stay separate on the CAT side.

## 0.1.15 — 2026-09-26

- CW: the TEXT keyer memories, and typed text, did not play. 0.1.11 had
  switched their playback to KY1–KY5, taking the reference's name for them,
  "Keyer Memory playback", to mean the memories KM writes. The radio refuses
  KY1–KY5 for a TEXT memory with "?": text is played by KY6–KYA ("Message
  Keyer"), as Hamlib and RemoteRig did and FT891Remote up to 0.1.10. Typed
  text goes back to KM1 then KY6; the memory keys play KY6–KYA for a TEXT
  memory and KY1–KY5 for a MESSAGE one — the latter by elimination, still to
  be tried on the radio.
- Simulator: KY1–KY5 refuse a TEXT memory, as the radio does; ft891-cattest
  checks it.

## 0.1.14 — 2026-09-26

- Client: the MONITOR, IF NOTCH, CONTOUR and APF switches did nothing. A
  switch sent a bare "1" or "0", while these commands take 000/001 or
  0000/0001 (ML0001, BP00001, CO000001, CO020001); the server refused the
  value as not in the list. Every switch — in the title line, in an editor,
  and the toggle keys — now sends the command's own values.
- IF SHIFT and WIDTH: back to the format of RemoteRig's description, checked
  on a radio: an ON/OFF digit, then the value (IS01+0500, SH0114). 0.1.10 had
  removed that digit after the CAT reference 1609-A0, which does not show it;
  the radio then ignored every setting. Both can be switched on and off in
  their title line; IF SHIFT runs ±1200 Hz in 20 Hz steps; the WIDTH steps
  keep the table by mode and NARROW, and WIDTH off is the mode's default
  width.
- Memory channel up/down: CH only moves the cursor of the radio's memory
  list without selecting. The keys now go to the next or previous used
  channel with MC, which selects it (memories are read first if they never
  were). The channel's name travels in the state (P1L…), since a PMS channel
  has no number; the display showed "MEM 0" for one.
- CW memories: each memory is played by the command of its type in menus
  04-07 to 04-11 — KY1–KY5 for TEXT ("Keyer Memory" playback), KY6–KYA for
  MESSAGE ("Message Keyer" playback) — with one ▶ per memory showing its
  type. The types are read at start-up. The radio's end-of-text mark "}" is
  no longer shown as text.
- BK-IN delay moves in 10 ms steps (menu 07-09), and so does menu 16-21.
- The SPLIT note is corrected: ST is in revision 1909-C of the reference.
- Tests: ft891-cattest and ft891-fieldtest follow the formats; memory up/down
  and playback by memory type are checked (cattest 68, fieldtest 76, against
  the simulator).

## 0.1.13 — 2026-09-25

- Tests: `ft891-fieldtest`, a guided test on the real radio of everything
  not yet tried on one. It saves the radio first — every setting, menu and
  memory, the settings of each mode it visits, every band stack, VFOs, mode,
  clarifier and split — to a JSON file, runs the checks (CAT checks
  automatic, what only the radio shows asked of the operator; transmission
  with --tx, power switching with --power, the checks outside CAT with
  --checklist), then puts everything back, also after an error or Ctrl+C,
  reads it all again and compares. --restore replays a saved snapshot. The
  report is a Markdown file written as the test goes.
- Simulator: MA, AM, the VFO kept aside in memory mode, channel up/down
  round the list, DVS playback on the air, and spaces kept in frames (they
  are data in a keyer memory); its memory write format had one character too
  many.

## 0.1.12 — 2026-09-25

- Client: a MEMORY page. The radio's memories — 001 to 099 and the PMS
  pairs P1L to P9U — are listed with their frequency, mode, clarifier, CTCSS
  and shift; empty channels can be hidden. A memory is edited in a dialog,
  filled from VFO-A if wanted, written to the radio (after a confirmation
  when it is not empty) and read back, or recalled. Over CAT a memory holds
  neither its CTCSS tone frequency nor its name: the page says so.
- Server: memories read with MR, written with MW, recalled with MC, all in
  the format of the CAT reference. Read all reads them after the settings.
  An empty channel, which the radio refuses, is an empty value, not a
  refusal notice per channel. They travel with the settings (MEM:<channel>),
  so a client that connects receives them at once.
- Client: the page tabs scroll to show the current one, however it was
  chosen.
- Simulator and tests: memories in the simulator; `ft891-cattest` checks
  Read all with the 117 memories, MR, an empty channel, MW with its
  read-back, a refused frequency, and MC (66 checks).

## 0.1.11 — 2026-09-25

- The CAT description was checked, command by command, against the FT-891
  CAT Operation Reference Book (1909-C). The 159 menu entries all match the
  reference's table. Outside the menu:
  - IF: P4 is CLAR on/off and P5 is fixed; they had been read as "clarifier
    on receive" and "on transmit". The side now comes from menu 05-18 CLAR
    SELECT, read at start-up — the band-edge guard ignored a clarifier
    shifting the transmit frequency.
  - Keyer: KY1–KY5 play the text memories KM writes, KY6–KYA the messages
    recorded with the paddle. Typed CW and the memory keys used KY6–KYA;
    they now use KY1–KY5, and the CW page plays paddle messages too.
  - Band keys: BS02 is not used and BS12 is the MW band. 60 m, which has no
    band-stack key, goes to its preset frequency; MW was added.
  - AGC: set to AUTO, the radio answers AUTO-FAST, AUTO-MID or AUTO-SLOW
    (4, 5, 6); 5 and 6 were shown as bare numbers. They are shown by name,
    never offered and refused as settings ("readonly" values).
  - TX: 2 means the radio's own PTT, an answer only; the labels were wrong.
  - SPLIT: ST is not in the reference. The split state is now read through
    RI (SPLIT indicator), as documented; ST is kept to switch it, marked
    untried, and its undocumented "ON +5 kHz" value was removed. Quick split
    (QS) was added.
  - Added from the reference: memory channel up/down (CH) and scan (SC).
  - RF GAIN keeps 0–30, checked on a radio, where the reference says
    000–100; the note says so.
  - The versions of menu 18 are shown as the reference writes them, V01-23.
- Description format: `answer` for a read answered with another prefix, and
  `readonly`/`as` for values the radio answers but does not accept.
- Simulator and tests follow the reference; `ft891-cattest` checks AGC
  answers, the clarifier side, split through RI, quick split, the MW and
  60 m bands, and paddle messages (60 checks).

## 0.1.10 — 2026-09-25

- WIDTH (SH) follows the table of the FT-891 CAT reference. The steps that
  exist depend on the mode and on NARROW — SSB 09–21 (01–09 narrow), CW, RTTY
  and DATA 10–17 (01–10 narrow), none in AM and FM — and step 00 is the
  mode's default width. The FUNCTION-1 slider offers only those steps and
  shows each one's bandwidth; while the radio is on step 00 it stands at the
  step with the same bandwidth, marked "(default)". The server refuses a step
  the current mode does not have, with the reason. The table used before came
  from memory of Hamlib's, and ignored NARROW.
- WIDTH and IF SHIFT were sent in a format the FT-891 does not have. The
  description taken from RemoteRig gave both an ON/OFF digit (SH0 1 14;,
  IS0 1 -0500;); the reference has none (SH0 14;, IS0 -0500;). IF SHIFT's
  range is ±1000 Hz, not ±1200. Both are marked untried until checked on a
  radio.
- Client: a setting's value in its title line, and the WIDTH shown on the
  display, follow the mode: the same step is another bandwidth in another
  mode. The slider's label shows the radio's value at rest, and the step
  being chosen while it is dragged; a step outside the current table is shown
  as a step number, not as a bandwidth it does not have.
- Client: the SHIFT indication on the display shows when the IF shift is not
  zero; with the ON/OFF digit gone, it used to show all the time.
- Tests: `ft891-cattest` checks WIDTH by mode and NARROW, and the new IF
  SHIFT format; Read all no longer counts the repeater shift as missing
  outside FM, where the radio does not answer it.

## 0.1.9 — 2026-09-25

- Server and client: the three versions of menu 18 (MAIN, DSP, LCD) were
  never read, by Read all or otherwise, and their page showed only their
  names. They are read-only commands without parameters, and both sides
  only read and showed commands with parameters. They are now read with the
  rest and shown as the radio gives them.
- Tests: `ft891-cattest` measures Read all — how many settings, how long,
  and which ones, if any, got no value from the radio. Against the simulator:
  210 settings in 2.1 s at 8 ms per answer, 10.9 s at 25 ms, all read.

## 0.1.8 — 2026-09-25

- Client: the MODE key offers the six families of the radio's own MODE
  key — SSB, CW, AM, FM, RTTY, DATA — instead of the thirteen CAT modes.
  The mode displayed, on the client and on the server, is always the one the
  radio reports (LSB, USB, CW-U…).
- Server: CAT cannot ask for "SSB" without a sideband, so the server applies
  the radio's own rule: LSB below 10 MHz, USB from 10 MHz up and on 60 m.
  For the other families it chooses the variant the radio last used in that
  family (CW-L, DATA-L, AM-N…), as the MODE key does, or CW-U, AM, FM, RTTY-L
  and DATA-U the first time. A family the radio is already in is left as it
  is. rigctld clients still set exact modes.
- Server: after every mode change, MD0 is read back at once, then IF and the
  filter width once the radio has settled.
- Client: the filter width in hertz follows the mode. The same WIDTH step is
  another bandwidth in another mode, and the display kept the old one when the
  step read back after a mode change happened to be the same.
- Tests: `ft891-cattest` checks the families against the simulator.

## 0.1.7 — 2026-09-25

- Client: a keyer memory (KM1 to KM5) changed and sent with Set came back
  to its old text, because the old text was what had been sent. The field
  shows the radio's value whenever it does not have the focus; pressing Set
  took the focus, the old text was put back, and Set then read it. Set no
  longer takes the focus and the text is read before the field lets go of
  it. Enter was not affected. Leaving the field without Set or Enter now
  shows the radio's value again, rather than an unsent text.
- Tests: `test/qml/tst_settingrow_text.qml` checks text settings with
  qmltestrunner; `ft891-cattest` writes and reads back keyer memory 2.

## 0.1.6 — 2026-09-25

- Server: 0.1.5 called every Silicon Labs CP2105 an FT-891 and could
  preselect its ports. Yaesu's SCU-17 uses the same chip, with the same USB
  identifiers and the same descriptions: its ports could be taken for CAT,
  and its Standard port — whose RTS and DTR key another radio — for the
  local keyer. Nothing in USB tells the two apart, and probing the ports is
  not safe (opening a port raises DTR on Windows). A CP2105 is now shown as
  "CP2105 Enhanced/Standard COM · SN …", or "SCU-17" when its driver says
  so, and is never selected on its own. The FT-891 is recognised once it has
  answered ID0650 on a port: the server then remembers its USB serial number,
  labels its two ports "FT-891", follows it to another COM number, and
  offers its Standard port for the keyer.

## 0.1.5 — 2026-09-25

- Server: the Key port list stayed empty at start-up. The ports were scanned
  while the Radio tab was being built, before the CW tab had created its
  list; they are now scanned once every tab exists.
- Server: the port lists show what each port is. The FT-891's two ports are
  recognised — "COM5 — FT-891 Enhanced COM (CAT)", "COM6 — FT-891 Standard
  COM (PTT, keying)" — from their description on Windows and from the USB
  interface number in sysfs on Linux, where both carry the same description.
  The radio comes first, then other USB adapters, then built-in ports.
- Server: the saved port is selected in the list rather than typed into it.
  The CAT port follows the FT-891 when Windows gives it another COM number;
  the Key port defaults to the Standard port. A saved port that is missing
  stays shown, marked "not found"; a path such as /dev/serial/by-id/… is kept
  as it is.
- Server: a Rescan button on the CW tab too; `--list-ports` prints the same
  labels.
- Server: stopping the server during a CW message could leave the radio's
  keyer switched off. A message is stopped by switching the keyer off and, 80
  ms later, back on; when the port closed within those 80 ms, the second
  command was lost. It is now sent before the port closes. Found by running
  `ft891-cattest` twice against the same simulator.
- Tests: `ft891-portstest` checks the recognition on port descriptions as
  Windows and Linux report them.

## 0.1.4 — 2026-09-25

- Server: the explanations under the fields were nearly invisible in a dark
  theme. They were drawn in the palette's "mid" grey, readable on a light
  background and dark on a dark one; they now keep the theme's own text
  colour.
- Server: CM108 GPIO PTT removed, with its udev rule — the FT-891 keys over
  CAT or RTS/DTR through its own USB port. A saved CM108 setting falls back to
  CAT PTT.
- Server: the PTT tone on the right audio channel removed from the Audio tab.
- The keys these settings used are erased from the settings file at the next
  save.

## 0.1.3 — 2026-09-24

- Client, Panel page: every key has the same height (48 px), whether it has
  a second line or not, and the tuning-step list matches the keys beside it.
  Keys used to be 40 or 48 px depending on their second line, and Material
  added insets that made the list a third taller.
- Client: the re-read button of every setting showed an empty box on Android,
  whose fonts have no glyph for U+27F3, and the tuning arrows (U+25C0,
  U+25B6) were drawn as orange emoji. Icons are now drawn with a Canvas, the
  same on every system; the CAT terminal marks its lines with > and <.
- Client, Setup page: fields went past the right edge of a phone. A
  ColumnLayout never lays out narrower than its widest child, and several
  children had a fixed width — a long check box, a row of three buttons, a
  label column of 130 px, the port fields. Labels now take their own width,
  fields fill the rest, check boxes wrap, rows of buttons flow onto a second
  line, and the page keeps a 16 px margin on each side.
- Android: the application stays in portrait.

## 0.1.2 — 2026-09-24

- Linux: `make_deb.sh` builds a Debian package for the machine it runs on
  (Ubuntu 24.04 amd64, Raspberry Pi arm64/armhf), with `--server-only`,
  `--client-only`, `--deps` and a lintian check. The package now carries
  manual pages, the compressed changelog, the copyright file and AppStream
  metadata; lintian reports nothing.
- Linux: a client-only build no longer requires the Qt serial port module.
- Windows: `build_all.bat /deps` installs vcpkg, PortAudio and Opus when
  missing. The script checks that the QML modules were deployed, and bundles
  the Visual C++ runtime, which the installer now runs: on a fresh Windows
  both programs used to stop on a missing MSVCP140.dll. The installer script
  is saved as UTF-8 with a byte-order mark, so that its French text is read
  correctly.
- Android: `build_android.sh --setup` installs the whole toolchain on Ubuntu
  24.04 (JDK, git, Qt for Android and desktop through aqtinstall, SDK, NDK,
  platform-tools, adb udev rules, signing key). The script now checks for git
  and for a full JDK 17 or later, and uses the SDK's own adb.

## 0.1.1 — 2026-09-24

- Client: a second row of keys (CAT quick frames, long choice lists) was
  hidden under the next control. A Flow does not report its second row to the
  layout it sits in; it is now wrapped in an item that does.
- Client: setting values were cut short in their title line ("10 …" for
  10 Hz). Their width hint was computed while the row was still 73 px wide,
  and Qt 6.4's RowLayout did not apply the corrected hint once the row had its
  real width. The value is now sized through its implicit width with a fixed
  cap; text settings no longer repeat their value in the title line.
- Client: in the CAT terminal and the log, a short text sat at the bottom of
  the view. They are now lists of lines that scroll to their end.

## 0.1.0 — 2026-09-24

First release, derived from RemoteRig 1.17.1.

- Server: native FT-891 CAT engine without Hamlib — answers matched by
  prefix, prioritised and coalescing queues, polling of frequency, mode, TX,
  meters and front-panel settings, background reads of the menus, detection
  of a radio switched off, reopening of a lost serial port.
- Server: PTT through CAT, RTS, DTR (CAT port or Standard COM port), CM108
  GPIO (Linux) or none; band-edge guard by IARU region, split and clarifier
  included; confirmation required for reset, CAT rate and power-off; TX and MX
  refused from the raw terminal.
- Server: CW text sent by the radio's keyer through keyer memory 1, in
  fifty-character pieces; the local serial keyer of RemoteRig kept as an
  alternative.
- Server: one wiring class and one settings loader shared by the window and
  the headless daemon; `--list-ports`.
- Client: a single Qt Quick front panel for desktop and Android — LCD display
  and meters, knob-ready relative tuning, front-panel keys, level controls,
  pages F-1, F-2, CW, FM, MENU 01–18, CAT terminal, setup and log; every
  setting drawn from the CAT description.
- Client: rigctld-compatible port with split; PTT on the space bar or the
  volume-down key.
- CAT description extended: core commands (frequency, mode, band keys, VFO
  and memory keys, clarifier steps, power), keyer memories, voice memory
  playback; poll classes, confirmations and display scales. Commands not yet
  tried on a radio are flagged.
- Fixed while porting: RemoteRig's CAT reader stripped the read prefix, so
  "AC;" answered with "AC001;" was split as "0" and the tuner state was
  misread. Answers are now matched by the set template's prefix.
- Tests: an FT-891 simulator on a pseudo-terminal, and a harness that drives
  the CAT engine against it or against the radio.
