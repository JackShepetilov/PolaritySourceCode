# Buildables HUD: the refusal flash colour and sound on WBP_BuildMenu.
# Run AFTER the full rebuild that adds RefusedFlashTag / RefusedSound to UBuildMenuWidget.
import unreal, json

EAL = unreal.EditorAssetLibrary
P = "/Game/Variant_Shooter/UI/Widgets/HUD/Registry/WBP_BuildMenu"
SOUND = "/Game/InfimaGames/ArtCore/Audio/_Common/S_IG_Demo_Buzzer"

def tag(name):
    t = unreal.GameplayTag()
    t.import_text('(TagName="%s")' % name)
    return t

def main():
    cls = EAL.load_blueprint_class(P)
    cdo = unreal.get_default_object(cls)
    cdo.set_editor_property("refused_flash_tag", tag("Palette.HUD.Flash.Spend"))
    snd = unreal.load_asset(SOUND)
    cdo.set_editor_property("refused_sound", snd)
    res = unreal.ToolsetRegistry.execute_tool(
        "editor_toolset.toolsets.blueprint.BlueprintTools", "compile_blueprint",
        json.dumps({"blueprint": {"refPath": P + ".WBP_BuildMenu"}}))
    print("compile error:", res.error)
    EAL.save_asset(P)
    cdo = unreal.get_default_object(EAL.load_blueprint_class(P))
    print("MODIFIED:", P, cdo.get_editor_property("refused_flash_tag").export_text(), cdo.get_editor_property("refused_sound"))

main()
