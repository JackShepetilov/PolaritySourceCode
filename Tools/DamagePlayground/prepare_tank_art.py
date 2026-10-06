"""Reduce generated tank atlases to the approved 512px budget, adding per-cell padding and alpha."""
from pathlib import Path
from PIL import Image, ImageChops

ROOT = Path(__file__).resolve().parents[3]
ART = ROOT / "Docs/VFX_Refs_2026-10-03"
ROWS = (("tracks", 4, "T_Tank_Tracks_4x4", True), ("glass", 4, "T_Tank_Glass_4x4", True),
        ("smoke", 2, "T_Tank_Smoke_2x2", False), ("fire", 2, "T_Tank_Fire_2x2", False),
        ("sparks", 4, "T_Tank_Sparks_4x4", False), ("ricochet", 2, "T_Tank_Ricochet_2x2", False))


def main():
    destination = ART / "processed"
    destination.mkdir(exist_ok=True)
    for name, grid, texture, masked in ROWS:
        source = ART / "out" / ("tank_" + name + "_atlas.png")
        if not source.exists():
            continue
        original = Image.open(source).convert("RGB")
        result = Image.new("RGBA", (512, 512))
        cell_size = 512 // grid
        for row in range(grid):
            for column in range(grid):
                cell = original.crop((round(column * original.width / grid), round(row * original.height / grid),
                                      round((column + 1) * original.width / grid), round((row + 1) * original.height / grid)))
                cell.thumbnail((round(cell_size * .76), round(cell_size * .76)), Image.Resampling.LANCZOS)
                r, g, b = cell.split()
                alpha = ImageChops.lighter(ImageChops.lighter(r, g), b)
                if masked:
                    alpha = alpha.point(lambda value: 255 if value > 12 else 0)
                elif name == "smoke":
                    # A neutral white sprite tinted charcoal by its material, with density in alpha.
                    cell = Image.new("RGB", cell.size, (255, 255, 255))
                cell = cell.convert("RGBA")
                cell.putalpha(alpha)
                result.paste(cell, (column * cell_size + (cell_size - cell.width) // 2,
                                    row * cell_size + (cell_size - cell.height) // 2))
        target = destination / (texture + ".png")
        result.save(target)
        print(target.name, result.size)


if __name__ == "__main__":
    main()
