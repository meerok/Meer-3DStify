#!/usr/bin/env python3
"""Make a fake SD card ("sdmc:/Music/...") with tagged MP3 stubs and generated
cover art, so the preview renderer can scan it like a real 3DS would.
All artists, albums and songs are made up."""
import io, math, os, random, struct, sys
from PIL import Image, ImageDraw, ImageFilter

ROOT = sys.argv[1] if len(sys.argv) > 1 else "sd"

LIB = [
    ("Midnight Arcade", "Neon Harbor", ((255, 64, 160), (60, 20, 140)), "sun",
     ["Night Drive", "Chrome Skyline", "Pixel Rain", "Afterglow Avenue", "Laserline", "Overdrive Heart"]),
    ("Sunny Vale", "Golden Hour", ((255, 170, 60), (220, 70, 40)), "circle",
     ["Lemonade Sky", "Porch Lights", "Slow Summer", "Dandelion"]),
    ("Coral Static", "Deep Blue", ((40, 170, 220), (10, 30, 90)), "waves",
     ["Glass Ocean", "Undertow", "Bioluminescent", "Tidal", "Lighthouse"]),
    ("Juniper Lane", "Wildflower Tapes", ((120, 200, 110), (20, 80, 60)), "leaves",
     ["Polaroid", "Fern Gully Road", "Moss & Stone", "Campfire Song"]),
    ("Echo Atlas", "Paper Moons", ((230, 230, 220), (60, 60, 70)), "moons",
     ["Origami", "Quiet Satellites", "Ink & Ivory"]),
    ("Velvet Riot", "Static Hearts", ((230, 40, 50), (40, 0, 10)), "bars",
     ["Feedback Loop", "Red Light", "Amplifier", "Broken Speakers", "Encore"]),
]


def cover(c1, c2, style, seed):
    random.seed(seed)
    n = 600
    img = Image.new("RGB", (n, n))
    px = img.load()
    for y in range(n):
        t = y / n
        for x in range(0, n):
            u = x / n
            k = t * 0.75 + u * 0.25
            px[x, y] = tuple(int(c1[i] * (1 - k) + c2[i] * k) for i in range(3))
    d = ImageDraw.Draw(img, "RGBA")
    if style == "sun":
        d.ellipse((150, 120, 450, 420), fill=(255, 210, 90, 255))
        for i in range(8):
            y = 300 + i * 22
            d.rectangle((100, y, 500, y + 8 + i), fill=(60, 20, 140, 255))
        for i in range(12):
            d.line((300, 600, -300 + i * 110, 420), fill=(255, 100, 220, 140), width=3)
        d.line((0, 420, 600, 420), fill=(255, 120, 230, 200), width=4)
    elif style == "circle":
        for r in range(260, 20, -40):
            a = 60 + (260 - r) // 2
            d.ellipse((300 - r, 300 - r, 300 + r, 300 + r), outline=(255, 245, 210, a), width=10)
        d.ellipse((240, 240, 360, 360), fill=(255, 250, 230, 255))
    elif style == "waves":
        for i in range(10):
            pts = [(x, 330 + i * 26 + 18 * math.sin(x / 50 + i)) for x in range(0, 601, 10)]
            d.line(pts, fill=(180, 240, 255, 60 + i * 18), width=6)
        d.ellipse((380, 90, 470, 180), fill=(230, 250, 255, 220))
    elif style == "leaves":
        for i in range(26):
            x, y = random.randint(0, 600), random.randint(0, 600)
            r = random.randint(30, 90)
            d.ellipse((x - r, y - r // 3, x + r, y + r // 3), fill=(200, 255, 170, random.randint(50, 140)))
        d.rectangle((0, 470, 600, 600), fill=(20, 60, 40, 160))
    elif style == "moons":
        for i in range(5):
            x = 80 + i * 110
            d.ellipse((x - 40, 260, x + 40, 340), fill=(250, 250, 240, 255))
            d.ellipse((x - 40 + i * 16, 260, x + 40 + i * 16, 340), fill=(60, 60, 70, 255 if i < 4 else 0))
    elif style == "bars":
        for i in range(14):
            h = random.randint(80, 420)
            d.rectangle((40 + i * 38, 560 - h, 64 + i * 38, 560), fill=(255, 90, 90, 200))
    img = img.filter(ImageFilter.GaussianBlur(0.8))
    b = io.BytesIO()
    img.save(b, "JPEG", quality=88)
    return b.getvalue()


def frame(fid, data):
    return fid.encode() + struct.pack(">I", len(data)) + b"\0\0" + data


def text(fid, s):
    return frame(fid, b"\x03" + s.encode("utf-8"))


def syncsafe(n):
    return bytes([(n >> 21) & 127, (n >> 14) & 127, (n >> 7) & 127, n & 127])


def mp3(path, title, artist, album, no, art):
    body = text("TIT2", title) + text("TPE1", artist) + text("TALB", album) + text("TRCK", str(no))
    if art:
        body += frame("APIC", b"\x00image/jpeg\x00\x03\x00" + art)
    body += b"\0" * 64
    tag = b"ID3\x03\x00\x00" + syncsafe(len(body)) + body
    audio = b"\xff\xfb\x90\x64" + b"\0" * 413          # one silent MPEG frame
    with open(path, "wb") as f:
        f.write(tag + audio * 4)


for i, (artist, album, cols, style, songs) in enumerate(LIB):
    d = os.path.join(ROOT, "sdmc:", "Music", artist, album)
    os.makedirs(d, exist_ok=True)
    art = cover(cols[0], cols[1], style, i)
    for n, s in enumerate(songs, 1):
        mp3(os.path.join(d, f"{n:02d} {s}.mp3"), s, artist, album, n, art)
print("fake SD ready in", ROOT)
