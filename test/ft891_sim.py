#!/usr/bin/env python3
"""A simulated Yaesu FT-891 on a pseudo-terminal, for testing FT891Remote
without the radio.

    python3 test/ft891_sim.py            # prints the port to use, e.g. /dev/pts/5
    python3 test/ft891_sim.py --link /tmp/ft891   # also creates a symlink

It answers every command of data/cat/ft891.json from a table of values, and
simulates what the server relies on: IF, frequencies and modes, TX, meters,
band stack, VFO keys, clarifier, keyer memories, power switch. It is a model
of the CAT reference, not of the radio's firmware: behaviour it gets right is
worth checking, behaviour it gets wrong proves nothing about the real set.
"""
import argparse
import json
import os
import pty
import random
import re
import select
import sys
import termios
import time
import tty

HERE = os.path.dirname(os.path.abspath(__file__))
DESC = os.path.join(HERE, "..", "data", "cat", "ft891.json")

MODES_WITH_FM = {"4", "A", "B"}
BANDS = {  # BS number -> frequency the band stack returns to; 02 is unused
    "00": 1840000, "01": 3650000, "03": 7074000, "04": 10136000,
    "05": 14074000, "06": 18100000, "07": 21074000, "08": 24915000,
    "09": 28074000, "10": 50313000, "11": 15000000, "12": 1000000,
}


def param_width(p):
    if p["type"] == "enum":
        return len(p["values"][0]["v"]) if p.get("values") else 1
    if p["type"] == "text":
        return None
    return int(p.get("digits", 1))


def default_value(p):
    if p["type"] == "enum":
        return p["values"][0]["v"]
    if p["type"] == "text":
        return ""
    v = max(p.get("min", 0), 0) if p.get("min", 0) <= 0 <= p.get("max", 0) else p.get("min", 0)
    d = int(p.get("digits", 1))
    if p.get("signed"):
        return ("-" if v < 0 else "+") + str(abs(v)).zfill(d - 1)
    return str(v).zfill(d)


class Command:
    def __init__(self, c):
        self.code = c["code"]
        self.set = c.get("set")
        self.read = c.get("read")
        self.params = c.get("params", [])
        tmpl = self.set or self.read or ""
        brace = tmpl.find("{")
        if self.params and self.set and brace >= 0:
            self.prefix = tmpl[:brace]
        else:
            self.prefix = (self.read or self.set or "").rstrip(";")
        # The value is kept as the set frame gives it; the answer may start
        # differently ("answer" in the description: SPLIT is set with ST and
        # read with RI, answered RIC).
        self.answer = c.get("answer") or self.prefix
        self.set_re = None
        if self.set:
            pattern = re.escape(self.set.rstrip(";"))
            for p in self.params:
                w = param_width(p)
                token = re.escape("{%s}" % p["id"])
                if w is None:
                    pattern = pattern.replace(token, "(.*)")
                elif p["type"] == "range" and p.get("signed"):
                    pattern = pattern.replace(token, "([+-]\\d{%d})" % (w - 1))
                else:
                    pattern = pattern.replace(token, "(.{%d})" % w)
            self.set_re = re.compile("^" + pattern + "$")
        # The stored answer: what follows the prefix in the set frame.
        if self.params and self.set:
            frame = self.set.rstrip(";")
            for p in self.params:
                frame = frame.replace("{%s}" % p["id"], default_value(p))
            self.value = frame[len(self.prefix):]
        else:
            self.value = None

    def valid(self, groups):
        for p, g in zip(self.params, groups):
            if p["type"] == "enum" and g not in [v["v"] for v in p["values"]]:
                return False
            if p["type"] == "range":
                try:
                    n = int(g)
                except ValueError:
                    return False
                if n < p["min"] or n > p["max"]:
                    return False
        return True


