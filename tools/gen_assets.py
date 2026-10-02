#!/usr/bin/env python3
"""Generate the PNG source art for Mushoku Tensei 64.

Every texture in the game is procedurally generated here so the project has
no third-party art. The PNGs are committed under assets/, so this script only
needs to be re-run when you want to change the art (requires Pillow).

Textures are small (32x32) and mostly grayscale: the game tints them with
vertex colors on the RDP, the same trick many N64 games used to save TMEM.
"""
import math
import os
import random
import sys

from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "assets")
SIZE = 32


def tileable_noise(seed, size=SIZE, octaves=((4, 1.0), (8, 0.5), (16, 0.25))):
    """Value noise that wraps around on both axes, normalised to 0..1."""
    rnd = random.Random(seed)
    acc = [[0.0] * size for _ in range(size)]
    total = 0.0
    for cells, amp in octaves:
        grid = [[rnd.random() for _ in range(cells)] for _ in range(cells)]
        for y in range(size):
            for x in range(size):
                fx = x * cells / size
                fy = y * cells / size
                x0, y0 = int(fx), int(fy)
                tx, ty = fx - x0, fy - y0
                tx = tx * tx * (3 - 2 * tx)
                ty = ty * ty * (3 - 2 * ty)
                x1, y1 = (x0 + 1) % cells, (y0 + 1) % cells
                a = grid[y0][x0] * (1 - tx) + grid[y0][x1] * tx
                b = grid[y1][x0] * (1 - tx) + grid[y1][x1] * tx
                acc[y][x] += (a * (1 - ty) + b * ty) * amp
        total += amp
    return [[v / total for v in row] for row in acc]


def gray_image(fn):
    img = Image.new("RGB", (SIZE, SIZE))
    px = img.load()
    for y in range(SIZE):
        for x in range(SIZE):
            v = max(0, min(255, int(fn(x, y))))
            px[x, y] = (v, v, v)
    return img


def tex_grass():
    n = tileable_noise(1)
    rnd = random.Random(11)
    img = gray_image(lambda x, y: 170 + n[y][x] * 70)
    d = ImageDraw.Draw(img)
    # little grass blades, wrapped so the tile stays seamless
    for _ in range(70):
        x, y = rnd.randrange(SIZE), rnd.randrange(SIZE)
        h = rnd.randint(2, 4)
        c = rnd.choice([150, 165, 235, 250])
        for dy in range(h):
            img.putpixel((x % SIZE, (y - dy) % SIZE), (c, c, c))
    return img


def tex_dirt():
    n = tileable_noise(2, octaves=((8, 1.0), (16, 0.6)))
    rnd = random.Random(12)
    img = gray_image(lambda x, y: 175 + n[y][x] * 60)
    for _ in range(18):
        x, y = rnd.randrange(SIZE), rnd.randrange(SIZE)
        c = rnd.choice([140, 150, 240])
        img.putpixel((x, y), (c, c, c))
        img.putpixel(((x + 1) % SIZE, y), (c, c, c))
    return img


def tex_wood():
    n = tileable_noise(3, octaves=((4, 1.0), (32, 0.3)))
    def f(x, y):
        plank = y // 8
        grain = math.sin((x + plank * 7) * 0.9 + n[y][x] * 6.0) * 12
        v = 190 + grain + n[y][x] * 40 - (plank % 2) * 12
        if y % 8 == 0:
            v = 110  # gap between planks
        if (x + plank * 11) % 32 == 0:
            v = 120  # plank seam
        return v
    return gray_image(f)


def tex_stone():
    n = tileable_noise(4, octaves=((8, 1.0), (16, 0.5)))
    def f(x, y):
        row = y // 8
        off = 8 if row % 2 else 0
        v = 185 + n[y][x] * 60
        if y % 8 == 0 or (x + off) % 16 == 0:
            v = 105  # mortar
        elif y % 8 == 1 or (x + off) % 16 == 1:
            v += 25  # bevel highlight
        return v
    return gray_image(f)


def tex_roof():
    n = tileable_noise(5, octaves=((8, 1.0),))
    def f(x, y):
        row = y // 8
        off = 4 if row % 2 else 0
        ly = y % 8
        v = 200 + n[y][x] * 40 - ly * 6
        if ly == 7:
            v = 95
        if (x + off) % 8 == 0:
            v = 120
        return v
    return gray_image(f)


