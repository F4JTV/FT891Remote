# CAT Description

Every setting FT891Remote shows lives in one file,
[`data/cat/ft891.json`](../blob/main/data/cat/ft891.json), embedded in both
programs. The client draws its pages from it, and the server checks every
value against it before anything reaches the radio. Its format is documented
in [`data/cat/FORMAT.md`](../blob/main/data/cat/FORMAT.md).

## What a command looks like

```json
{
  "code": "NB",
  "name": "NB — noise blanker",
  "set": "NB0{p1};",
  "read": "NB0;",
  "companion": "NB-LVL",
  "params": [
    { "id": "p1", "name": "NB", "type": "enum",
      "values": [ { "v": "0", "label": "OFF" }, { "v": "1", "label": "ON" } ] }
  ],
  "verified": true
}
```

- `set` and `read` are the CAT frames, with `{p1}`, `{p2}`… for the values.
- Parameters are an **enum** (a fixed list), a **range** (min, max, digits,
  step, sign, and how to display it), or **text** (a keyer memory).
- `companion` names the setting shown in the same section as a switch —
  here the noise blanker's level.
- `readonly` / `as` mark values the radio answers but does not accept (AGC
  AUTO-MID and AUTO-SLOW answer to AUTO).
- `answer` gives the start of an answer that differs from the set frame
  (SPLIT is set with `ST` and read through `RI`, answered `RIC1;`).
- `confirm` asks the operator before sending (reset, CAT rate, power-off).

The description holds 256 commands, among them the 159 MENU entries, each
checked against the **FT-891 CAT Operation Reference Book (1909-C)** and
tried on a real FT-891.

## Where the radio and the reference differ

Where they disagree, the radio wins:

| Subject | The reference | The radio |
|---|---|---|
| RF GAIN (`RG`) | 000–100 | 0–30 |
| IF SHIFT (`IS`), WIDTH (`SH`) | value only | an ON/OFF digit, then the value: `IS01+0500;`, `SH0114;` |
| Keyer memory playback (`KY`) | 1–5 "Keyer Memory", 6–A "Message Keyer" | a TEXT memory plays with `KY6`–`KYA` (`KY1`–`KY5` are refused with `?;`), a MESSAGE memory with `KY1`–`KY5` |
| Memory read (`MR`) | the answer names the channel read | the answer names the radio's **current** memory channel; the fields are those of the channel read |
| Band keys (`BS`) | — | `BS00` 160 m … `BS10` 6 m, `BS11` GEN, `BS12` MW; `BS02` is not a band; 60 m has no band-stack key |
| Memory channel up/down (`CH`) | — | moves the cursor of the radio's memory list without selecting; `MC` selects |
| AGC (`GT`) | set 0–4 | answers 4, 5 or 6 (AUTO-FAST, -MID, -SLOW) when set to AUTO |
| Tuner (`AC`) | — | answers 2 while an ATU cycle runs, then 1 |
| Empty keyer memory (`KM`) | — | answered `}`, the radio's end-of-text mark |

## WIDTH

The WIDTH steps, and the bandwidth each gives, follow the SH table of the
reference and depend on the mode and on NARROW:

| Mode | NARROW off | NARROW on |
|---|---|---|
| SSB | steps 09–21 | steps 01–09 |
| CW, RTTY, DATA | steps 10–17 | steps 01–10 |
| AM, FM | no WIDTH | no WIDTH |

The client offers only the steps the current mode has; the server refuses
the others with the reason.

## Memories

Memories are records, not settings: they are read with `MR`, written with
`MW` and recalled with `MC`, outside the description. Over CAT a memory has
its frequency, mode, clarifier, CTCSS on/off and repeater shift — neither
its tone frequency nor its name.
