from __future__ import annotations

import shutil
from pathlib import Path

from PIL import Image


SUPPORTED_EXTENSIONS = {".png", ".jpg", ".jpeg", ".webp"}
EXCLUDED_STEMS = {"CONTACT_SHEET"}


def main() -> None:
    project_root = Path(__file__).resolve().parents[3]
    source_dir = (
        project_root / "Source" / "ArtRefs" / "DataCenter" / "SlopReferences"
    )
    destination_dir = (
        project_root / "Source" / "ArtRefs" / "DataCenter" / "SlopVFX_Inbox"
    )
    destination_dir.mkdir(parents=True, exist_ok=True)

    sources = sorted(
        path
        for path in source_dir.iterdir()
        if path.is_file()
        and path.suffix.casefold() in SUPPORTED_EXTENSIONS
        and path.stem not in EXCLUDED_STEMS
    )

    for source in sources:
        destination = destination_dir / f"{source.stem}.png"
        if source.suffix.casefold() == ".png":
            shutil.copy2(source, destination)
        else:
            with Image.open(source) as image:
                image.convert("RGBA").save(destination, "PNG", optimize=True)
        print(f"{source.name} -> {destination.name}")

    print(f"TOTAL={len(sources)}")


if __name__ == "__main__":
    main()
