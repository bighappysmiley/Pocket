#!/usr/bin/env python3
"""Hand-author reMarkable-style Home glyphs: thin e-ink strokes, spare stationery.

Draws vector-like stroke marks at high res, thresholds to crisp 1-bit 96×96,
packs firmware/components/pocket_ui/src/home_glyphs.inc, writes 1bit-*.png + preview.

Style brief (v73):
  - Thin confident strokes (no SF fills, no blobs/triangles/scribbles)
  - Generous padding inside the rounded-square plate
  - High recognizability at ~50–60 px on-panel
  - Calm professional notebook UI (reMarkable)
"""
from __future__ import annotations

import math
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent
SRC = ROOT / "home_glyphs"
OUT_INC = ROOT.parent / "components/pocket_ui/src/home_glyphs.inc"

SIZE = 96
# Draw large then BOX-downsample so strokes stay even after 1-bit.
HI = 384  # 4×
# Stroke weight at HI res → ~2.0–2.5 px at 96 after BOX (survives home downsample).
SW = 11  # ~2.5–3 px at 96 — confident at ~55 px panel size
PAD = 64  # HI-space margin → ~16 px at 96 — generous but still legible

NAMES = [
    ("notes", "Notes"),
    ("ledger", "Ledger"),
    ("clock", "Clock"),
    ("pass", "Pass"),
    ("weather", "Weather"),
    ("music", "Music"),
    ("settings", "Settings"),
    ("update", "Update"),
    ("reading", "Reading"),
]


def blank() -> tuple[Image.Image, ImageDraw.ImageDraw]:
    im = Image.new("L", (HI, HI), 255)
    return im, ImageDraw.Draw(im)


def ink(draw: ImageDraw.ImageDraw, *args, **kwargs) -> None:
    kwargs.setdefault("fill", 0)
    draw.line(*args, **kwargs)


def circle_outline(draw: ImageDraw.ImageDraw, cx: float, cy: float, r: float, w: int = SW) -> None:
    bbox = [cx - r, cy - r, cx + r, cy + r]
    draw.ellipse(bbox, outline=0, width=w)


def arc_outline(
    draw: ImageDraw.ImageDraw,
    cx: float,
    cy: float,
    r: float,
    start: float,
    end: float,
    w: int = SW,
) -> None:
    """Stroke an arc (degrees, ImageDraw convention: 0=east, CCW)."""
    bbox = [cx - r, cy - r, cx + r, cy + r]
    draw.arc(bbox, start=start, end=end, fill=0, width=w)


def rounded_rect_outline(
    draw: ImageDraw.ImageDraw, x0: float, y0: float, x1: float, y1: float, r: float, w: int = SW
) -> None:
    draw.rounded_rectangle([x0, y0, x1, y1], radius=r, outline=0, width=w)


# ---------------------------------------------------------------------------
# Individual glyphs — composition centered in HI with PAD margin.
# Content box: (PAD, PAD) .. (HI-PAD, HI-PAD) ≈ 240×240 usable.
# ---------------------------------------------------------------------------


def draw_notes(draw: ImageDraw.ImageDraw) -> None:
    """Page outline + three calm ruled lines. Tiny dog-ear as stroke only."""
    x0, y0, x1, y1 = PAD + 28, PAD + 8, HI - PAD - 28, HI - PAD - 8
    fold = 36
    # Page body without top-right corner (dog-ear cut).
    pts = [
        (x0, y0),
        (x1 - fold, y0),
        (x1, y0 + fold),
        (x1, y1),
        (x0, y1),
        (x0, y0),
    ]
    ink(draw, pts, width=SW, joint="curve")
    # Dog-ear crease
    ink(draw, [(x1 - fold, y0), (x1 - fold, y0 + fold), (x1, y0 + fold)], width=SW)
    # Three short rules — stationery, not a filled sheet
    lx0, lx1 = x0 + 28, x1 - 28
    for i, t in enumerate((0.32, 0.50, 0.68)):
        y = y0 + (y1 - y0) * t
        # Slightly shorten bottom rule for calm asymmetry
        r = lx1 - (8 if i == 2 else 0)
        ink(draw, [(lx0, y), (r, y)], width=SW - 1)


