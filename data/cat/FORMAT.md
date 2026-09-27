# CAT description format

`ft891.json` describes every command FT891Remote can send to the radio. It
contains no code: a setting added here appears in the client's pages with the
right control, and the server accepts it, without recompiling anything but the
embedded resource. The format is RemoteRig's, with a few additions marked
**(FT891Remote)** below.

```json
{
  "rig": "Yaesu FT-891",
  "protocol": "ascii-semicolon",
  "identify": "0650",
  "verified": true,
  "source": "FT-891 CAT Operation Reference Book, 1909-C; …",
  "groups": [
    {
      "name": "FRONT PANEL",
      "poll": "panel",
      "commands": [
        {
          "code": "PWR",
          "name": "Transmit power",
          "set": "PC{p1};",
          "read": "PC;",
          "params": [
            { "id": "p1", "name": "Power", "type": "range",
              "min": 5, "max": 100, "digits": 3, "unit": "W" }
          ],
          "verified": true,
          "tried": "checked against a real FT-891 over CAT at 38400 baud"
        }
      ]
    }
  ]
}
```

## Commands

`code` is the identifier the client and the server use; it is not the frame.
`set` and `read` are templates: `{p1}` is replaced by the formatted parameter,
everything else is copied as written, terminator included. A command with no
`set` is read-only; one with no `read` is an action (A=B, a memory recall) and
is never polled.

The answer to a read is recognised by the part of the **set** template before
the first parameter. The FT-891 answers `AC;` with `AC001;`: the tuner state is
the last digit, after the `AC00` that the set template `AC00{p1};` spells out.
Taking only the read prefix `AC` would read `0` instead.

`verified` says whether the command was tried on a radio, and the optional
`tried` how; the file-level flag says whether every command was. In this
description every command has been.

**(FT891Remote)** `answer` is the start of the answer, when it is not that of
the set template: SPLIT is set with `ST{p1};` but read with `RIC;`, which the
radio answers `RIC1;` — so `"answer": "RIC"`.

**(FT891Remote)** `companion` names a setting that belongs to this switch —
the level or frequency of the function it turns on (`"PRC"` has
`"companion": "PRC-LVL"`). The client shows both in one row: the switch and
the setting's value in the title line, the setting's control below. The
companion keeps its own command; it only has no row of its own.

**(FT891Remote)** `confirm` is a question the client asks before sending the
command, and without which the server refuses it: a reset, a CAT-rate change,
switching the radio off.

## Parameter types

- `range` — a number: `min`, `max`, `digits` (zero-padded width), optional
  `unit` and `step`. `PC005;` needs three digits: the width is not cosmetic,
  the radio rejects anything else. With `"signed": true` the sign takes one of
  those characters: `-25 … +25` in three gives `-25`, `+00`, `+25`.
- `enum` — a fixed list: `"values": [{ "v": "0", "label": "OFF" }, …]`. The
  `v` string is inserted verbatim, so it carries its own width. The server
  refuses a value that is not listed rather than sending the first one.
  **(FT891Remote)** A value the radio answers but does not accept carries
  `"readonly": true` and `"as"`, the settable value it stands for: AGC is set
  to AUTO (4) and answers AUTO-FAST, AUTO-MID or AUTO-SLOW (4, 5, 6). It is
  shown, never offered in a list, and refused as a setting.
- `text` — free characters, `maxLength` at most. Used by the keyer memories;
  the semicolon can never pass.

**(FT891Remote)** A range may carry `display`, for steps whose meaning is not
the number sent: PITCH `00` is 300 Hz and each step adds 10 Hz.

```json
"display": { "offset": 300, "scale": 10, "decimals": 0, "unit": "Hz", "zero": "OFF" }
```

The value sent is always the raw step; `zero`, when present, is shown for the
minimum.

## Groups (FT891Remote)

`poll` tells the server how to keep a group up to date:

- `core` — frequency, mode, TX: read by the server on its own schedule.
- `panel` — what the front panel shows: read at start-up, then one setting per
  poll cycle, so that a knob turned on the radio itself is seen.
- `meter` — meters: read in every cycle, never stored as settings.
- `menu` — read when the operator opens the page, or on "Read all".

`hidden` groups have no page of their own in the client.

## Menus

A menu entry is an ordinary command whose template carries the menu number:

```json
{ "code": "EX0506", "name": "05-06 CAT RATE", "set": "EX0506{p1};",
  "read": "EX0506;",
  "params": [ { "id": "p1", "type": "enum",
                "values": [ { "v": "0", "label": "4800" }, … ] } ] }
```

Nothing in the engine knows what a menu is — which is the point.