class FT891:
    def __init__(self, verbose):
        self.verbose = verbose
        desc = json.load(open(DESC))
        self.commands = [Command(c) for g in desc["groups"] for c in g["commands"]]
        self.by_code = {c.code: c for c in self.commands}
        self.power = True
        self.wake_pending = False
        self.fa = 14074000
        self.fb = 14080000
        self.mode = "2"            # USB
        self.tx = False
        self.mem = 0               # P7: 0 VFO, 1 MEM
        self.vfo_saved = (self.fa, self.mode)
        self.channel = 1
        self.pms = None            # a PMS channel in use (P1L…), shown by name in IF
        self.slow_memory = 0       # --slow-memory: late answers to the first memory reads
        self.clar = 0
        self.rx_clar = False
        self.tx_clar = False
        self.keying_until = 0.0
        self.tune_until = 0.0
        # A few front-panel values that are not zero on a real set.
        self.store("AF", "AG0100")
        self.store("PWR", "PC100")
        self.store("MIC", "MG050")
        self.store("SPEED", "KS020")
        self.store("KEYER", "KR1")
        self.store("AGC", "GT04")
        self.store("EX0407", "EX04070")
        self.store("EX0506", "EX05063")
        self.store("KM1", "KM1CQ CQ DE N0CALL K")
        for n in range(2, 6):
            self.store("KM%d" % n, "KM%d}" % n)   # empty: the radio's end mark only
        # Memories, as MR gives them after "MR" and the channel: frequency,
        # clarifier (sign + 4), P4 clar on, P5 0, P6 mode, P7, P8 CTCSS, P9 00,
        # P10 shift. The other channels are empty.
        self.memories = {
            "001": "014074000+000000200000",
            "002": "007074000+000000100000",
            "003": "010136000+000000C00000",
            "010": "029600000+000000410002",
            "025": "050313000+001510200000",
            "P1L": "007000000+000000300000",
            "P1U": "007040000+000000300000",
        }

    def store(self, code, frame):
        c = self.by_code[code]
        c.value = frame[len(c.prefix):]

    def log(self, *a):
        if self.verbose:
            print(*a, file=sys.stderr, flush=True)

    def transmitting(self):
        return self.tx or time.time() < self.keying_until or time.time() < self.tune_until

    def if_frame(self):
        freq = self.fa
        sign = "-" if self.clar < 0 else "+"
        # P4 clarifier on/off, P5 fixed 0 (CAT reference, IF).
        name = self.pms if (self.mem and self.pms) else "%03d" % self.channel
        return ("IF%s%09d%s%04d%d0%s%d%d00%d" % (
            name, freq, sign, abs(self.clar), int(self.rx_clar),
            self.mode, self.mem, 0, 0))

    def handle(self, frame):
        """Returns the answer (without ';') or None, or '?' for a refusal."""
        if not self.power:
            # A sleeping radio ignores everything but a second PS1.
            if frame == "PS1":
                if self.wake_pending:
                    self.power = True
                    self.wake_pending = False
                    self.log("-- powered on")
                else:
                    self.wake_pending = True
            return None
        now = time.time()

        if frame == "IF":
            return self.if_frame()
        if frame == "ID":
            return "ID0650"
        if frame == "FA":
            return "FA%09d" % self.fa
        if frame == "FB":
            return "FB%09d" % self.fb
        if re.fullmatch(r"FA\d{9}", frame):
            f = int(frame[2:])
            if not 30000 <= f <= 56000000:
                return "?"
            self.fa = f
            return None
        if re.fullmatch(r"FB\d{9}", frame):
            self.fb = int(frame[2:])
            return None
        if frame == "MD0":
            return "MD0" + self.mode
        if re.fullmatch(r"MD0[1-9A-D]", frame):
            self.mode = frame[3]
            return None
        if frame == "TX":
            return "TX" + ("1" if self.transmitting() else "0")
        if frame in ("TX0", "TX1", "TX2"):
            self.tx = frame != "TX0"
            return None
        if frame == "SM0":
            return "SM0%03d" % (0 if self.transmitting() else random.randint(40, 140))
        if re.fullmatch(r"RM[3-6]", frame):
            if not self.transmitting():
                return frame + "000"
            v = {"3": random.randint(10, 60), "4": random.randint(0, 40),
                 "5": random.randint(150, 200), "6": random.randint(10, 40)}[frame[2]]
            return frame + "%03d" % v
        if frame == "RI0":
            return "RI00"
        if frame == "PS":
            return "PS1"
        if frame == "PS0":
            self.power = False
            self.tx = False
            self.log("-- powered off")
            return None
        if frame == "PS1":
            return None
        if re.fullmatch(r"AI[01]", frame):
            return None
        if re.fullmatch(r"BS\d\d", frame):
            f = BANDS.get(frame[2:])
            if f is None:
                return "?"
            self.fa = f
            return None
        if frame in ("BU0", "BD0"):
            keys = sorted(BANDS)
            cur = min(keys, key=lambda k: abs(BANDS[k] - self.fa))
            i = keys.index(cur) + (1 if frame == "BU0" else -1)
            self.fa = BANDS[keys[i % len(keys)]]
            return None
        if frame == "VM":
            if self.mem:
                self.mem = 0
                self.fa, self.mode = self.vfo_saved
            else:
                self.vfo_saved = (self.fa, self.mode)
                self.mem = 1
                ch = "%03d" % self.channel
                if ch in self.memories:
                    self.fa, self.mode = int(self.memories[ch][:9]), self.memories[ch][16]
            return None
        if frame == "AB":
            self.fb = self.fa
            return None
        if frame == "BA":
            self.fa = self.fb
            return None
        if frame == "SV":
            self.fa, self.fb = self.fb, self.fa
            return None
        if re.fullmatch(r"R[UD]\d{4}", frame):
            d = int(frame[2:])
            self.clar = max(-9999, min(9999, self.clar + (d if frame[1] == "U" else -d)))
            return None
        if frame == "RC":
            self.clar = 0
            return None
        if re.fullmatch(r"CF0[01]0", frame):
            self.rx_clar = frame[3] == "1"
            self.store("CLAR", frame)
            return None
        if re.fullmatch(r"MR(\d{3}|P[1-9][LU])", frame):
            ch = frame[2:]
            if self.slow_memory > 0:
                # A slow radio: this answer comes after the server has
                # stopped waiting, while the next read is in progress.
                self.slow_memory -= 1
                time.sleep(1.8)
            if ch not in self.memories:
                return "?"              # an empty channel
            # As a real FT-891 answers: the channel field holds the radio's
            # current memory channel, not the one read (MR002 is answered
            # MR001... when the radio stands on 001); the fields that follow
            # are those of the channel read.
            current = self.pms if (self.mem and self.pms) else "%03d" % self.channel
            return "MR" + current + self.memories[ch]
        m = re.fullmatch(r"MW(\d{3}|P[1-9][LU])(\d{9}[+-]\d{4}[01]0[1-9A-D]0[012]00[012])", frame)
        if m:
            f = int(m.group(2)[:9])
            if not 30000 <= f <= 56000000:
                return "?"
            self.memories[m.group(1)] = m.group(2)
            return None
        if frame.startswith("MW"):
            return "?"
        if re.fullmatch(r"MC(\d{3}|P[1-9][LU])", frame):
            ch = frame[2:]
            if ch not in self.memories:
                return "?"
            # Recalled: memory mode on that channel; the VFO is kept aside.
            if ch.isdigit():
                self.channel = int(ch)
                self.pms = None
            else:
                self.pms = ch
            if not self.mem:
                self.vfo_saved = (self.fa, self.mode)
            data = self.memories[ch]
            self.fa = int(data[:9])
            self.mode = data[16]
            self.mem = 1
            return None
        if frame == "MA":
            # Memory to VFO-A: the selected channel's frequency and mode.
            ch = "%03d" % self.channel
            if ch not in self.memories:
                return "?"
            data = self.memories[ch]
            self.vfo_saved = (int(data[:9]), data[16])
            return None
        if frame == "AM":
            # VFO-A to the selected memory channel.
            fa, mode = (self.vfo_saved if self.mem else (self.fa, self.mode))
            # freq(9) clar(+0000) P4 P5, mode, P7 P8 P9(2) P10: 22 characters.
            self.memories["%03d" % self.channel] = "%09d+000000%s00000" % (fa, mode)
            return None
        if re.fullmatch(r"PB0[0-5]", frame):
            # Voice memory playback goes on the air, a few seconds here.
            self.keying_until = now + 3.0 if frame[3] != "0" else 0
            return None
        if frame == "RIC":
            return "RIC" + (self.by_code["SPL"].value or "0")
        if frame == "QS":
            off = int(self.by_code["EX0513"].value or "+05")
            self.fb = self.fa + off * 1000
            self.by_code["SPL"].value = "1"
            return None
        if frame in ("CH0", "CH1"):
            # Round the list, as the radio does: 099 up is 001.
            self.channel = (self.channel - 1 + (1 if frame == "CH0" else -1)) % 99 + 1
            ch = "%03d" % self.channel
            if self.mem and ch in self.memories:
                self.fa, self.mode = int(self.memories[ch][:9]), self.memories[ch][16]
            return None
        if frame == "GT0":
            # Set to AUTO (4), the radio answers AUTO-FAST, -MID or -SLOW; this
            # model gives SLOW in SSB, FAST in CW, MID otherwise.
            v = self.by_code["AGC"].value or "0"
            if v == "4":
                v = "6" if self.mode in ("1", "2") else "4" if self.mode in ("3", "7") else "5"
            return "GT0" + v
        if frame == "AC002":
            self.tune_until = now + 2.5
            self.store("TNR", "AC001")
            return None
        if re.fullmatch(r"KY[6-9A]", frame):
            # A TEXT memory, written with KM, is played by KY6-KYA.
            if self.mode not in ("3", "7") or self.by_code["KEYER"].value != "1":
                return "?"
            n = "6789A".index(frame[2]) + 1
            text = (self.by_code["KM%d" % n].value or "").rstrip("}")
            wpm = int(self.by_code["SPEED"].value or "20")
            self.keying_until = now + max(1.0, len(text) * 8 * 1.2 / wpm)
            self.log("-- keying memory %d: %s" % (n, text))
            return None
        if re.fullmatch(r"KY[1-5]", frame):
            # KY1-KY5 play a MESSAGE memory, recorded with the paddle; a TEXT
            # one is refused, as the radio does.
            n = int(frame[2])
            if self.mode not in ("3", "7") or self.by_code["EX04%02d" % (6 + n)].value != "1":
                return "?"
            self.keying_until = now + 2.0
            self.log("-- keying message %d" % n)
            return None
        if frame in ("KR0",):
            self.keying_until = 0
        if frame.startswith("MX1") or frame.startswith("MX0"):
            self.tx = frame == "MX1"
        if frame.startswith("OS0") and self.mode not in MODES_WITH_FM:
            return "?"
        if frame == "OS0" and self.mode not in MODES_WITH_FM:
            return "?"

        # Read-only information without parameters: the versions of menu 18.
        # The digits are made up; the real format is whatever the radio sends.
        versions = {"EX1801": "0105", "EX1802": "0212", "EX1803": "0103"}
        if frame in versions:
            return frame + versions[frame]

        # Generic table: reads first (exact read frame), then sets.
        for c in self.commands:
            if c.read and frame == c.read.rstrip(";") and c.value is not None:
                return c.answer + c.value
        for c in self.commands:
            if c.set_re:
                m = c.set_re.match(frame)
                if m:
                    if not c.valid(m.groups()):
                        return "?"
                    if c.params:
                        c.value = frame[len(c.prefix):]
                    return None
        return "?"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--link", help="create a symlink to the pseudo-terminal at this path")
    ap.add_argument("-v", "--verbose", action="store_true", help="print every frame")
    ap.add_argument("--latency", type=float, default=0.008, help="answer delay in seconds")
    ap.add_argument("--slow-memory", type=int, default=0,
                    help="answer the first N memory reads (MR) late, after 1.8 s")
    args = ap.parse_args()

    master, slave = pty.openpty()
    tty.setraw(slave)
    name = os.ttyname(slave)
    if args.link:
        try:
            os.unlink(args.link)
        except FileNotFoundError:
            pass
        os.symlink(name, args.link)
        print(args.link, "->", name, flush=True)
    else:
        print(name, flush=True)

    radio = FT891(args.verbose)

    radio.slow_memory = args.slow_memory
    buf = b""
    while True:
        r, _, _ = select.select([master], [], [], 0.5)
        if not r:
            continue
        try:
            data = os.read(master, 4096)
        except OSError:
            time.sleep(0.1)
            continue
        buf += data
        while b";" in buf:
            raw, buf = buf.split(b";", 1)
            # Only line ends are dropped: spaces are data in a keyer memory.
            frame = raw.decode("latin-1").strip("\r\n").upper()
            if not frame:
                continue
            answer = radio.handle(frame)
            radio.log("<-", frame + ";", "->", (answer + ";") if answer else "")
            if answer:
                time.sleep(args.latency)
                os.write(master, (answer + ";").encode("latin-1"))


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
