"""Renders the FurUltimate brand mark (gradient hexagon badge, flame core, orbit arcs) into a multi-size .ico,
a 512 px PNG, a README banner and the NSIS installer bitmaps."""
import math, os, sys
from PIL import Image, ImageDraw, ImageFilter, ImageFont

GOLD, EMBER, MAGENTA, VIOLET, CORE = (255, 194, 74), (255, 90, 46), (255, 45, 138), (139, 92, 255), (255, 245, 218)
HERE = os.path.dirname(os.path.abspath(__file__))
FONT_DIR = os.path.join(HERE, "..", "assets", "fonts")

def lerp(a, b, t): return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))

def ramp(t):
    t %= 1.0
    stops = [GOLD, EMBER, MAGENTA, VIOLET, GOLD]
    k = t * 4; i = int(k)
    return lerp(stops[i], stops[i + 1], k - i)

def font(name, size): return ImageFont.truetype(os.path.join(FONT_DIR, name), size)

def flame_pts(cx, cy, R, scale, yoff, flick=0.0):
    h, w, fy = R * 0.60 * scale, R * 0.50 * scale, cy + R * 0.10 + yoff
    pts = []
    for i in range(96):
        th = i / 96 * 2 * math.pi
        x = w * math.sin(th) * abs(math.sin(th / 2)) ** 1.5
        wt = ((1 + math.cos(th)) / 2) ** 3
        pts.append((cx + x + flick * wt * R, fy - h * math.cos(th)))
    return pts

def mark(size, tile=True):
    S = size * 4  # supersample
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    cx = cy = S / 2
    if tile:
        d = ImageDraw.Draw(img)
        pad = S * 0.04
        d.rounded_rectangle([pad, pad, S - pad, S - pad], radius=S * 0.22, fill=(14, 10, 20, 255))
    R = S * (0.30 if tile else 0.33)
    # glow
    glow = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    gd = ImageDraw.Draw(glow)
    gd.ellipse([cx - R * 1.25, cy - R * 1.25, cx + R * 1.25, cy + R * 1.25], fill=MAGENTA + (85,))
    gd.ellipse([cx - R * 0.8, cy - R * 0.6, cx + R * 0.8, cy + R * 1.0], fill=EMBER + (110,))
    glow = glow.filter(ImageFilter.GaussianBlur(S * 0.07))
    if tile:
        mask = Image.new("L", (S, S), 0)
        ImageDraw.Draw(mask).rounded_rectangle([S * 0.04, S * 0.04, S * 0.96, S * 0.96], radius=S * 0.22, fill=255)
        glow.putalpha(Image.composite(glow.getchannel("A"), Image.new("L", (S, S), 0), mask))
    img.alpha_composite(glow)
    d = ImageDraw.Draw(img)
    hexp = [(cx + math.cos(-math.pi / 2 + k * math.pi / 3) * R, cy + math.sin(-math.pi / 2 + k * math.pi / 3) * R) for k in range(6)]
    d.polygon(hexp, fill=(18, 12, 26, 255))
    rim = max(2, int(R * 0.14))
    for k in range(6):
        steps = 160
        a = hexp[k]; b = hexp[(k + 1) % 6]
        rr = rim / 2
        for s in range(steps + 1):   # smooth gradient rim: dense overlapping discs, no seams at the joints
            t = s / steps
            x, y = a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t
            d.ellipse([x - rr, y - rr, x + rr, y + rr], fill=ramp((k + t) / 6) + (255,))
    # orbit arcs + spark (skipped on tiny icon sizes where they turn to noise)
    if size >= 32:
        ro = R * 1.30 if not tile else R * 1.28
        aw = max(2, int(R * 0.07))
        d.arc([cx - ro, cy - ro, cx + ro, cy + ro], start=-40, end=15, fill=MAGENTA + (230,), width=aw)
        d.arc([cx - ro, cy - ro, cx + ro, cy + ro], start=140, end=195, fill=VIOLET + (230,), width=aw)
        sx, sy = cx + math.cos(math.radians(15)) * ro, cy + math.sin(math.radians(15)) * ro
        sp = Image.new("RGBA", (S, S), (0, 0, 0, 0))
        ImageDraw.Draw(sp).ellipse([sx - aw * 3, sy - aw * 3, sx + aw * 3, sy + aw * 3], fill=CORE + (200,))
        img.alpha_composite(sp.filter(ImageFilter.GaussianBlur(aw * 1.2)))
        d = ImageDraw.Draw(img)
        d.ellipse([sx - aw, sy - aw, sx + aw, sy + aw], fill=CORE + (255,))
    # flame (gradient: ember outer, gold middle, white-hot core)
    d.polygon(flame_pts(cx, cy, R, 1.0, 0, 0.04), fill=EMBER + (255,))
    d.polygon(flame_pts(cx, cy, R, 0.86, R * 0.07, 0.03), fill=lerp(EMBER, GOLD, 0.45) + (255,))
    d.polygon(flame_pts(cx, cy, R, 0.70, R * 0.14, 0.03), fill=lerp(GOLD, EMBER, 0.2) + (255,))
    d.polygon(flame_pts(cx, cy, R, 0.38, R * 0.30, 0.01), fill=CORE + (255,))
    return img.resize((size, size), Image.LANCZOS)

