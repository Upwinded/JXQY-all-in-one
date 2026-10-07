"""Draw Qingyu's geometric UI trims and wells; requires Pillow, no source image edits."""
from pathlib import Path

from PIL import Image, ImageDraw


ROOT = Path(__file__).resolve().parents[1] / "assets/common/asf/ui/qingyu"
SCALE = 3


def canvas(width, height):
    image = Image.new("RGBA", (width * SCALE, height * SCALE))
    return image, ImageDraw.Draw(image)


def box(draw, bounds, fill=None, outline=None, width=1):
    draw.rectangle(tuple(round(v * SCALE) for v in bounds), fill=fill, outline=outline, width=width * SCALE)


def line(draw, points, fill, width=1):
    draw.line([(round(x * SCALE), round(y * SCALE)) for x, y in points], fill=fill, width=width * SCALE)


def save(image, name):
    ROOT.mkdir(parents=True, exist_ok=True)
    image.resize((image.width // SCALE, image.height // SCALE), Image.Resampling.LANCZOS).save(ROOT / (name + ".png"))


def generate():
    for kind, light, dark, edge in (
        ("goods", (240, 226, 194), (211, 192, 151), (146, 111, 63)),
        ("magic", (218, 233, 218), (164, 192, 174), (71, 117, 100)),
    ):
        for hovered in (False, True):
            image, draw = canvas(96, 96)
            box(draw, (1, 2, 94, 95), fill=(28, 45, 36, 95))
            box(draw, (1, 1, 94, 93), fill=edge)
            for y in range(3, 92):
                ratio = (y - 3) / 88
                shade = tuple(min(255, round(a * (1 - ratio) + b * ratio) + (9 if hovered else 0)) for a, b in zip(light, dark))
                line(draw, [(3, y), (92, y)], shade)
            box(draw, (6, 6, 89, 88), outline=(*edge, 125))
            line(draw, [(3, 91), (3, 3), (92, 3)], (255, 247, 218, 220))
            for x, y, sx, sy in ((9, 9, 1, 1), (86, 9, -1, 1), (9, 85, 1, -1), (86, 85, -1, -1)):
                line(draw, [(x, y + sy * 12), (x, y), (x + sx * 12, y)], (*edge, 220))
                box(draw, (x + sx * 4 - 1, y + sy * 4 - 1, x + sx * 4 + 1, y + sy * 4 + 1), fill=(248, 229, 172))
            if hovered:
                box(draw, (1, 1, 94, 93), outline=(224, 179, 82), width=2)
            save(image, "slot-" + kind + ("-hover" if hovered else ""))

        image, draw = canvas(44, 44)
        box(draw, (1, 1, 42, 42), fill=(*edge, 255))
        box(draw, (3, 3, 40, 40), outline=(234, 204, 141))
        line(draw, [(8, 7), (7, 7), (7, 12)], (255, 230, 174))
        line(draw, [(36, 32), (36, 36), (31, 36)], (255, 230, 174))
        save(image, "badge-" + kind)

    image, draw = canvas(256, 48)
    points = [(1, 24), (15, 7), (241, 7), (255, 24), (241, 41), (15, 41)]
    draw.polygon([(x * SCALE, y * SCALE) for x, y in points], fill=(34, 76, 63), outline=(169, 137, 78), width=2 * SCALE)
    line(draw, [(21, 11), (235, 11)], (131, 157, 121))
    line(draw, [(21, 37), (235, 37)], (184, 149, 83))
    for x in (18, 238):
        draw.polygon([(x * SCALE, 19 * SCALE), ((x + 4) * SCALE, 24 * SCALE), (x * SCALE, 29 * SCALE), ((x - 4) * SCALE, 24 * SCALE)], fill=(222, 188, 114))
    save(image, "heading")

    image, draw = canvas(256, 12)
    line(draw, [(0, 5), (112, 5)], (155, 129, 77, 120))
    line(draw, [(144, 5), (255, 5)], (155, 129, 77, 120))
    line(draw, [(117, 5), (128, 1), (139, 5), (128, 9), (117, 5)], (155, 129, 77, 200))
    save(image, "rule")

    image, draw = canvas(192, 48)
    box(draw, (0, 0, 191, 47), outline=(169, 137, 78))
    box(draw, (3, 3, 188, 44), outline=(230, 207, 148, 130))
    line(draw, [(6, 6), (185, 6)], (255, 247, 218, 65))
    line(draw, [(4, 43), (187, 43)], (22, 48, 37, 90))
    save(image, "button-trim")

    image, draw = canvas(64, 64)
    box(draw, (0, 0, 63, 63), fill=(100, 98, 64, 24), outline=(141, 130, 89, 100))
    box(draw, (2, 2, 61, 61), outline=(255, 251, 231, 165))
    save(image, "inset")

    image, draw = canvas(20, 18)
    box(draw, (0, 0, 19, 17), fill=(38, 69, 55, 235), outline=(201, 173, 111))
    save(image, "key-cap")


if __name__ == "__main__":
    generate()
    print(f"Qingyu control art written to {ROOT}")
