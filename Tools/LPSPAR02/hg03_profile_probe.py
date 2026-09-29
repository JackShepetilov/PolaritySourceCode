import json
import unreal

# Read-only comparison of the live TP adapter and the two Infima donors.
OUT = unreal.Paths.project_saved_dir() + 'LPSP_AR02/hg03_profile_probe.json'
PATHS = {
    'adapter': '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test',
    'donor_ar': '/Game/InfimaGames/LowPolyShooterPack/Usable/Weapons/BP_LPSP_WEP_AR_02',
    'donor_hg': '/Game/InfimaGames/LowPolyShooterPack/Usable/Weapons/BP_LPSP_WEP_Handgun_03',
}
FIELDS = (
    'Data Table Animation Poses', 'Data Table Sequences', 'Data Table Blendspaces',
    'Settings Animation', 'Settings Grip', 'Settings Scope', 'Weapon Settings',
    'Look Offset', 'Recoil State Weapon', 'Actor Weapon', 'Aiming', 'Third Person',
)


def brief(value):
    if value is None:
        return None
    if hasattr(value, 'get_path_name'):
        return value.get_path_name()
    if hasattr(value, 'export_text'):
        raw = value.export_text()
        return raw[:1800] + ('...' if len(raw) > 1800 else '')
    return str(value)


result = {'pie': bool(unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world())}
assert not result['pie'], 'Stop PIE before asset reads'
for label, path in PATHS.items():
    row = {'path': path, 'fields': {}}
    cls = unreal.EditorAssetLibrary.load_blueprint_class(path)
    if not cls:
        row['missing'] = True
        result[label] = row
        continue
    cdo = unreal.get_default_object(cls)
    for field in FIELDS:
        try:
            row['fields'][field] = brief(cdo.get_editor_property(field))
        except Exception:
            pass
    try:
        info = unreal.BlueprintService.get_blueprint_info(path)
        row['variables'] = [str(v)[:300] for v in info.variables]
    except Exception as exc:
        row['variables_error'] = str(exc)
    result[label] = row

with open(OUT, 'w', encoding='utf-8') as handle:
    json.dump(result, handle, ensure_ascii=False, indent=2)
print('PROFILE PROBE', OUT, {k: list(v.get('fields', {})) for k, v in result.items() if isinstance(v, dict)})
