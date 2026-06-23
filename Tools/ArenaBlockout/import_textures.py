# Import the downloaded normal (_nor_gl) and ARM (_arm) maps for the chosen textures into UE,
# next to the lead's diffuse imports at /Game/ArenaArtPass/Textures/ (flat). Imported LINEAR
# (sRGB off) so the villa master can world-project them via WorldCoordinate3Way and unpack the
# normal manually. Run via REST. Log: [IMPORT_TEX].

import os
import unreal

TAG = "[IMPORT_TEX]"
TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
SRC_ROOT = os.path.join(TOOLS_DIR, "DownloadedTextures")
DEST = "/Game/ArenaArtPass/Textures"
CATS = ["wall", "floor", "stone", "roof"]


def log(m):
    unreal.log("{} {}".format(TAG, m))


def warn(m):
    unreal.log_warning("{} {}".format(TAG, m))


def main():
    eal = unreal.EditorAssetLibrary
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    if not eal.does_directory_exist(DEST):
        eal.make_directory(DEST)
    tasks = []
    names = []
    for cat in CATS:
        d = os.path.join(SRC_ROOT, cat)
        if not os.path.isdir(d):
            continue
        for f in os.listdir(d):
            low = f.lower()
            if not (low.endswith("_nor_gl.jpg") or low.endswith("_arm.jpg")):
                continue
            full = os.path.join(d, f)
            if os.path.getsize(full) < 2000:
                continue
            name = os.path.splitext(f)[0]  # e.g. beige_wall_001_nor_gl  (matches lead's plain naming)
            t = unreal.AssetImportTask()
            t.set_editor_property("filename", full)
            t.set_editor_property("destination_path", DEST)
            t.set_editor_property("destination_name", name)
            t.set_editor_property("automated", True)
            t.set_editor_property("save", True)
            t.set_editor_property("replace_existing", True)
            tasks.append(t)
            names.append(name)
    if not tasks:
        warn("no _nor_gl/_arm files found under {}".format(SRC_ROOT))
        log("RESULT: FAILED")
        return
    tools.import_asset_tasks(tasks)
    # set linear + sensible compression so WCW can sample them as data
    fixed = 0
    for name in names:
        p = "{}/{}.{}".format(DEST, name, name)
        tex = eal.load_asset(p)
        if tex is None:
            warn("post-import load fail: {}".format(p))
            continue
        try:
            tex.set_editor_property("srgb", False)
            if name.endswith("_arm"):
                tex.set_editor_property("compression_settings",
                                        unreal.TextureCompressionSettings.TC_MASKS)
            # _nor_gl: keep default (DXT) but linear; we unpack in the material
            eal.save_asset(p)
            fixed += 1
        except Exception as e:
            warn("settings {}: {}".format(name, e))
    log("imported {} maps, settings fixed {}".format(len(tasks), fixed))
    log("RESULT: SUCCESS")


main()
