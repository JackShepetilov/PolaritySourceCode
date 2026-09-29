"""Restore only the third-person mesh changed by this test; retain other edits."""
import unreal
import json
from pathlib import Path

BP = '/Game/Variant_Shooter/Blueprints/Pickups/Weapons/EnemyWeapons/BP_AR'
manifest = Path(unreal.Paths.project_saved_dir()) / 'LPSP_AR02' / 'weapon_mesh_rollback.json'
data = json.loads(manifest.read_text(encoding='utf-8'))
assert data['blueprint'] == BP
mesh = unreal.load_asset(data['skeletal_mesh_asset'])
assert mesh
cdo = unreal.get_default_object(unreal.EditorAssetLibrary.load_blueprint_class(BP))
cdo.get_editor_property('third_person_mesh').set_skeletal_mesh_asset(mesh)
print('RESTORED:', BP, 'ThirdPersonMesh', mesh.get_path_name())
assert unreal.BlueprintEditorLibrary.compile_blueprint(unreal.load_asset(BP))
assert unreal.EditorAssetLibrary.save_asset(BP, False)
check = unreal.get_default_object(unreal.EditorAssetLibrary.load_blueprint_class(BP))
assert check.get_editor_property('third_person_mesh').get_editor_property('skeletal_mesh_asset') == mesh
print('VERIFIED: previous third-person mesh restored')
