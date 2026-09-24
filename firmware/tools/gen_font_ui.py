#!/usr/bin/env python3
"""Regenerate firmware/components/pocket_ui/src/font_ui.inc from DejaVu Sans.

Default: Body 28px / Display 42px. Advance == bitmap width (unsmush).
Requires: Pillow, DejaVuSans.ttf
"""
from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


def render_face(font: ImageFont.FreeTypeFont, prefix: str, px: int):
    ascent, descent = font.getmetrics()
    h = ascent + descent
    glyphs = []
    bits_arrays = []
    for i, ch in enumerate(range(32, 127)):
        c = chr(ch)
        canvas_w = max(8, px * 3)
        img = Image.new("L", (canvas_w, h), 255)
        draw = ImageDraw.Draw(img)
        draw.text((2, ascent), c, font=font, fill=0, anchor="ls")

        ink_xs = [x for y in range(h) for x in range(canvas_w) if img.getpixel((x, y)) < 250]
        if not ink_xs:
            adv = max(1, int(round(font.getlength(c))))
            bits = [0] * (adv * h)
            bits_arrays.append((f"{prefix}_bits_{i}", bits, adv))
            glyphs.append((adv, adv, f"{prefix}_bits_{i}", c))
            continue

        x0 = max(0, min(ink_xs) - 1)
        x1 = min(canvas_w - 1, max(ink_xs) + 1)
        tw = x1 - x0 + 1
        bits = []
        for y in range(h):
            for x in range(x0, x1 + 1):
                p = img.getpixel((x, y))
                if p < 32:
                    bits.append(3)
                elif p < 96:
                    bits.append(2)
                elif p < 180:
                    bits.append(1)
                else:
                    bits.append(0)
        bits_arrays.append((f"{prefix}_bits_{i}", bits, tw))
        glyphs.append((tw, tw, f"{prefix}_bits_{i}", c))
    return h, ascent, bits_arrays, glyphs


def emit(f, prefix, px, h, ascent, bits_arrays, glyphs):
    f.write(f"// {prefix}: DejaVu {px}px, h={h}\n")
    f.write(f"static constexpr int {prefix}_H = {h};\n")
    f.write(f"static constexpr int {prefix}_ASCENT = {ascent};\n")
    f.write(f"struct {prefix}_Glyph {{ uint8_t w; uint8_t adv; const uint8_t* bits; }};\n")
    for name, bits, w in bits_arrays:
        f.write(f"static const uint8_t {name}[] = {{{', '.join(str(b) for b in bits)}}};\n")
    f.write(f"static const {prefix}_Glyph {prefix}_GLYPHS[95] = {{\n")
    for w, adv, name, c in glyphs:
        esc = c.replace("\\", "\\\\").replace("'", "\\'")
        f.write(f"  {{{w}, {adv}, {name}}}, // '{esc}'\n")
    f.write("};\n")
    f.write(f"static const {prefix}_Glyph& {prefix}_glyph(char c) {{\n")
    f.write(f"  if (c < 32 || c > 126) return {prefix}_GLYPHS[0];\n")
    f.write(f"  return {prefix}_GLYPHS[static_cast<int>(c) - 32];\n")
    f.write("}\n\n")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--ttf", default="/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf")
    ap.add_argument("--body", type=int, default=28)
    ap.add_argument("--disp", type=int, default=42)
    ap.add_argument(
        "--out",
        type=Path,
        default=Path(__file__).resolve().parents[1] / "components/pocket_ui/src/font_ui.inc",
    )
    args = ap.parse_args()

    body_font = ImageFont.truetype(args.ttf, args.body)
    disp_font = ImageFont.truetype(args.ttf, args.disp)
    body = render_face(body_font, "kBody", args.body)
    disp = render_face(disp_font, "kDisp", args.disp)

    with args.out.open("w") as f:
        f.write("// Auto-generated UI fonts (DejaVu Sans) — larger readable sizes for e-ink\n")
        f.write("// Advance == bitmap width (AA pad included) so glyphs do not overlap.\n")
        emit(f, "kBody", args.body, *body)
        emit(f, "kDisp", args.disp, *disp)
    print(f"Wrote {args.out} body={args.body}h={body[0]} disp={args.disp}h={disp[0]}")


if __name__ == "__main__":
    main()
