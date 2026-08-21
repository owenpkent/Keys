"""Generate every raster form of the Keys mark from one description of it.

Mirrors Octavium's installer/generate_wizard_images.py, but does the icon too,
because Keys has one mark and four places that want it at different sizes:

    assets/Keys.ico            16/32/48/64/128/256, SetupIconFile
    assets/logo-1024.png       README and the release page
    installer/wizard_large.bmp 164 x 314, WizardImageFile
    installer/wizard_small.bmp  55 x  58, WizardSmallImageFile

assets/Keys.svg is the master and carries the same geometry as `draw_mark`
below. Redraw there, mirror it here, regenerate. There is no rasteriser in the
toolchain (no cairosvg), so the two are kept in step by hand; they are six
shapes, which is cheaper than the dependency.

Everything is drawn at SS times the target and downsampled with LANCZOS. That
matters most at 16 px, where the two black keys are barely two pixels wide and
drawing them directly would drop one of them to nothing.

    py installer/generate_brand.py

Requires Pillow only.
"""

from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
ASSETS = ROOT / "assets"
INSTALLER = ROOT / "installer"

# src/ui/KeysLookAndFeel.h. These are the token values as they actually stand -
# the mark used to claim #1c1f25 / #0b0c0f were bgTop and bgBot, and they are
# headerTop and a bgBot from some earlier build.
GROUND = (0x0E, 0x0F, 0x12)  # skin::bgBot
IVORY_TOP = (0xFF, 0xFF, 0xFF)
IVORY_BOT = (0xCD, 0xD3, 0xDB)
LIT_TOP = (0x35, 0xC4, 0xD7)  # skin::cyanAccent.base
LIT_BOT = (0x1B, 0x84, 0x96)  # skin::cyanAccent.deep
EBONY_TOP = (0x17, 0x18, 0x1C)  # skin::bgTop
EBONY_BOT = (0x10, 0x12, 0x16)  # skin::well
ACCENT = LIT_TOP
INK = (0xE9, 0xEC, 0xF0)  # skin::text
INK_FAINT = (0x7D, 0x84, 0x90)
# The wizard panel's own ground, which is not the mark's: a teal-shifted charcoal
# so the tile reads as an object sitting on a panel rather than a hole in it.
PANEL_TOP = (0x1C, 0x2A, 0x30)
PANEL_BOT = (0x0E, 0x0F, 0x12)

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


def key_mask(size, radius):
    """A piano key's outline: square at the shoulder, rounded at the foot.

    Pillow rounds all four corners or none, so the top pair is squared off by
    overdrawing a plain rectangle down to the radius. That one detail is what
    makes the mark read as a keyboard instead of three rounded bars.
    """
    w, h = size
    mask = Image.new("L", size, 0)
    d = ImageDraw.Draw(mask)
    d.rounded_rectangle([0, 0, w - 1, h - 1], radius=radius, fill=255)
    d.rectangle([0, 0, w - 1, radius], fill=255)
    return mask


def paste_key(img, box, radius, top, bottom):
    """One shaded key face, pasted through its own outline."""
    x0, y0, x1, y1 = box
    size = (x1 - x0, y1 - y0)
    grad = vertical_gradient(size, top, bottom).convert("RGBA")
    img.paste(grad, (x0, y0), key_mask(size, radius))


def draw_mark(px, tile=True):
    """The Keys mark at px by px, RGBA. Geometry mirrors assets/Keys.svg.

    Three white keys with two black keys between them, the middle one lit. The
    block is 202 x 172 at (27, 42) in the 256 grid, so the air is 27 either side
    and 42 top and bottom - centred, which the K it replaced was not.

    It fills ~79% of the tile's width, which is a legibility floor rather than a
    taste: three keys and two gaps across a 16 px icon put every gap under half a
    pixel at half this size, and the keys blurred into a single bar.
    """
    n = px * SS
    s = n / BASE  # SVG units -> supersampled pixels

    def u(v):
        return round(v * s)

    img = Image.new("RGBA", (n, n), (0, 0, 0, 0))

    if tile:
        # Flat, not a gradient. The keys carry all the shading the mark needs,
        # and a gradient behind them only muddied the foot of the white ones.
        mask = Image.new("L", (n, n), 0)
        ImageDraw.Draw(mask).rounded_rectangle(
            [0, 0, n - 1, n - 1], radius=u(56), fill=255
        )
        img.paste(Image.new("RGBA", (n, n), GROUND + (255,)), (0, 0), mask)

    for i, x in enumerate((27, 99, 171)):
        lit = i == 1
        paste_key(
            img,
            (u(x), u(42), u(x + 58), u(214)),
            u(11),
            LIT_TOP if lit else IVORY_TOP,
            LIT_BOT if lit else IVORY_BOT,
        )

    # Last, so they sit over the white faces including the lit one - which is
    # what stops the accent reading as a stripe painted between two keys.
    for x in (75, 147):
        paste_key(img, (u(x), u(42), u(x + 34), u(146)), u(4), EBONY_TOP, EBONY_BOT)

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
    img = vertical_gradient((w, h), PANEL_TOP, PANEL_BOT)
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
    img = vertical_gradient((w, h), PANEL_TOP, PANEL_BOT)
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
