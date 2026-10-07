#!/usr/bin/env python3
"""Hand-author reMarkable-style Home glyphs: thin e-ink strokes, spare stationery.

Draws vector-like stroke marks at high res, thresholds to crisp 1-bit 96×96,
packs firmware/components/pocket_ui/src/home_glyphs.inc, writes 1bit-*.png + preview.

Style brief (v74):
  - Even stroke weights with optical balance across the set
  - Clearer metaphors (Pass badge, Reading book, Settings gear teeth)
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
# Stroke weight at HI res → ~2.5–3 px at 96 after BOX (survives home downsample).
SW = 12  # confident at ~55 px panel size; even across set
PAD = 56  # HI-space margin → ~14 px at 96 — a touch tighter than v73 for optical presence

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
    """Stroke an arc (degrees, ImageDraw convention: 0=east, CW)."""
    bbox = [cx - r, cy - r, cx + r, cy + r]
    draw.arc(bbox, start=start, end=end, fill=0, width=w)


def rounded_rect_outline(
    draw: ImageDraw.ImageDraw, x0: float, y0: float, x1: float, y1: float, r: float, w: int = SW
) -> None:
    draw.rounded_rectangle([x0, y0, x1, y1], radius=r, outline=0, width=w)


# ---------------------------------------------------------------------------
# Individual glyphs — composition centered in HI with PAD margin.
# Content box: (PAD, PAD) .. (HI-PAD, HI-PAD).
# ---------------------------------------------------------------------------


def draw_notes(draw: ImageDraw.ImageDraw) -> None:
    """Page outline + dog-ear crease + three calm rules — stationery sheet."""
    x0, y0, x1, y1 = PAD + 36, PAD + 12, HI - PAD - 36, HI - PAD - 12
    fold = 40
    pts = [
        (x0, y0),
        (x1 - fold, y0),
        (x1, y0 + fold),
        (x1, y1),
        (x0, y1),
        (x0, y0),
    ]
    ink(draw, pts, width=SW, joint="curve")
    # Dog-ear crease (two segments meeting at the fold corner)
    ink(draw, [(x1 - fold, y0), (x1 - fold, y0 + fold), (x1, y0 + fold)], width=SW)
    # Three rules — even length, optically centered in the page body
    lx0, lx1 = x0 + 30, x1 - 30
    for t in (0.36, 0.52, 0.68):
        y = y0 + (y1 - y0) * t
        ink(draw, [(lx0, y), (lx1, y)], width=SW - 1)


def draw_ledger(draw: ImageDraw.ImageDraw) -> None:
    """Bound notebook: cover + spine + three open rings + two ledger rules."""
    x0, y0, x1, y1 = PAD + 44, PAD + 16, HI - PAD - 24, HI - PAD - 16
    rounded_rect_outline(draw, x0, y0, x1, y1, r=16, w=SW)
    # Spine rule
    sx = x0 + 38
    ink(draw, [(sx, y0 + 12), (sx, y1 - 12)], width=SW - 1)
    # Three binder rings — open circles centered on the spine
    for t in (0.26, 0.50, 0.74):
        cy = y0 + (y1 - y0) * t
        circle_outline(draw, sx, cy, 13, w=SW - 1)
    # Two calm ledger lines in the page body
    for t in (0.40, 0.58):
        y = y0 + (y1 - y0) * t
        ink(draw, [(sx + 30, y), (x1 - 26, y)], width=SW - 1)


def draw_clock(draw: ImageDraw.ImageDraw) -> None:
    """Analog face: outer ring, four ticks, distinct hands at ~10:10, hub."""
    cx = cy = HI / 2
    r = (HI - 2 * PAD) / 2 - 4
    circle_outline(draw, cx, cy, r, w=SW)
    # Cardinal ticks — long enough to survive 96→55 downsample
    for clock_h in (0, 3, 6, 9):
        rad = math.radians(clock_h * 30 - 90)
        x_o = cx + (r - 4) * math.cos(rad)
        y_o = cy + (r - 4) * math.sin(rad)
        x_i = cx + (r - 36) * math.cos(rad)
        y_i = cy + (r - 36) * math.sin(rad)
        ink(draw, [(x_i, y_i), (x_o, y_o)], width=SW + 1)

    def hand(clock_deg: float, length: float, w: int) -> None:
        rad = math.radians(clock_deg - 90)
        x0 = cx + 10 * math.cos(rad)
        y0 = cy + 10 * math.sin(rad)
        ink(draw, [(x0, y0), (cx + length * math.cos(rad), cy + length * math.sin(rad))], width=w)

    hand(60, r * 0.64, SW + 1)  # minute at :10 → 2
    hand(305, r * 0.44, SW)  # hour at 10:10
    draw.ellipse([cx - 8, cy - 8, cx + 8, cy + 8], fill=0)


def draw_pass(draw: ImageDraw.ImageDraw) -> None:
    """ID badge: clip bar + rounded plate + photo window + two identity rules."""
    cx = HI / 2
    # Clip tab — short bar above the plate (readable at small size)
    tab_w, tab_h = 42, 24
    tab_y0 = PAD + 14
    rounded_rect_outline(draw, cx - tab_w / 2, tab_y0, cx + tab_w / 2, tab_y0 + tab_h, r=7, w=SW)
    # Small clip slot inside the tab
    ink(draw, [(cx - 10, tab_y0 + tab_h / 2), (cx + 10, tab_y0 + tab_h / 2)], width=SW - 2)
    # Badge body
    x0, y0 = PAD + 48, PAD + 52
    x1, y1 = HI - PAD - 48, HI - PAD - 14
    rounded_rect_outline(draw, x0, y0, x1, y1, r=18, w=SW)
    # Photo window (square-ish, centered upper third)
    px0, py0 = cx - 42, y0 + 22
    px1, py1 = cx + 42, y0 + 100
    rounded_rect_outline(draw, px0, py0, px1, py1, r=10, w=SW - 1)
    # Simple head silhouette hint inside photo (calm, not a fill blob)
    head_cy = py0 + 28
    circle_outline(draw, cx, head_cy, 16, w=SW - 2)
    # Shoulder arc under the head
    arc_outline(draw, cx, py1 - 8, 28, start=200, end=340, w=SW - 2)
    # Two identity rules under the photo
    for i, t in enumerate((0.0, 1.0)):
        ry = py1 + 28 + i * 28
        half = 52 - i * 10  # second rule shorter — name + subtitle
        ink(draw, [(cx - half, ry), (cx + half, ry)], width=SW - 1)


def draw_weather(draw: ImageDraw.ImageDraw) -> None:
    """Sun: open circle + eight short rays — even gaps, no cloud blob."""
    cx = cy = HI / 2
    r = 62
    circle_outline(draw, cx, cy, r, w=SW)
    ray_in, ray_out = r + 18, r + 52
    for i in range(8):
        ang = math.radians(i * 45 - 90)
        x0 = cx + ray_in * math.cos(ang)
        y0 = cy + ray_in * math.sin(ang)
        x1 = cx + ray_out * math.cos(ang)
        y1 = cy + ray_out * math.sin(ang)
        ink(draw, [(x0, y0), (x1, y1)], width=SW)


def draw_music(draw: ImageDraw.ImageDraw) -> None:
    """Eighth note: solid oval head + thick stem + single flag — optically centered."""
    hx, hy = HI / 2 - 14, HI / 2 + 56
    # Head — solid oval, slightly larger so it matches stroke-set presence
    draw.ellipse([hx - 40, hy - 28, hx + 40, hy + 28], outline=0, fill=0, width=SW)
    # Stem — SW+2 so it survives BOX downsample
    sx = hx + 34
    top = hy - 172
    ink(draw, [(sx, hy - 2), (sx, top)], width=SW + 2)
    # Flag — single calm curve from stem top
    flag = [
        (sx, top),
        (sx + 30, top + 16),
        (sx + 54, top + 42),
        (sx + 62, top + 72),
        (sx + 44, top + 102),
    ]
    ink(draw, flag, width=SW + 1, joint="curve")


def draw_settings(draw: ImageDraw.ImageDraw) -> None:
    """Gear: hollow hub + six flat-top teeth (not a ship-wheel of bars)."""
    cx = cy = HI / 2
    r_hub = 34
    r_rim = 72
    r_tooth = 98
    # Hub ring
    circle_outline(draw, cx, cy, r_hub, w=SW)
    # Rim ring
    circle_outline(draw, cx, cy, r_rim, w=SW)
    # Six flat-top teeth: short radial rectangle stubs (read as gear, not spokes)
    tooth_half_ang = math.radians(11)
    for i in range(6):
        mid = math.radians(i * 60 - 90)
        # Outer flat
        a0, a1 = mid - tooth_half_ang, mid + tooth_half_ang
        pts = [
            (cx + r_rim * math.cos(a0), cy + r_rim * math.sin(a0)),
            (cx + r_tooth * math.cos(a0), cy + r_tooth * math.sin(a0)),
            (cx + r_tooth * math.cos(a1), cy + r_tooth * math.sin(a1)),
            (cx + r_rim * math.cos(a1), cy + r_rim * math.sin(a1)),
        ]
        ink(draw, pts, width=SW, joint="curve")


def draw_update(draw: ImageDraw.ImageDraw) -> None:
    """Circular refresh: open clockwise arc + clear chevron tip."""
    cx = cy = HI / 2
    r = 92
    # Nearly full ring; gap near 12–1 o'clock
    arc_outline(draw, cx, cy, r, start=345, end=295, w=SW)
    # Tip at the clockwise end of the arc (~295°)
    tip_pil = 295
    tip = math.radians(tip_pil)
    tx = cx + r * math.cos(tip)
    ty = cy + r * math.sin(tip)
    # Back along the arc for chevron base
    back_pil = 275
    back = math.radians(back_pil)
    bx = cx + r * math.cos(back)
    by = cy + r * math.sin(back)
    # Radial normal at tip
    nx, ny = math.cos(tip), math.sin(tip)
    ink(draw, [(bx + nx * 30, by + ny * 30), (tx, ty)], width=SW)
    ink(draw, [(bx - nx * 30, by - ny * 30), (tx, ty)], width=SW)


def draw_reading(draw: ImageDraw.ImageDraw) -> None:
    """Open book: two pages with curved bottoms + shared spine (not a window)."""
    cx = HI / 2
    top = PAD + 44
    bot = HI - PAD - 28
    left, right = PAD + 22, HI - PAD - 22
    mid = cx
    # Left page: top edge, outer side, curved bottom (page droop), spine
    # Approximate curve with polyline points
    left_bot = []
    for i in range(9):
        t = i / 8.0
        x = mid - 4 - t * ((mid - 4) - left)
        # Parabola droop: deepest at outer edge
        y = bot - 22 * math.sin(t * math.pi * 0.55)
        left_bot.append((x, y))
    left_pts = (
        [(mid - 4, top), (left + 14, top), (left, top + 20)]
        + left_bot
        + [(mid - 4, bot - 6), (mid - 4, top)]
    )
    ink(draw, left_pts, width=SW, joint="curve")
    # Right page (mirror)
    right_bot = []
    for i in range(9):
        t = i / 8.0
        x = mid + 4 + t * (right - (mid + 4))
        y = bot - 22 * math.sin(t * math.pi * 0.55)
        right_bot.append((x, y))
    right_pts = (
        [(mid + 4, top), (right - 14, top), (right, top + 20)]
        + right_bot
        + [(mid + 4, bot - 6), (mid + 4, top)]
    )
    ink(draw, right_pts, width=SW, joint="curve")
    # Spine
    ink(draw, [(mid, top + 2), (mid, bot - 8)], width=SW)
    # One calm rule per page
    mid_y = top + (bot - top) * 0.48
    ink(draw, [(left + 30, mid_y), (mid - 18, mid_y)], width=SW - 1)
    ink(draw, [(mid + 18, mid_y), (right - 30, mid_y)], width=SW - 1)


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
        "// Auto-generated 1-bit Home glyphs (v74 reMarkable strokes). Do not edit by hand.",
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
