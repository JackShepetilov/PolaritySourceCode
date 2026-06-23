# Import the generated temple-plaza pattern PNG into UE as a color (sRGB) Texture2D.
# Log: [IMPORT_TP].

import os
import unreal

TAG = "[IMPORT_TP]"
SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)), "DownloadedTextures", "temple_pattern.png")
DEST = "/Game/ArenaArtPass/Textures"
NAME = "temple_pattern"


def log(m):
    unreal.log("{} {}".format(TAG, m))


def main():
    if not os.path.isfile(SRC):
        unreal.log_warning("{} source missing: {}".format(TAG, SRC))
        return
    eal = unreal.EditorAssetLibrary
    if not eal.does_directory_exist(DEST):
        eal.make_directory(DEST)
    t = unreal.AssetImportTask()
    t.set_editor_property("filename", SRC)
    t.set_editor_property("destination_path", DEST)
    t.set_editor_property("destination_name", NAME)
    t.set_editor_property("automated", True)
    t.set_editor_property("save", True)
    t.set_editor_property("replace_existing", True)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t])
    p = "{}/{}.{}".format(DEST, NAME, NAME)
    log("imported {} -> {} exists={}".format(SRC, p, eal.does_asset_exist(p)))
    log("RESULT: SUCCESS")


main()