def draw_ledger(draw: ImageDraw.ImageDraw) -> None:
    """Bound notebook: thin cover + three open rings on the spine."""
    x0, y0, x1, y1 = PAD + 40, PAD + 12, HI - PAD - 20, HI - PAD - 12
    rounded_rect_outline(draw, x0, y0, x1, y1, r=14, w=SW)
    # Spine rule
    sx = x0 + 34
    ink(draw, [(sx, y0 + 10), (sx, y1 - 10)], width=SW - 1)
    # Three binder rings (small open circles straddling the spine)
    for t in (0.28, 0.50, 0.72):
        cy = y0 + (y1 - y0) * t
        circle_outline(draw, sx, cy, 11, w=SW - 2)
    # Two calm ledger lines
    for t in (0.40, 0.58):
        y = y0 + (y1 - y0) * t
        ink(draw, [(sx + 28, y), (x1 - 24, y)], width=SW - 1)


def draw_clock(draw: ImageDraw.ImageDraw) -> None:
    """Analog face: outer ring, four ticks, thin hands at ~10:10."""
    cx = cy = HI / 2
    r = (HI - 2 * PAD) / 2 - 8
    circle_outline(draw, cx, cy, r, w=SW)
    # Cardinal ticks — short, inward (math angles: 0=east, CCW; screen y↓)
    for clock_h in (0, 3, 6, 9):
        rad = math.radians(clock_h * 30 - 90)
        x_o = cx + (r - 4) * math.cos(rad)
        y_o = cy + (r - 4) * math.sin(rad)
        x_i = cx + (r - 26) * math.cos(rad)
        y_i = cy + (r - 26) * math.sin(rad)
        ink(draw, [(x_i, y_i), (x_o, y_o)], width=SW - 1)

    def hand(clock_deg: float, length: float, w: int) -> None:
        # clock 0° = 12 o'clock, increasing clockwise
        rad = math.radians(clock_deg - 90)
        ink(draw, [(cx, cy), (cx + length * math.cos(rad), cy + length * math.sin(rad))], width=w)

    hand(60, r * 0.58, SW)  # minute at :10 → 2
    hand(305, r * 0.40, SW)  # hour at 10:10
    draw.ellipse([cx - 5, cy - 5, cx + 5, cy + 5], fill=0)


def draw_pass(draw: ImageDraw.ImageDraw) -> None:
    """ID badge: small clip bar + rounded plate + photo window + one rule."""
    cx = HI / 2
    # Clip tab (simple, readable at small size — no fragile lanyard arc)
    tab_w, tab_h = 36, 22
    rounded_rect_outline(draw, cx - tab_w / 2, PAD + 18, cx + tab_w / 2, PAD + 18 + tab_h, r=6, w=SW)
    # Badge body
    x0, y0 = PAD + 44, PAD + 48
    x1, y1 = HI - PAD - 44, HI - PAD - 12
    rounded_rect_outline(draw, x0, y0, x1, y1, r=18, w=SW)
    # Photo window
    px0, py0 = cx - 40, y0 + 26
    px1, py1 = cx + 40, y0 + 96
    rounded_rect_outline(draw, px0, py0, px1, py1, r=8, w=SW - 1)
    # Identity rule
    ry = py1 + 30
    ink(draw, [(x0 + 28, ry), (x1 - 28, ry)], width=SW - 1)


def draw_weather(draw: ImageDraw.ImageDraw) -> None:
    """Sun: open circle + eight short rays — no cloud blob."""
    cx = cy = HI / 2
    r = 58
    circle_outline(draw, cx, cy, r, w=SW)
    ray_in, ray_out = r + 16, r + 48
    for i in range(8):
        ang = math.radians(i * 45 - 90)
        x0 = cx + ray_in * math.cos(ang)
        y0 = cy + ray_in * math.sin(ang)
        x1 = cx + ray_out * math.cos(ang)
        y1 = cy + ray_out * math.sin(ang)
        ink(draw, [(x0, y0), (x1, y1)], width=SW + 1)


def draw_music(draw: ImageDraw.ImageDraw) -> None:
    """Eighth note: open oval head + stem + single flag curve."""
    # Head — solid oval (classic notation mark; still spare vs SF fills)
    hx, hy = HI / 2 - 10, HI / 2 + 52
    draw.ellipse([hx - 40, hy - 28, hx + 40, hy + 28], outline=0, fill=0, width=SW)
    # Stem
    sx = hx + 36
    top = hy - 168
    ink(draw, [(sx, hy - 6), (sx, top)], width=SW)
    # Flag — single calm curve from stem top (polyline approx)
    flag = [
        (sx, top),
        (sx + 26, top + 16),
        (sx + 48, top + 40),
        (sx + 58, top + 64),
        (sx + 44, top + 92),
    ]
    ink(draw, flag, width=SW, joint="curve")