def tex_bark():
    n = tileable_noise(6, octaves=((4, 1.0), (16, 0.6)))
    def f(x, y):
        streak = math.sin(x * 1.3 + n[y][x] * 5.0)
        return 165 + streak * 35 + n[y][x] * 40
    return gray_image(f)


def tex_leaves():
    n = tileable_noise(7, octaves=((8, 1.0), (16, 0.7)))
    rnd = random.Random(17)
    img = gray_image(lambda x, y: 150 + n[y][x] * 70)
    for _ in range(40):
        x, y = rnd.randrange(SIZE), rnd.randrange(SIZE)
        c = rnd.choice([235, 250, 120])
        for dx, dy in ((0, 0), (1, 0), (0, 1)):
            img.putpixel(((x + dx) % SIZE, (y + dy) % SIZE), (c, c, c))
    return img


def tex_plaster():
    n = tileable_noise(8, octaves=((8, 1.0), (32, 0.4)))
    return gray_image(lambda x, y: 215 + n[y][x] * 40)


def tex_water():
    n = tileable_noise(9, octaves=((4, 1.0), (8, 0.5)))
    def f(x, y):
        w = math.sin((x + n[y][x] * 10) * 2 * math.pi / 16) * math.sin((y + n[x][y] * 10) * 2 * math.pi / 16)
        return 190 + w * 45 + n[y][x] * 20
    return gray_image(f)


def tex_glow():
    """Soft round sprite used for magic, sparks and the sun (I8 intensity)."""
    img = Image.new("L", (SIZE, SIZE))
    px = img.load()
    c = (SIZE - 1) / 2
    for y in range(SIZE):
        for x in range(SIZE):
            d = math.hypot(x - c, y - c) / (SIZE / 2)
            v = max(0.0, 1.0 - d)
            px[x, y] = int(255 * (v ** 1.6))
    return img


def ui_circle(size=16, ring=False):
    """White disc (or ring) with soft alpha edge; tinted at runtime for the C-button HUD."""
    big = size * 4
    mask = Image.new("L", (big, big))
    d = ImageDraw.Draw(mask)
    d.ellipse((1, 1, big - 2, big - 2), fill=255)
    if ring:
        d.ellipse((9, 9, big - 10, big - 10), fill=0)
    mask = mask.resize((size, size), Image.LANCZOS)
    img = Image.new("LA", (size, size))
    img.putdata([(255, a) for a in mask.tobytes()])
    return img


def logo_kanji():
    """The 無職転生 title logo, rendered from the system IPA Gothic font."""
    candidates = [
        "/usr/share/fonts/opentype/ipafont-gothic/ipag.ttf",
        "/usr/share/fonts/truetype/fonts-japanese-gothic.ttf",
    ]
    path = next((p for p in candidates if os.path.exists(p)), None)
    if path is None:
        print("warning: no Japanese font found, skipping logo_kanji.png", file=sys.stderr)
        return None
    font = ImageFont.truetype(path, 44)
    text = "無職転生"
    w, h = 200, 56
    mask = Image.new("L", (w, h))
    ImageDraw.Draw(mask).text((w // 2, h // 2), text, font=font, fill=255, anchor="mm",
                              stroke_width=1, stroke_fill=255)
    outline = mask.filter(ImageFilter.MaxFilter(5))
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    px, mp, op = img.load(), mask.load(), outline.load()
    for y in range(h):
        # warm parchment-to-gold vertical gradient
        t = y / (h - 1)
        fill = (int(255 - 20 * t), int(246 - 90 * t), int(214 - 170 * t))
        for x in range(w):
            if mp[x, y] > 110:
                px[x, y] = fill + (255,)
            elif op[x, y] > 110:
                px[x, y] = (40, 18, 60, 255)
    return img


def main():
    os.makedirs(OUT, exist_ok=True)
    textures = {
        "grass": tex_grass, "dirt": tex_dirt, "wood": tex_wood,
        "stone": tex_stone, "roof": tex_roof, "bark": tex_bark,
        "leaves": tex_leaves, "plaster": tex_plaster, "water": tex_water,
    }
    for name, fn in textures.items():
        fn().save(os.path.join(OUT, "tex_%s.png" % name))
    tex_glow().save(os.path.join(OUT, "fx_glow.png"))
    ui_circle().save(os.path.join(OUT, "ui_circle.png"))
    ui_circle(ring=True).save(os.path.join(OUT, "ui_ring.png"))
    logo = logo_kanji()
    if logo is not None:
        logo.save(os.path.join(OUT, "logo_kanji.png"))
    print("assets written to", OUT)


if __name__ == "__main__":
    main()
