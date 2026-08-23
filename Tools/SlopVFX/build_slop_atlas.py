from __future__ import annotations

import argparse
import json
import math
import re
from pathlib import Path

from PIL import Image


DEFAULT_TILE_SIZE = 512
DEFAULT_PADDING = 8
BASE_MESH_ASPECT = 0.22 / 0.14


def natural_key(path: Path) -> list[object]:
    return [
        int(part) if part.isdigit() else part.casefold()
        for part in re.split(r"(\d+)", path.name)
    ]


def next_power_of_two(value: int) -> int:
    result = 1
    while result < value:
        result *= 2
    return result


def build_atlas(
    input_dir: Path,
    output_dir: Path,
    tile_size: int,
    padding: int,
) -> tuple[Path, Path, Path]:
    sources = sorted(input_dir.glob("*.png"), key=natural_key)
    if not sources:
        raise RuntimeError(f"No PNG files found in {input_dir}")

    if padding * 2 >= tile_size:
        raise ValueError("Padding must be smaller than half of tile size")

    grid_x = next_power_of_two(math.ceil(math.sqrt(len(sources))))
    grid_y = next_power_of_two(math.ceil(len(sources) / grid_x))
    atlas_width = grid_x * tile_size
    atlas_height = grid_y * tile_size
    atlas = Image.new("RGBA", (atlas_width, atlas_height), (0, 0, 0, 0))
    inner_size = tile_size - padding * 2
    cards: list[dict[str, object]] = []

    for index, source in enumerate(sources):
        with Image.open(source) as image:
            rgba = image.convert("RGBA")
            width, height = rgba.size
            if width <= 0 or height <= 0:
                raise RuntimeError(f"Invalid image size: {source}")

            aspect = width / height
            relative_aspect = aspect / BASE_MESH_ASPECT
            scale_x = math.sqrt(relative_aspect)
            scale_y = 1.0 / scale_x

            # The square-cell stretch is intentional. The Niagara plane applies
            # the inverse geometric aspect correction, restoring the source image.
            tile = rgba.resize((inner_size, inner_size), Image.Resampling.LANCZOS)
            column = index % grid_x
            row = index // grid_x
            x = column * tile_size + padding
            y = row * tile_size + padding
            atlas.alpha_composite(tile, (x, y))

            cards.append(
                {
                    "index": index,
                    "file": source.name,
                    "width": width,
                    "height": height,
                    "aspect": aspect,
                    "particle_scale": [scale_x, scale_y, 1.0],
                    "cell": [column, row],
                }
            )

    output_dir.mkdir(parents=True, exist_ok=True)
    atlas_path = output_dir / "T_SlopMemes_Atlas.png"
    manifest_path = output_dir / "SlopMemes_Atlas.json"
    hlsl_path = output_dir / "AssignSlopAtlasCard.hlsl"
    atlas.save(atlas_path, optimize=True)

    manifest = {
        "version": 1,
        "source_directory": str(input_dir.resolve()),
        "atlas_file": atlas_path.name,
        "hlsl_file": hlsl_path.name,
        "image_count": len(cards),
        "grid_x": grid_x,
        "grid_y": grid_y,
        "tile_size": tile_size,
        "padding": padding,
        "atlas_size": [atlas_width, atlas_height],
        "base_mesh_aspect": BASE_MESH_ASPECT,
        "cards": cards,
    }
    manifest_path.write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2),
        encoding="utf-8",
    )

    hlsl_lines = [
        (
            "float CardIndex = VariantIndex >= 0.0 "
            "? floor(fmod(VariantIndex, "
            f"{float(len(cards)):.1f})) "
            ": floor(fmod(abs(EngineTime * 37.0 + float(UniqueID) * 7.0), "
            f"{float(len(cards)):.1f}));"
        ),
        "float SizeMultiplier = 1.0;",
        "if (AreaMultiplier > 0.0) SizeMultiplier = sqrt(AreaMultiplier);",
        (
            "float2 CardScale = float2("
            f"{cards[0]['particle_scale'][0]:.9f}, "
            f"{cards[0]['particle_scale'][1]:.9f});"
        ),
    ]
    for card in cards[1:]:
        scale_x, scale_y, _ = card["particle_scale"]
        lower = card["index"] - 0.5
        upper = card["index"] + 0.5
        condition = (
            f"CardIndex > {lower:.1f}"
            if card["index"] == len(cards) - 1
            else f"CardIndex > {lower:.1f} && CardIndex < {upper:.1f}"
        )
        hlsl_lines.append(
            f"if ({condition}) "
            f"CardScale = float2({scale_x:.9f}, {scale_y:.9f});"
        )
    hlsl_lines.extend(
        [
            "SubImageIndex = float(CardIndex);",
            (
                "Scale = float3(CardScale.x * SizeMultiplier, "
                "CardScale.y * SizeMultiplier, 1.0);"
            ),
        ]
    )
    hlsl_path.write_text("\n".join(hlsl_lines) + "\n", encoding="utf-8")
    return atlas_path, manifest_path, hlsl_path


def main() -> None:
    project_root = Path(__file__).resolve().parents[3]
    default_input = (
        project_root / "Source" / "ArtRefs" / "DataCenter" / "SlopVFX_Inbox"
    )
    default_output = (
        project_root / "Source" / "ArtRefs" / "DataCenter" / "SlopVFX_Generated"
    )

    parser = argparse.ArgumentParser(description="Build the Slop VFX texture atlas")
    parser.add_argument("--input", type=Path, default=default_input)
    parser.add_argument("--output", type=Path, default=default_output)
    parser.add_argument("--tile-size", type=int, default=DEFAULT_TILE_SIZE)
    parser.add_argument("--padding", type=int, default=DEFAULT_PADDING)
    args = parser.parse_args()

    atlas_path, manifest_path, hlsl_path = build_atlas(
        args.input.resolve(),
        args.output.resolve(),
        args.tile_size,
        args.padding,
    )
    print(atlas_path)
    print(manifest_path)
    print(hlsl_path)


if __name__ == "__main__":
    main()
