"""Rasterize the vendored, licensed Lucide SVG geometry for SDL's PNG loader.

Run with CairoSVG installed. No network access or runtime SVG library is used.
The original SVGs, pinned upstream revision and complete licenses are packaged.
"""
from pathlib import Path

import cairosvg


def main() -> None:
    directory = Path(__file__).resolve().parents[1] / "content" / "assets" / "icons"
    for source in sorted((directory / "lucide-source").glob("*.svg")):
        geometry = source.read_text(encoding="utf-8").replace("currentColor", "#ffffff")
        destination = directory / (source.stem + ".png")
        cairosvg.svg2png(bytestring=geometry.encode("utf-8"), write_to=str(destination),
                        output_width=128, output_height=128)
        print(destination)


if __name__ == "__main__":
    main()