def gradient_text(canvas, xy, text, fnt, c0, c1):
    w, h = canvas.size
    mask = Image.new("L", (w, h), 0)
    ImageDraw.Draw(mask).text(xy, text, font=fnt, fill=255)
    bbox = mask.getbbox() or (0, 0, w, h)
    grad = Image.new("RGBA", (w, h))
    gd = ImageDraw.Draw(grad)
    for x in range(bbox[0], bbox[2] + 1):
        t = (x - bbox[0]) / max(1, bbox[2] - bbox[0])
        gd.line([(x, 0), (x, h)], fill=lerp(c0, c1, t) + (255,))
    canvas.paste(grad, (0, 0), mask)

def tracked(d, xy, text, fnt, fill, tracking):
    x, y = xy
    for ch in text:
        d.text((x, y), ch, font=fnt, fill=fill)
        x += d.textlength(ch, font=fnt) + tracking

def banner(path, W=1280, H=400):
    img = Image.new("RGB", (W, H), (9, 7, 14))
    g = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    gd = ImageDraw.Draw(g)
    gd.ellipse([-200, -260, 700, 560], fill=MAGENTA + (60,))
    gd.ellipse([120, 60, 640, 520], fill=EMBER + (70,))
    gd.ellipse([800, -200, 1500, 500], fill=VIOLET + (55,))
    g = g.filter(ImageFilter.GaussianBlur(110))
    img.paste(g, (0, 0), g)
    m = mark(300, tile=False)
    img.paste(m, (90, 50), m)
    d = ImageDraw.Draw(img)
    gradient_text(img, (430, 92), "FUR", font("InterDisplay-SemiBold.ttf", 150), (255, 255, 255), (255, 214, 190))
    d = ImageDraw.Draw(img)
    tracked(d, (440, 258), "ULTIMATE", font("Inter-SemiBold.ttf", 44), (255, 106, 61), 16)
    bar = Image.new("RGBA", (420, 6))
    bd = ImageDraw.Draw(bar)
    for x in range(420): bd.line([(x, 0), (x, 6)], fill=lerp(GOLD, VIOLET, x / 419) + (255,))
    img.paste(bar, (442, 322), bar)
    tracked(d, (442, 342), "GPU STRESS TEST  ·  BENCHMARK  ·  AI TOPS  ·  LEADERBOARD", font("Inter-Medium.ttf", 18), (170, 162, 190), 2)
    img.save(path)

if __name__ == "__main__":
    out = sys.argv[1]
    sizes = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256]
    base = mark(256)
    base.save(out, sizes=[(s, s) for s in sizes], append_images=[mark(s) for s in sizes[:-1]])
    mark(512).save(out.replace(".ico", "_512.png"))
    if len(sys.argv) > 2:   # Installer bitmaps (NSIS MUI2): welcome 164x314, header 150x57
        wel = Image.new("RGB", (164, 314), (9, 7, 14))
        g = Image.new("RGBA", (164, 314), (0, 0, 0, 0))
        gd = ImageDraw.Draw(g)
        gd.ellipse([-70, 20, 230, 300], fill=MAGENTA + (55,))
        gd.ellipse([-20, 120, 190, 330], fill=EMBER + (50,))
        g = g.filter(ImageFilter.GaussianBlur(28))
        wel.paste(g, (0, 0), g)
        m = mark(116, tile=False)
        wel.paste(m, (24, 58), m)
        gradient_text(wel, (26, 186), "FUR", font("InterDisplay-SemiBold.ttf", 40), (255, 255, 255), (255, 214, 190))
        d = ImageDraw.Draw(wel)
        tracked(d, (28, 234), "ULTIMATE", font("Inter-SemiBold.ttf", 12), (255, 106, 61), 3.2)
        for x in range(28, 136): d.line([(x, 254), (x, 256)], fill=lerp(GOLD, VIOLET, (x - 28) / 108))
        wel.save(sys.argv[2] + "/welcome.bmp")
        hdr = Image.new("RGB", (150, 57), (255, 255, 255))
        m = mark(48)
        bg = Image.new("RGB", (48, 48), (255, 255, 255)); bg.paste(m, (0, 0), m)
        hdr.paste(bg, (98, 4))
        hdr.save(sys.argv[2] + "/header.bmp")
    if len(sys.argv) > 3:
        banner(sys.argv[3])