def draw_settings(draw: ImageDraw.ImageDraw) -> None:
    """Gear as stroked ring + six short radial teeth — hollow hub."""
    cx = cy = HI / 2
    r_outer = 70
    r_inner = 32
    circle_outline(draw, cx, cy, r_outer, w=SW)
    circle_outline(draw, cx, cy, r_inner, w=SW)
    # Six teeth as short radial bars outside the rim (math: 0=east, y↓)
    for i in range(6):
        ang = math.radians(i * 60 - 90)
        x0 = cx + (r_outer + 2) * math.cos(ang)
        y0 = cy + (r_outer + 2) * math.sin(ang)
        x1 = cx + (r_outer + 30) * math.cos(ang)
        y1 = cy + (r_outer + 30) * math.sin(ang)
        ink(draw, [(x0, y0), (x1, y1)], width=SW + 1)


def draw_update(draw: ImageDraw.ImageDraw) -> None:
    """Circular refresh: open arc + simple chevron tip (no fat triangle).

    Pillow arc angles: 0° = east (3 o'clock), increasing clockwise.
    Gap sits near 12–1 o'clock; arrow points clockwise into the gap.
    """
    cx = cy = HI / 2
    r = 90
    # Nearly full ring; gap ~300°→340° (1 o'clock region)
    arc_outline(draw, cx, cy, r, start=340, end=300, w=SW)
    # Tip at the clockwise end of the arc (300° = ~1 o'clock)
    tip_pil = 300
    tip = math.radians(tip_pil)
    # Convert PIL clockwise-from-east to screen math (x right, y down):
    # screen angle from +x CCW: same as PIL for cos/sin if we use sin positive down…
    # PIL 0° → (+1,0); 90° → (0,+1) in image coords. So:
    tx = cx + r * math.cos(tip)
    ty = cy + r * math.sin(tip)
    # Point slightly behind tip along the arc (counter-clockwise = smaller PIL? no —
    # clockwise end means back is counter-clockwise = decreasing PIL angle… wait,
    # arc drew start 340 → … → 300, so end is 300; back along stroke is toward 280)
    back_pil = 282
    back = math.radians(back_pil)
    bx = cx + r * math.cos(back)
    by = cy + r * math.sin(back)
    # Radial normal at tip
    nx, ny = math.cos(tip), math.sin(tip)
    ink(draw, [(bx + nx * 26, by + ny * 26), (tx, ty)], width=SW)
    ink(draw, [(bx - nx * 26, by - ny * 26), (tx, ty)], width=SW)


def draw_reading(draw: ImageDraw.ImageDraw) -> None:
    """Open book: two simple page rectangles + spine — crisp at small size."""
    cx = HI / 2
    top, bot = PAD + 40, HI - PAD - 32
    left, right = PAD + 24, HI - PAD - 24
    mid = cx
    gap = 6  # air at spine so pages read as two panels
    # Left page
    rounded_rect_outline(draw, left, top, mid - gap, bot, r=10, w=SW)
    # Right page
    rounded_rect_outline(draw, mid + gap, top, right, bot, r=10, w=SW)
    # Spine (shared binding mark)
    ink(draw, [(mid, top + 6), (mid, bot - 6)], width=SW)
    # One calm rule per page
    mid_y = (top + bot) / 2
    ink(draw, [(left + 22, mid_y), (mid - gap - 16, mid_y)], width=SW - 2)
    ink(draw, [(mid + gap + 16, mid_y), (right - 22, mid_y)], width=SW - 2)


DRAWERS = {
    "notes": draw_notes,
    "ledger": draw_ledger,
    "clock": draw_clock,
    "pass": draw_pass,
    "weather": draw_weather,
    "music": draw_music,
    "settings": draw_settings,
    "update": draw_update,
    "reading": draw_reading,
}


