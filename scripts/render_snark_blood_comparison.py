"""Compose generated E27.1 test frames. Never opens installed game assets.

Run after the optional approved-asset tests with HLCLIENT_E271_ARTIFACT_DIR.
Requires Pillow; the input directory contains exported production GL TGA files.
"""
from pathlib import Path
import argparse
from PIL import Image, ImageDraw, ImageFont


def compose(directory: Path) -> None:
    font = ImageFont.truetype("C:/Windows/Fonts/segoeui.ttf", 19)
    small = ImageFont.truetype("C:/Windows/Fonts/segoeui.ttf", 15)
    samples = [0, 100, 250, 450, 800]
    canvas = Image.new("RGB", (1800, 650), (24, 28, 34))
    draw = ImageDraw.Draw(canvas)
    draw.text((22, 15), "E27.1 — actual approved sprites / production OpenGL", font=font, fill="white")
    draw.text((22, 45), "Same camera, floor, FOV and time. Stock-renderer capture unavailable; this is not a stock comparison.", font=small, fill=(185, 194, 205))
    for column, time in enumerate(samples):
        x = 150 + column * 328
        draw.text((x, 84), f"{time / 1000:.2f}s", font=font, fill="white")
        for row, prefix in enumerate(("before", "sound-after")):
            source = directory / f"{prefix}-{time}.tga"
            frame = Image.open(source).convert("RGB")
            if frame.size != (320, 240):
                raise ValueError("Unexpected exported framebuffer extent")
            canvas.paste(frame, (x, 118 + row * 260))
    draw.text((20, 212), "Before", font=font, fill=(235, 185, 130))
    draw.text((20, 472), "After", font=font, fill=(190, 220, 155))
    draw.text((22, 614), "After: anchored animated spray + independent imported droplets; palette195 read from gfx/palette.lmp.", font=small, fill=(185, 194, 205))
    canvas.save(directory / "blood-pop-before-after.png")

    components = Image.new("RGB", (1100, 865), (24, 28, 34))
    draw = ImageDraw.Draw(components)
    draw.text((18, 15), "E27.1 — isolated components, same production renderer", font=font, fill="white")
    for column, time in enumerate((100, 250, 450)):
        x = 120 + column * 328
        draw.text((x, 53), f"{time / 1000:.2f}s", font=font, fill="white")
        for row, prefix in enumerate(("spray", "drops", "sound-after")):
            components.paste(Image.open(directory / f"{prefix}-{time}.tga").convert("RGB"), (x, 86 + row * 250))
    for row, label in enumerate(("Spray", "Drops", "Together")):
        draw.text((12, 185 + row * 250), label, font=font, fill="white")
    components.save(directory / "blood-pop-components.png")
    Image.open(directory / "blood-original-contact-sheet.tga").save(directory / "blood-original-contact-sheet.png")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    compose(parser.parse_args().directory.resolve())
