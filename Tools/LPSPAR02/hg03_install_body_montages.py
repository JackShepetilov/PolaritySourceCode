import unreal

assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
BASE = '/Game/Variant_Shooter/Tests/LPSP_Handgun_03'
SOURCE = '/Game/InfimaGames/AnimatedLowPolyWeapons/Art/Characters/Animations/Handguns/'
WEAPON = '/Game/Variant_Shooter/Blueprints/Pickups/Weapons/Kinemation/Pistols/BP_M1911'
COPIES = {
    'AM_HG03_TP_Reload_Empty': 'AM_TP_CH_Handgun_Reload_Empty',
    'AM_HG03_TP_Fire': 'AM_TP_CH_Handgun_Fire',
    'AM_HG03_TP_Holster': 'AM_TP_CH_Handgun_Holster',
    'AM_HG03_TP_Unholster': 'AM_TP_CH_Handgun_Unholster',
}

unreal.EditorAssetLibrary.make_directory(BASE)
assets = {}
for copy_name, source_name in COPIES.items():
    path = BASE + '/' + copy_name
    if not unreal.EditorAssetLibrary.does_asset_exist(path):
        assert unreal.EditorAssetLibrary.duplicate_asset(SOURCE + source_name, path)
        print('CREATED', path)
    montage = unreal.EditorAssetLibrary.load_asset(path)
    tracks = list(montage.get_editor_property('slot_anim_tracks'))
    names = [str(track.get_editor_property('slot_name')) for track in tracks]
    if names == ['DefaultSlot']:
        standing = tracks[0]
        aiming = unreal.SlotAnimationTrack()
        aiming.import_text(standing.export_text())
        standing.set_editor_property('slot_name', 'Action Standing')
        aiming.set_editor_property('slot_name', 'Action Aiming')
        montage.set_editor_property('slot_anim_tracks', [standing, aiming])
        print('MODIFIED', path, 'Action Standing + Action Aiming')
    else:
        assert names == ['Action Standing', 'Action Aiming'], (path, names)
    assert unreal.EditorAssetLibrary.save_loaded_asset(montage, False)
    assets[copy_name] = montage

bp = unreal.EditorAssetLibrary.load_asset(WEAPON)
cdo = unreal.get_default_object(unreal.EditorAssetLibrary.load_blueprint_class(WEAPON))
assignments = {
    'ReloadMontageTP': assets['AM_HG03_TP_Reload_Empty'],
    'FiringMontageTP': assets['AM_HG03_TP_Fire'],
    'DrawMontageTP': assets['AM_HG03_TP_Unholster'],
    'HolsterMontageTP': assets['AM_HG03_TP_Holster'],
}
for field, montage in assignments.items():
    cdo.set_editor_property(field, montage)
    print('MODIFIED', WEAPON, field, montage.get_name())
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
assert unreal.EditorAssetLibrary.save_loaded_asset(bp, False)
print('SAVED', WEAPON)
