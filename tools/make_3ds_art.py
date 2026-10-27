"""Make the 3DS pictures from art cut out of the port's own frames of your disc (so none of it goes in git).

Inputs in build/cia/: title_raw.png (a 384x280 title-screen frame), mario_054600.png and mario_056200.png (Mario
walking and jumping, cut from a stage). Outputs in build/cia/:
  banner.png  256x128  home-menu banner          icon.png  48x48  home-menu icon
  bottom.png  320x240  bottom-screen picture     bottom.bin       the same, raw RGB for the 3DS
Copy bottom.bin to sdmc:/3ds/HotelMario/bottom.bin.
"""
import os

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ART = os.path.join(ROOT, "build", "cia")
FONT = r"C:\Windows\Fonts\segoeuib.ttf"


def load():
    title = Image.open(os.path.join(ART, "title_raw.png")).convert("RGB")
    walk = Image.open(os.path.join(ART, "mario_054600.png")).transpose(Image.FLIP_LEFT_RIGHT)
    jump = Image.open(os.path.join(ART, "mario_056200.png"))
    return title, walk, jump


def outlined(draw, xy, text, font, fill, outline=(24, 14, 20), width=3, anchor="mm"):
    draw.text(xy, text, font=font, fill=fill, anchor=anchor, stroke_width=width, stroke_fill=outline)


def bottom(title, walk, jump):
    """Plain black: the controls, and the credit underneath. Nothing from the game."""
    w, h = 320, 240
    img = Image.new("RGB", (w, h), (0, 0, 0))
    d = ImageDraw.Draw(img)
    key = ImageFont.truetype(FONT, 15)
    small = ImageFont.truetype(FONT, 11)
    rows = [("A", "Button 1"), ("B", "Button 2"), ("Circle Pad / D-Pad", "Move"),
            ("SELECT", "Info"), ("START + SELECT", "Quit")]
    y = 52
    for k, what in rows:
        d.text((w // 2 - 8, y), k, font=key, fill=(255, 255, 255), anchor="rm")
        d.text((w // 2 + 8, y), what, font=key, fill=(150, 150, 150), anchor="lm")
        y += 26
    d.text((w // 2, h - 18), "Port by Anton Retro", font=small, fill=(110, 110, 110), anchor="mm")
    return img


def banner(title, walk, jump):
    top = title.crop((24, 8, 360, 104))
    ground = title.crop((24, 189, 360, 241))
    comp = Image.new("RGB", (336, 148))
    comp.paste(top, (0, 0))
    comp.paste(ground, (0, 96))
    ban = comp.resize((256, 128), Image.LANCZOS).convert("RGBA")
    ban.alpha_composite(walk, (34, 92))
    ban.alpha_composite(jump, (190, 72))
    return ban.convert("RGB")


def icon(title, jump):
    ic = Image.new("RGBA", (48, 48), title.getpixel((10, 60)) + (255,))
    ic.alpha_composite(jump.resize((44, 47), Image.NEAREST), (2, 1))
    return ic.convert("RGB")


def main():
    title, walk, jump = load()
    b = bottom(title, walk, jump)
    b.save(os.path.join(ART, "bottom.png"))
    with open(os.path.join(ART, "bottom.bin"), "wb") as f:
        f.write(b.tobytes())                                 # 320 x 240 x RGB, rows top to bottom
    banner(title, walk, jump).save(os.path.join(ART, "banner.png"))
    icon(title, jump).save(os.path.join(ART, "icon.png"))
    print("made bottom.png/bin, banner.png, icon.png in", ART)


if __name__ == "__main__":
    main()
