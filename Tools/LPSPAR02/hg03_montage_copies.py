import unreal

# Pistol step 1: project copies of the pack's handgun character montages with the slots our body graph
# actually plays. The pack authored these three with a single DefaultSlot, which the body graph's
# Action Standing / Action Aiming slot nodes never pick up.
#
# Nothing in the production weapon BP is touched here.

assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world(), 'PIE is running'

BASE = '/Game/Variant_Shooter/Tests/LPSP_Handgun_03'
SRC = '/Game/InfimaGames/AnimatedLowPolyWeapons/Art/Characters/Animations/Handguns/'
COPIES = {
    'AM_HG03_TP_Reload_Empty': 'AM_TP_CH_Handgun_Reload_Empty',
    'AM_HG03_TP_Holster': 'AM_TP_CH_Handgun_Holster',
    'AM_HG03_TP_Unholster': 'AM_TP_CH_Handgun_Unholster',
}

print('SET_PROPERTY_AVAILABLE', hasattr(unreal.BlueprintService, 'set_property'))
unreal.EditorAssetLibrary.make_directory(BASE)
for name, source in COPIES.items():
    path = BASE + '/' + name
    if not unreal.EditorAssetLibrary.does_asset_exist(path):
        assert unreal.EditorAssetLibrary.duplicate_asset(SRC + source, path), path
        print('CREATED', path)
    montage = unreal.load_asset(path)
    tracks = list(montage.get_editor_property('slot_anim_tracks'))
    slot_names = [str(t.get_editor_property('slot_name')) for t in tracks]
    if slot_names != ['Action Standing', 'Action Aiming']:
        assert len(tracks) == 1 and slot_names == ['DefaultSlot'], slot_names
        standing, aiming = tracks[0], unreal.SlotAnimationTrack()
        aiming.import_text(standing.export_text())
        standing.set_editor_property('slot_name', 'Action Standing')
        aiming.set_editor_property('slot_name', 'Action Aiming')
        montage.set_editor_property('slot_anim_tracks', [standing, aiming])
        print('MODIFIED', path, 'DefaultSlot -> Action Standing + Action Aiming')
    assert unreal.EditorAssetLibrary.save_asset(path, False), path
    check = unreal.load_asset(path)
    print('VERIFY', path, [str(t.get_editor_property('slot_name'))
                          for t in check.get_editor_property('slot_anim_tracks')])
print('DONE')