def to_1bit(hi: Image.Image) -> Image.Image:
    """BOX downsample HI→SIZE then hard threshold — keeps stroke weight even."""
    small = hi.resize((SIZE, SIZE), Image.Resampling.BOX)
    # Mid threshold — strokes are solid black on white
    return small.point(lambda p: 0 if p < 180 else 255, mode="L")


def cleanup(mask: Image.Image) -> Image.Image:
    """Drop isolated speckles only (preserve thin stroke runs)."""
    px = mask.load()
    w, h = mask.size
    out = mask.copy()
    opx = out.load()
    for y in range(h):
        for x in range(w):
            if px[x, y] >= 128:
                continue
            n = 0
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    if dx == 0 and dy == 0:
                        continue
                    xx, yy = x + dx, y + dy
                    if 0 <= xx < w and 0 <= yy < h and px[xx, yy] < 128:
                        n += 1
            if n == 0:
                opx[x, y] = 255
    return out


def pack_bits(mask: Image.Image) -> list[int]:
    w, h = mask.size
    row_bytes = (w + 7) // 8
    bits = [0] * (row_bytes * h)
    px = mask.load()
    for y in range(h):
        for x in range(w):
            if px[x, y] < 128:
                bits[y * row_bytes + (x >> 3)] |= 1 << (7 - (x & 7))
    return bits


def emit_c(arrays: list[tuple[str, list[int]]]) -> str:
    row_bytes = (SIZE + 7) // 8
    lines = [
        "// Auto-generated 1-bit Home glyphs (v73 reMarkable strokes). Do not edit by hand.",
        f"// {SIZE}×{SIZE}, MSB packed, 1 = ink. Regenerated by tools/gen_remarkable_glyphs.py",
        f"static constexpr int kHomeGlyphSize = {SIZE};",
        f"static constexpr int kHomeGlyphRowBytes = {row_bytes};",
        f"static constexpr int kHomeGlyphBytes = {SIZE * row_bytes};",
        "",
    ]
    for ident, bits in arrays:
        lines.append(f"static const uint8_t kGlyph{ident}[kHomeGlyphBytes] = {{")
        for i in range(0, len(bits), 12):
            chunk = ", ".join(f"0x{b:02X}" for b in bits[i : i + 12])
            comma = "," if i + 12 < len(bits) else ""
            lines.append(f"  {chunk}{comma}")
        lines.append("};")
        lines.append("")
    lines.append("static const uint8_t* kHomeGlyphBits[9] = {")
    for ident, _ in arrays:
        lines.append(f"  kGlyph{ident},")
    lines.append("};")
    lines.append("")
    return "\n".join(lines)


def main() -> None:
    SRC.mkdir(parents=True, exist_ok=True)
    arrays: list[tuple[str, list[int]]] = []
    thumbs: list[Image.Image] = []

    for slug, ident in NAMES:
        im, draw = blank()
        DRAWERS[slug](draw)
        # Keep a grayscale reference for QA (not used by pack path anymore)
        im.save(SRC / f"glyph-{slug}.png")
        mask = cleanup(to_1bit(im))
        ink_n = sum(1 for p in mask.getdata() if p < 128)
        frac = ink_n / (SIZE * SIZE)
        # Stroke marks: sparse ink. Reject blobs and empties.
        if not (0.030 <= frac <= 0.28):
            raise SystemExit(f"{slug}: ink fraction {frac:.3f} out of stroke range")
        bits = pack_bits(mask)
        arrays.append((ident, bits))
        mask.save(SRC / f"1bit-{slug}.png")
        thumbs.append(mask)
        print(f"{slug:10s} ink={ink_n:4d} ({frac * 100:4.1f}%)")

    OUT_INC.write_text(emit_c(arrays))
    print(f"wrote {OUT_INC}")

    scale = 4
    cell = SIZE * scale + 24
    sheet = Image.new("L", (3 * cell, 3 * cell), 255)
    d = ImageDraw.Draw(sheet)
    for i, mask in enumerate(thumbs):
        big = mask.resize((SIZE * scale, SIZE * scale), Image.Resampling.NEAREST)
        col, row = i % 3, i // 3
        x = col * cell + 12
        y = row * cell + 12
        sheet.paste(big, (x, y))
        d.rectangle([x - 1, y - 1, x + SIZE * scale, y + SIZE * scale], outline=180)
    preview = SRC / "preview-1bit.png"
    sheet.save(preview)
    print(f"wrote {preview}")


if __name__ == "__main__":
    main()
