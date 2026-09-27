#!/usr/bin/env python3
# Usage: python3 tools/make_icons.py   (needs Pillow)
# Generates the FT891Remote icons: a radio front panel with a tuning knob.
from PIL import Image, ImageDraw, ImageFont, ImageFilter
import math, os

S = 1024
FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
MONO = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf"

def knob(d, cx, cy, r, accent):
    # outer skirt with grip ridges
    for i in range(72):
        a = 2 * math.pi * i / 72
        x1, y1 = cx + (r + 10) * math.cos(a), cy + (r + 10) * math.sin(a)
        x2, y2 = cx + (r - 18) * math.cos(a), cy + (r - 18) * math.sin(a)
        d.line([(x1, y1), (x2, y2)], fill=(20, 22, 26), width=6)
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=(46, 50, 58), outline=(12, 13, 16), width=8)
    r2 = int(r * 0.80)
    d.ellipse([cx - r2, cy - r2, cx + r2, cy + r2], fill=(62, 67, 77))
    r3 = int(r * 0.74)
    d.ellipse([cx - r3, cy - r3, cx + r3, cy + r3], fill=(52, 56, 64))
    # finger dimple
    a = math.radians(-50)
    dx, dy = cx + r * 0.46 * math.cos(a), cy + r * 0.46 * math.sin(a)
    rd = r * 0.17
    d.ellipse([dx - rd, dy - rd, dx + rd, dy + rd], fill=(36, 39, 45), outline=(80, 86, 98), width=4)
    # index mark
    a = math.radians(-90)
    d.line([(cx + r * 0.55 * math.cos(a), cy + r * 0.55 * math.sin(a)),
            (cx + r * 0.72 * math.cos(a), cy + r * 0.72 * math.sin(a))], fill=accent, width=14)

def panel(server):
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    sh = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    ImageDraw.Draw(sh).rounded_rectangle([60, 90, S - 60, S - 40], 150, fill=(0, 0, 0, 150))
    sh = sh.filter(ImageFilter.GaussianBlur(24))
    img.alpha_composite(sh)
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([56, 56, S - 56, S - 72], 150, fill=(34, 38, 46), outline=(14, 15, 18), width=10)
    d.rounded_rectangle([80, 80, S - 80, S - 96], 132, outline=(70, 76, 88), width=4)

    accent = (240, 198, 116)
    # LCD
    d.rounded_rectangle([130, 150, S - 130, 400], 36, fill=(12, 26, 30), outline=(8, 10, 12), width=6)
    f = ImageFont.truetype(MONO, 150)
    txt = "FT-891"
    w = d.textlength(txt, font=f)
    d.text(((S - w) / 2, 175), txt, font=f, fill=(120, 230, 255))
    # small S-meter bar
    for i in range(14):
        col = (120, 230, 255) if i < 9 else (255, 110, 90)
        d.rectangle([165 + i * 50, 352, 165 + i * 50 + 34, 378], fill=col if i < 11 else (40, 60, 66))

    knob(d, S // 2 + (90 if server else 0), 690, 210, accent)

    if server:
        # antenna mast and waves on the left: the station side
        x = 230
        d.line([(x, 880), (x, 610)], fill=accent, width=22)
        d.polygon([(x - 70, 880), (x + 70, 880), (x, 740)], outline=accent, width=14)
        for k, rr in enumerate((60, 105, 150)):
            box = [x - rr, 610 - rr, x + rr, 610 + rr]
            d.arc(box, 205, 255, fill=accent, width=16)
            d.arc(box, 285, 335, fill=accent, width=16)
        d.ellipse([x - 24, 586, x + 24, 634], fill=accent)
    return img

def save(img, base, outdir):
    os.makedirs(outdir, exist_ok=True)
    img.resize((512, 512), Image.LANCZOS).save(os.path.join(outdir, base + "-512.png"))
    img.resize((256, 256), Image.LANCZOS).save(os.path.join(outdir, base + ".png"))
    img.resize((256, 256), Image.LANCZOS).save(os.path.join(outdir, base + ".ico"),
        sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
    for sz in (16, 22, 24, 32, 48, 64, 128, 256, 512):
        dd = os.path.join(outdir, "hicolor", f"{sz}x{sz}", "apps")
        os.makedirs(dd, exist_ok=True)
        img.resize((sz, sz), Image.LANCZOS).save(os.path.join(dd, base + ".png"))

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
out = os.path.join(ROOT, "icons")
save(panel(True), "ft891remote-server", out)
save(panel(False), "ft891remote", out)

# Android launcher icons and splash
client = panel(False)
dens = {"mdpi": 48, "hdpi": 72, "xhdpi": 96, "xxhdpi": 144, "xxxhdpi": 192}
res = os.path.join(ROOT, "android", "res")
for k, v in dens.items():
    client.resize((v, v), Image.LANCZOS).save(f"{res}/drawable-{k}/icon.png")
    client.resize((v * 3, v * 3), Image.LANCZOS).save(f"{res}/drawable-{k}/splash_logo.png")
client.resize((192, 192), Image.LANCZOS).save(f"{res}/drawable/icon.png")
client.resize((432, 432), Image.LANCZOS).save(f"{res}/drawable/splash_logo.png")
print("ok")
