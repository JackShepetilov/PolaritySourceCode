# BP_AKX203: third person mesh -> SK_AR_01_AKX203, the Infima magazine part off (the AKX magazine is in
# the mesh, on the magazine bone), no dropped magazine. Everything else of its TP kit stays as it is
# (the mapping tool would also put back the stock reload montage the project replaced).
import unreal
EAL = unreal.EditorAssetLibrary
BP = '/Game/Variant_Shooter/Blueprints/Pickups/Weapons/Kinemation/ARs/BP_AKX203'
MESH = '/Game/Variant_Shooter/Meshes/Weapons_TP/SK_AR_01_AKX203'
BACKUP = '/Game/Variant_Shooter/Tests/LPSP_TP_Backups/BP_AKX203_BeforeAKXDouble'

assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world(), 'Stop PIE first'
if not EAL.does_asset_exist(BACKUP):
    assert EAL.duplicate_asset(BP, BACKUP), BACKUP
    print('CREATED backup', BACKUP)
cls = EAL.load_blueprint_class(BP)
cdo = unreal.get_default_object(cls)
tp = next(c for c in cdo.get_components_by_class(unreal.SkeletalMeshComponent) if c.get_name() == 'Third Person Mesh')
print('BEFORE mesh', tp.get_editor_property('skeletal_mesh_asset').get_path_name(),
      'row', cdo.get_editor_property('InfimaTPMagazineRowOverride'), 'drop', cdo.get_editor_property('InfimaTPMagazineDropClass'))
tp.set_editor_property('skeletal_mesh_asset', unreal.load_asset(MESH))
cdo.set_editor_property('InfimaTPMagazineRowOverride', unreal.Name('NoPart_MagIsInTheMesh'))
cdo.set_editor_property('InfimaTPMagazineDropClass', None)
bp = EAL.load_asset(BP)
print('COMPILED', unreal.BlueprintEditorLibrary.compile_blueprint(bp))
print('SAVED', EAL.save_loaded_asset(bp, False))
cdo = unreal.get_default_object(EAL.load_blueprint_class(BP))
tp = next(c for c in cdo.get_components_by_class(unreal.SkeletalMeshComponent) if c.get_name() == 'Third Person Mesh')
print('AFTER mesh', tp.get_editor_property('skeletal_mesh_asset').get_path_name(),
      'row', cdo.get_editor_property('InfimaTPMagazineRowOverride'), 'drop', cdo.get_editor_property('InfimaTPMagazineDropClass'))
