import json

import unreal

# Manifest for the LPSP TP kit of one target weapon, derived from the pack's naming rules.
# Read-only. See Docs/Pipeline_LPSP_TP_Kit_2026-09-28.md.
#
# Input:  Saved/LPSP_AR02/kit_target.json  {"target": "<weapon BP path>", "donor": "<BP_LPSP_WEP_* path>"}
# Output: Saved/LPSP_AR02/kit_<target name>.json and a short printout.
#
# NOTE: the first statement must be an import (ExecuteFile mode treats a leading docstring as a path).

SAVED = unreal.Paths.project_saved_dir() + 'LPSP_AR02/'
CONFIG = SAVED + 'kit_target.json'
DT_MAG = '/Game/InfimaGames/LowPolyShooterPack/Data/Weapons/Settings/DT_LPSP_WEP_Settings_Magazines'
WEP_ROOT = '/Game/InfimaGames/AnimatedLowPolyWeapons/Art/Weapons/'
CH_ROOT = '/Game/InfimaGames/AnimatedLowPolyWeapons/Art/Characters/Animations/'
POSE_ROOT = '/Game/InfimaGames/LowPolyShooterPack/Data/Weapons/Animations/'
GROUPS = {'AR': 'ARs', 'SMG': 'SMGs', 'Shotgun': 'Shotguns', 'Handgun': 'Handguns',
          'Sniper': 'Snipers', 'RL': 'Launchers', 'GL': 'Launchers'}
ROW_SPECIAL = {'AR_02': 'Assault-Rifle-02', 'RL_01': 'Launcher-Rocket-01', 'GL_01': 'Launcher-Grenade-01'}
FIELDS = ('ThirdPersonMesh', 'bThirdPersonWeaponPoseFromAnimation',
          'ThirdPersonAnimInstanceClass', 'InfimaTPDonorClass', 'InfimaTPPosesTable',
          'bUseTPAnimBPDefaultRecoil',
          'WeaponMeshFireAnimationTP', 'WeaponMeshLastShotAnimationTP', 'WeaponMeshReloadAnimationTP',
          'WeaponMeshSecondaryReloadAnimationTP', 'WeaponMeshReloadEndAnimationTP',
          'FiringMontageTP', 'CycleActionMontageTP', 'ReloadMontageTP', 'SecondaryReloadMontageTP',
          'ReloadEndMontageTP', 'ThirdPersonAimHoldTime', 'DrawMontageTP', 'HolsterMontageTP',
          'PackAimSocketName', 'SightAimSocketName')
FUNCTIONS = ('get_third_person_shot_count', 'is_third_person_aiming_after_shot', 'is_reloading')
EMPTY = ('None', 'False', '0', '0.0', '', '()', '[]')


def family_of(donor):
    parts = donor.split('/')[-1].split('.')[0].split('_')
    if len(parts) < 2:
        return None, None
    token, index = parts[-2], parts[-1]
    family = '%s_%s' % (token, index)
    return family, GROUPS.get(token)


def row_name(family):
    return ROW_SPECIAL.get(family, family.replace('_', '-'))


try:
    config = json.loads(open(CONFIG, encoding='utf-8').read())
except Exception:
    config = {}
target = config.get('target')
donor = config.get('donor')
if not target:
    print('NO_TARGET: put {"target": ..., "donor": ...} into', CONFIG)
    raise SystemExit

family, group = family_of(donor or '')
manifest = {'target': target, 'donor': donor, 'family': family, 'group': group, 'assets': {}, 'target_state': {}}

if family and group:
    base = WEP_ROOT + group + '/'
    body_family = 'Handgun' if group == 'Handguns' else family
    candidates = {
        'tp_mesh': base + 'SK_' + family,
        'poses_table': POSE_ROOT + 'DT_LPSP_CH_' + family + '_Poses',
        'magazine_mesh': base + 'SM_' + family + '_Magazine_Default',
        'scope_mesh': base + 'SM_' + family + '_Scope_Default',
        'weapon_reload': base + 'Animations/AM_TP_WEP_' + family + '_Reload',
        'weapon_reload_empty': base + 'Animations/AM_TP_WEP_' + family + '_Reload_Empty',
        'weapon_fire': base + 'Animations/AM_TP_WEP_' + family + '_Fire',
        'weapon_fire_alt': base + 'Animations/AM_WEP_' + family + '_Fire',
        'body_reload': CH_ROOT + group + '/AM_TP_CH_' + body_family + '_Reload',
        'body_reload_empty': CH_ROOT + group + '/AM_TP_CH_' + body_family + '_Reload_Empty',
        'body_fire': CH_ROOT + group + '/AM_TP_CH_' + body_family + '_Fire',
        'body_holster': CH_ROOT + group + '/AM_TP_CH_' + body_family + '_Holster',
        'body_unholster': CH_ROOT + group + '/AM_TP_CH_' + body_family + '_Unholster',
        'donor_bp': donor,
    }
    for key, path in candidates.items():
        manifest['assets'][key] = {'path': path, 'exists': unreal.EditorAssetLibrary.does_asset_exist(path)}
    mesh_path = candidates['tp_mesh']
    if unreal.EditorAssetLibrary.does_asset_exist(mesh_path):
        mesh = unreal.EditorAssetLibrary.load_asset(mesh_path)
        manifest['assets']['tp_mesh_has_magazine_socket'] = bool(mesh.find_socket('SOCKET_Magazine'))

    table = unreal.EditorAssetLibrary.load_asset(DT_MAG)
    rows = [str(r) for r in unreal.DataTableFunctionLibrary.get_data_table_row_names(table)] if table else []
    wanted = row_name(family)
    manifest['magazine_row'] = {'name': wanted, 'exists': wanted in rows, 'all_rows': rows}

cls = unreal.EditorAssetLibrary.load_blueprint_class(target)
cdo = unreal.get_default_object(cls)
filled, empty, missing = {}, [], []
for name in FIELDS:
    try:
        value = cdo.get_editor_property(name)
    except Exception as exc:
        missing.append('%s (%s)' % (name, exc))
        continue
    text = str(value)
    if text in EMPTY:
        empty.append(name)
    else:
        filled[name] = text
manifest['target_state'] = {'filled': filled, 'empty': empty, 'absent': missing,
                            'functions': {fn: hasattr(cdo, fn) for fn in FUNCTIONS},
                            'variables_has_row': unreal.BlueprintService.variable_exists(target, 'InfimaMagazineRow'),
                            'components': [str(c) for c in unreal.BlueprintService.list_components(target)],
                            'graphs': [str(g) for g in unreal.BlueprintService.list_graphs(target)]}

out = SAVED + 'kit_' + target.split('/')[-1].split('.')[0] + '.json'
with open(out, 'w', encoding='utf-8') as handle:
    json.dump(manifest, handle, indent=2, default=str)
print('KIT_FAMILY', family, group, 'ROW', manifest.get('magazine_row', {}).get('name'))
print('KIT_ASSETS', json.dumps({k: v.get('exists') for k, v in manifest['assets'].items()
                                if isinstance(v, dict)}, default=str))
print('KIT_FILLED', sorted(manifest['target_state']['filled']))
print('KIT_EMPTY', sorted(manifest['target_state']['empty']))
print('KIT_ABSENT', manifest['target_state']['absent'])
print('KIT_OUT', out)
