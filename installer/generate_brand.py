"""Generate every raster form of the Keys mark from one description of it.

Mirrors Octavium's installer/generate_wizard_images.py, but does the icon too,
because Keys has one mark and four places that want it at different sizes:

    assets/Keys.ico            16/32/48/64/128/256, SetupIconFile
    assets/logo-1024.png       README and the release page
    installer/wizard_large.bmp 164 x 314, WizardImageFile
    installer/wizard_small.bmp  55 x  58, WizardSmallImageFile

assets/Keys.svg is the master and carries the same geometry as `draw_mark`
below. Redraw there, mirror it here, regenerate. There is no rasteriser in the
toolchain (no cairosvg), so the two are kept in step by hand; they are twelve
shapes, which is cheaper than the dependency.

Everything is drawn at SS times the target and downsampled with LANCZOS. That
matters most at 16 px, where drawing directly would alias the diagonal arms
into a staircase.

    py installer/generate_brand.py

Requires Pillow only.
"""

from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
ASSETS = ROOT / "assets"
INSTALLER = ROOT / "installer"

# src/ui/KeysLookAndFeel.h
TILE_TOP = (0x1C, 0x1F, 0x25)
TILE_BOT = (0x0B, 0x0C, 0x0F)
IVORY_TOP = (0xFF, 0xFF, 0xFF)
IVORY_BOT = (0xCD, 0xD3, 0xDB)
ACCENT = (0x35, 0xC4, 0xD7)
INK = (0xE9, 0xEC, 0xF0)
INK_FAINT = (0x7D, 0x84, 0x90)

SS = 4  # supersample factor
BASE = 256  # the SVG's viewBox


def vertical_gradient(size, top, bottom):
    """A vertical gradient as an RGB image."""
    w, h = size
    img = Image.new("RGB", size)
    draw = ImageDraw.Draw(img)
    for y in range(h):
        t = y / max(h - 1, 1)
        draw.line(
            [(0, y), (w, y)],
            fill=tuple(round(top[i] + (bottom[i] - top[i]) * t) for i in range(3)),
        )
    return img


def round_capped_line(draw, p0, p1, width, fill):
    """Pillow has no round line cap, so the caps are drawn as circles.

    Without this the arms end in flat diagonal chops that read as broken
    strokes at icon sizes.
    """
    draw.line([p0, p1], fill=fill, width=width)
    r = width / 2
    for x, y in (p0, p1):
        draw.ellipse([x - r, y - r, x + r, y + r], fill=fill)


def draw_mark(px, tile=True):
    """The Keys mark at px by px, RGBA. Geometry mirrors assets/Keys.svg."""
    n = px * SS
    s = n / BASE  # SVG units -> supersampled pixels

    img = Image.new("RGBA", (n, n), (0, 0, 0, 0))

    if tile:
        grad = vertical_gradient((n, n), TILE_TOP, TILE_BOT).convert("RGBA")
        mask = Image.new("L", (n, n), 0)
        ImageDraw.Draw(mask).rounded_rectangle(
            [0, 0, n - 1, n - 1], radius=round(56 * s), fill=255
        )
        img.paste(grad, (0, 0), mask)

    # Stem: the white key. A gradient, so it reads as a key face rather than a bar.
    stem = [round(60 * s), round(56 * s), round(96 * s), round(200 * s)]
    stem_w, stem_h = stem[2] - stem[0], stem[3] - stem[1]
    stem_grad = vertical_gradient((stem_w, stem_h), IVORY_TOP, IVORY_BOT).convert("RGBA")
    stem_mask = Image.new("L", (stem_w, stem_h), 0)
    ImageDraw.Draw(stem_mask).rounded_rectangle(
        [0, 0, stem_w - 1, stem_h - 1], radius=round(8 * s), fill=255
    )
    img.paste(stem_grad, (stem[0], stem[1]), stem_mask)

    draw = ImageDraw.Draw(img)
    arm_w = round(36 * s)
    junction = (104 * s, 128 * s)
    # Upper arm carries the accent; the lower arm goes last so it sits on top
    # at the junction, exactly as in the SVG.
    round_capped_line(draw, junction, (178 * s, 62 * s), arm_w, ACCENT)
    round_capped_line(draw, junction, (178 * s, 194 * s), arm_w, INK)

    return img.resize((px, px), Image.Resampling.LANCZOS)


def pick_font(size, bold=True):
    names = (
        ["segoeuib.ttf", "arialbd.ttf", "calibrib.ttf"]
        if bold
        else ["segoeui.ttf", "arial.ttf", "calibri.ttf"]
    )
    for name in names:
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            continue
    return ImageFont.load_default()


def centred_text(draw, cx, y, text, font, fill, tracking=0):
    """Draw text centred on cx, with optional letter-spacing."""
    if not tracking:
        w = draw.textbbox((0, 0), text, font=font)[2]
        draw.text((cx - w / 2, y), text, font=font, fill=fill)
        return
    widths = [draw.textbbox((0, 0), ch, font=font)[2] for ch in text]
    total = sum(widths) + tracking * (len(text) - 1)
    x = cx - total / 2
    for ch, w in zip(text, widths):
        draw.text((x, y), ch, font=font, fill=fill)
        x += w + tracking


def wizard_large(path):
    """164 x 314, the tall panel down the left of the wizard."""
    w, h = 164, 314
    img = vertical_gradient((w, h), (0x1C, 0x2A, 0x30), TILE_BOT)
    draw = ImageDraw.Draw(img)

    mark = draw_mark(96)
    img.paste(mark, ((w - 96) // 2, 56), mark)

    # The rule sits below the wordmark's descent, not through it: a 30 px face
    # set at y=170 runs to about y=208, so the first cut of this drew the accent
    # line straight across the middle of the word.
    centred_text(draw, w / 2, 170, "KEYS", pick_font(30), INK, tracking=3)
    draw.line([(52, 216), (w - 52, 216)], fill=ACCENT, width=1)
    centred_text(draw, w / 2, 226, "OK STUDIO", pick_font(11, bold=False), INK_FAINT, tracking=2)

    img.save(path, "BMP")
    return path


def wizard_small(path):
    """55 x 58, the little one in the wizard's top-right corner."""
    w, h = 55, 58
    img = vertical_gradient((w, h), (0x1C, 0x2A, 0x30), TILE_BOT)
    mark = draw_mark(44)
    img.paste(mark, ((w - 44) // 2, (h - 44) // 2), mark)
    img.save(path, "BMP")
    return path


def main():
    ASSETS.mkdir(exist_ok=True)

    logo = draw_mark(1024)
    logo.save(ASSETS / "logo-1024.png")
    print(f"  {ASSETS / 'logo-1024.png'}")

    # Each frame is drawn at its own size rather than downsampled from one big
    # one: the tile's corner radius and the arm width are proportional, so a
    # 16 px frame drawn natively keeps its shape where a resized 256 goes muddy.
    sizes = [256, 128, 64, 48, 32, 16]
    frames = [draw_mark(s) for s in sizes]
    frames[0].save(
        ASSETS / "Keys.ico",
        format="ICO",
        sizes=[(s, s) for s in sizes],
        append_images=frames[1:],
    )
    print(f"  {ASSETS / 'Keys.ico'}  ({', '.join(str(s) for s in sizes)})")

    print(f"  {wizard_large(INSTALLER / 'wizard_large.bmp')}")
    print(f"  {wizard_small(INSTALLER / 'wizard_small.bmp')}")


if __name__ == "__main__":
    print("Generating the Keys mark:")
    main()
    print("Done.")
