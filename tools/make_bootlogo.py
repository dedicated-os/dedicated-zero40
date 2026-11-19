#!/usr/bin/env python3
"""Render the BootLogo instruction image using the launcher's bitmap fonts.

Requires Pillow. From the repository root, run:
  python3 tools/make_bootlogo.py --output github/bootlogo-preview.png
  python3 tools/make_bootlogo.py --output skeleton/bootlogo.bmp
"""
import argparse
import ast
from pathlib import Path
import re

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'source/libhookdrastic/libhookdrastic.c'
ASSETS = ROOT / 'skeleton/system/assets'

# (label, shortcut, x, y). Change these entries to update the instructions.
CONTROLS = [
    ('CROP', 'G + L2', 68, 516),
    ('BRIGHTNESS', 'G + L1/R1', 228, 516),
    ('GAP', 'G + R2', 68, 588),
    ('FAST-FORWARD', 'L3', 228, 588),
    ('x2/HI-RES', 'G+STA', 68, 660),
    ('LID    MIC', 'L2  R2', 228, 660),
    ('SWAP', 'G+SEL', 68, 732),
    ('SCREENSHOT', 'G+A', 228, 732),
]


class BitmapFont:
    def __init__(self, size, source):
        # Read the same character order, widths and kerning as the launcher.
        match = re.search(r'Font\* font' + str(size) + r' = &\(Font\)\{(.*?)\n\};', source, re.S)
        if not match:
            raise ValueError(f'font{size} definition not found')
        definition = match[1]
        self.charset = ast.literal_eval(re.search(r'\.charset = (".*?")', definition)[1])
        self.size = size
        self.tracking = self.number(definition, 'tracking')
        self.offset_x = self.number(definition, 'offset_x')
        self.default_width = self.number(definition, 'char_width')
        widths = re.search(r'\.char_widths = \{(.*?)\n\t\}', definition, re.S)[1]
        char = r"'(?:\\.|[^'\\])+'"
        self.widths = {
            ast.literal_eval(key): int(value)
            for key, value in re.findall(r'\[(' + char + r')\] = (-?\d+)', widths)
        }
        kern = re.search(r'\.kern_pairs = \{(.*?)\n\t\}', definition, re.S)[1]
        self.kerning = {
            (ast.literal_eval(a), ast.literal_eval(b)): int(value)
            for a, b, value in re.findall(
                r'\[(' + char + r')\]\[(' + char + r')\] = (-?\d+)', kern
            )
        }
        self.atlas = Image.open(ASSETS / f'font-dedicated-{size}.png').convert('RGBA')

    @staticmethod
    def number(definition, name):
        return int(re.search(r'\.' + name + r' = (-?\d+)', definition)[1])

    def draw(self, image, text, x, y, color):
        x += self.offset_x
        for i, character in enumerate(text):
            if character in ('↑', '↓') and self.size == 16:
                # The atlas has no arrows; use matching solid pixel strokes.
                glyph = Image.new('RGBA', (16, 16))
                points = [(7, 0), (1, 6), (5, 6), (5, 11),
                          (9, 11), (9, 6), (13, 6)]
                if character == '↓':
                    points = [(px, 11 - py) for px, py in points]
                ImageDraw.Draw(glyph).polygon(points, fill=(255, 255, 255, 255))
            else:
                # The launcher bitmap font is uppercase-only; use its X glyph for x.
                glyph_character = 'X' if character == 'x' else character
                if glyph_character not in self.charset:
                    raise ValueError(f'Unsupported bitmap character: {character!r}')
                tile = self.charset.index(glyph_character)
                columns = self.atlas.width // self.size
                tx, ty = tile % columns * self.size, tile // columns * self.size
                glyph = self.atlas.crop((tx, ty, tx + self.size, ty + self.size))
            mask = glyph.getchannel('A')
            image.paste((0, 0, 0), (x + 2, y + 2), mask)
            image.paste(color, (x, y), mask)
            x += self.widths.get(character, self.default_width)
            if i + 1 < len(text):
                x += self.tracking + self.kerning.get((character, text[i + 1]), 0)
        if x > image.width or y + self.size + 2 > image.height:
            raise ValueError(f'Text exceeds image bounds: {text!r}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True,
                        help='Output PNG or BMP path; BMP is saved as 24-bit RGB')
    args = parser.parse_args()
    source = SOURCE.read_text()
    fonts = {size: BitmapFont(size, source) for size in (16, 24, 36)}
    image = Image.new('RGB', (480, 800), (38, 38, 38))
    fonts[24].draw(image, 'DEDICATED DS JP+', 6, 6, (187, 187, 187))
    fonts[36].draw(image, 'NINTENDO DS', 6, 42, (255, 255, 255))
    for label, shortcut, x, y in CONTROLS:
        fonts[16].draw(image, label, x, y, (187, 187, 187))
        fonts[24].draw(image, shortcut, x, y + 20, (255, 255, 255))
    fonts[16].draw(image, 'G+↑Y G+↓B', 68, 566, (255, 255, 255))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    image.save(args.output)
    print(args.output)


if __name__ == '__main__':
    main()
