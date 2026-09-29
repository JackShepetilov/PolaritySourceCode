import json
import re
from pathlib import Path

import unreal

# Run inside the Unreal editor with Tools/mcp.sh py. The JSON input can contain several pairs.
# All assets are resolved before any Blueprint is changed. Set "apply": true to save the mapping.
CONFIG = Path(unreal.Paths.project_saved_dir()) / 'LPSP_TP/weapon_mapping.json'
REPORT = CONFIG.with_name('weapon_mapping_report.json')
DONOR_ROOT = '/Game/InfimaGames/LowPolyShooterPack/Usable/Weapons/'
TABLE_ROOT = '/Game/InfimaGames/LowPolyShooterPack/Data/Weapons/Animations/'
ART_ROOT = '/Game/InfimaGames/AnimatedLowPolyWeapons/Art/'
DROP_CLASS = '/Game/InfimaGames/AnimatedLowPolyWeapons/Core/BP_ALPW_Magazine'
SHARED_ANIM = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
BASE_WEAPON = '/Game/Variant_Shooter/Blueprints/Pickups/BP_ShooterWeaponBase'
GROUPS = {'AR': 'ARs', 'SMG': 'SMGs', 'Shotgun': 'Shotguns', 'Handgun': 'Handguns',
          'Sniper': 'Snipers', 'RL': 'Launchers', 'GL': 'Launchers'}
PER_ROUND_FAMILIES = {'Shotgun_01', 'Sniper_01'}
PER_ROUND_STAGES = {
    'WeaponMeshReloadAnimationTP': 'Reload_Open',
    'WeaponMeshSecondaryReloadAnimationTP': 'Reload_Insert',
    'WeaponMeshReloadEndAnimationTP': 'Reload_Close',
    'ReloadMontageTP': 'Reload_Open',
    'SecondaryReloadMontageTP': 'Reload_Insert',
    'ReloadEndMontageTP': 'Reload_Close',
}
ANIMATION_FIELDS = {
    'WeaponMeshFireAnimationTP': ('weapon', 'Fire'),
    'WeaponMeshLastShotAnimationTP': ('weapon', 'Fire_Last'),
    'WeaponMeshReloadAnimationTP': ('weapon', 'Reload_Empty', 'Reload'),
    'WeaponMeshSecondaryReloadAnimationTP': ('weapon', 'Reload', 'Reload_Empty'),
    'WeaponMeshReloadEndAnimationTP': ('weapon', 'Reload_End'),
    'FiringMontageTP': ('body', 'Fire'),
    'CycleActionMontageTP': ('body', 'Cycle_Action'),
    'ReloadMontageTP': ('body', 'Reload_Empty', 'Reload'),
    'SecondaryReloadMontageTP': ('body', 'Reload', 'Reload_Empty'),
    'ReloadEndMontageTP': ('body', 'Reload_End'),
    'DrawMontageTP': ('body', 'Unholster'),
    'HolsterMontageTP': ('body', 'Holster'),
}


def load(path, expected_type=None):
    asset = unreal.EditorAssetLibrary.load_asset(path) if path else None
    if not asset or (expected_type and not isinstance(asset, expected_type)):
        raise ValueError('Missing or wrong asset: %s' % path)
    return asset


def load_class(path):
    cls = unreal.EditorAssetLibrary.load_blueprint_class(path) if path else None
    if not cls:
        raise ValueError('Missing Blueprint class: %s' % path)
    return cls


def object_path(value):
    return value.get_path_name() if value else None


def report_value(value):
    if isinstance(value, unreal.Object):
        return object_path(value)
    if isinstance(value, (str, bool, int, float)) or value is None:
        return value
    return str(value)


def choose_animation(folders, prefixes, names):
    for folder in folders:
        for prefix in prefixes:
            for suffix in names:
                path = folder + prefix + suffix
                if unreal.EditorAssetLibrary.does_asset_exist(path):
                    return path
    return None


def plan_pair(pair, shared_anim, drop_class, base_class):
    target = pair['target'].split('.')[0]
    donor = pair['donor'].split('.')[0]
    match = re.fullmatch(r'BP_LPSP_WEP_(.+)', donor.rsplit('/', 1)[-1])
    if not match or not donor.startswith(DONOR_ROOT):
        raise ValueError('Expected a BP_LPSP_WEP_* donor: %s' % donor)
    family = match.group(1)
    group = pair.get('group') or GROUPS.get(family.split('_', 1)[0])
    if not group:
        raise ValueError('Unknown donor family %s; add its group to GROUPS' % family)
    body_family = pair.get('body_family', 'Handgun' if group == 'Handguns' else family)
    reload_mode = pair.get('reload_mode', 'per_round' if family in PER_ROUND_FAMILIES else 'magazine')
    if reload_mode not in ('per_round', 'magazine'):
        raise ValueError('reload_mode must be per_round or magazine: %s' % reload_mode)
    drop_magazine = pair.get('drop_magazine', reload_mode == 'magazine')
    if not isinstance(drop_magazine, bool):
        raise ValueError('drop_magazine must be a boolean: %s' % target)
    target_class = load_class(target)
    inherits_base = unreal.MathLibrary.class_is_child_of(target_class, base_class)
    if not unreal.MathLibrary.class_is_child_of(target_class, unreal.ShooterWeapon):
        raise ValueError('%s must derive from AShooterWeapon' % target)
    if drop_magazine and not inherits_base:
        raise ValueError('%s needs BP_ShooterWeaponBase for the falling magazine interface' % target)
    donor_class = load_class(donor)
    blueprint = load(target)
    cdo = unreal.get_default_object(target_class)
    third_person = next((c for c in cdo.get_components_by_class(unreal.SkeletalMeshComponent)
                         if c.get_name() == 'Third Person Mesh'), None)
    if not third_person:
        raise ValueError('%s has no Third Person Mesh component' % target)
    legacy_magazine = any(str(c.component_name) == 'InfimaTPMagazine'
                          for c in unreal.BlueprintService.list_components(target))

    weapon_dir = ART_ROOT + 'Weapons/' + group + '/'
    body_dir = ART_ROOT + 'Characters/Animations/' + group + '/'
    weapon_anim_dir = weapon_dir + 'Animations/'
    mesh_path = pair.get('mesh', weapon_dir + 'SK_' + family)
    mesh = load(mesh_path, unreal.SkeletalMesh)
    table_paths = pair.get('tables', {})
    if set(table_paths) - {'InfimaTPPosesTable', 'InfimaTPSequencesTable', 'InfimaTPBlendspacesTable'}:
        raise ValueError('Unknown table key in %s' % target)
    tables = {
        name: load(table_paths.get(name, TABLE_ROOT + 'DT_LPSP_CH_' + family + '_' + suffix),
                   unreal.DataTable)
        for name, suffix in (('InfimaTPPosesTable', 'Poses'),
                             ('InfimaTPSequencesTable', 'Sequences'),
                             ('InfimaTPBlendspacesTable', 'Blendspaces'))
    }
    fields = {'ThirdPersonAnimInstanceClass': shared_anim,
              'InfimaTPDonorClass': donor_class,
              'bThirdPersonWeaponPoseFromAnimation': True,
              'bUseTPAnimBPDefaultRecoil': family == 'AR_02'}
    fields.update(tables)
    resolved = {}
    overrides = pair.get('overrides', {})
    if not isinstance(overrides, dict):
        raise ValueError('overrides must be an object')
    unknown = set(overrides) - set(ANIMATION_FIELDS) - {
        'bUseTPAnimBPDefaultRecoil', 'bThirdPersonWeaponPoseFromAnimation',
        'ThirdPersonAimHoldTime', 'InfimaTPMagazineRowOverride',
        'LoadedRoundBoneName', 'LoadedRoundRevealTime', 'LoadedRoundRevealTimeTP'}
    if unknown:
        raise ValueError('Unsupported override fields: %s' % sorted(unknown))

    for field, spec in ANIMATION_FIELDS.items():
        source = spec[0]
        # Launcher weapon montages sit in the group folder itself, not in its Animations subfolder.
        folders = (weapon_anim_dir, weapon_dir) if source == 'weapon' else (body_dir,)
        prefixes = (('AM_TP_WEP_' + family + '_', 'AM_WEP_' + family + '_') if source == 'weapon'
                    else tuple(dict.fromkeys(('AM_TP_CH_' + body_family + '_',
                                              'AM_TP_CH_' + family.split('_', 1)[0] + '_'))))
        names = ((PER_ROUND_STAGES[field],) if reload_mode == 'per_round' and field in PER_ROUND_STAGES
                 else spec[1:])
        path = overrides.get(field, choose_animation(folders, prefixes, names))
        if reload_mode == 'per_round' and field in PER_ROUND_STAGES and not path:
            raise ValueError('%s needs a %s montage for per-round reload' % (target, field))
        resolved[field] = path
        if path:
            fields[field] = load(path, unreal.AnimMontage)
        else:
            # Clear inherited animations for a different weapon family.
            fields[field] = None

    donor_cdo = unreal.get_default_object(donor_class)
    try:
        magazine_handle = donor_cdo.get_editor_property('Row Handle Settings Magazine')
        has_magazine_row = bool(magazine_handle.data_table) and str(magazine_handle.row_name) != 'None'
    except Exception:
        has_magazine_row = False
    socket_probe = unreal.SkeletalMeshComponent()
    socket_probe.set_skeletal_mesh_asset(mesh)
    has_magazine_socket = bool(socket_probe.does_socket_exist('SOCKET_Magazine') or
                               socket_probe.does_socket_exist('SOCKET_Magazine_TP'))
    fields['InfimaTPMagazineDropClass'] = drop_class if has_magazine_row and drop_magazine else None
    if 'bUseTPAnimBPDefaultRecoil' in overrides:
        fields['bUseTPAnimBPDefaultRecoil'] = bool(overrides['bUseTPAnimBPDefaultRecoil'])
    if 'bThirdPersonWeaponPoseFromAnimation' in overrides:
        fields['bThirdPersonWeaponPoseFromAnimation'] = bool(overrides['bThirdPersonWeaponPoseFromAnimation'])
    if 'ThirdPersonAimHoldTime' in overrides:
        fields['ThirdPersonAimHoldTime'] = float(overrides['ThirdPersonAimHoldTime'])
    if 'InfimaTPMagazineRowOverride' in overrides:
        fields['InfimaTPMagazineRowOverride'] = unreal.Name(overrides['InfimaTPMagazineRowOverride'])
    if 'LoadedRoundBoneName' in overrides:
        fields['LoadedRoundBoneName'] = unreal.Name(overrides['LoadedRoundBoneName'])
    for name in ('LoadedRoundRevealTime', 'LoadedRoundRevealTimeTP'):
        if name in overrides:
            fields[name] = float(overrides[name])

    # Preflight every property before the first write.
    before = {}
    for name in fields:
        before[name] = report_value(cdo.get_editor_property(name))
    before['Third Person Mesh'] = object_path(third_person.get_editor_property('skeletal_mesh_asset'))
    return {'target': target, 'donor': donor, 'family': family, 'group': group,
            'reload_mode': reload_mode, 'drop_magazine': drop_magazine,
            'inherits_base': inherits_base,
            'mesh_path': mesh_path, 'mesh': mesh, 'blueprint': blueprint, 'fields': fields,
            'resolved_animations': resolved, 'has_magazine': has_magazine_row,
            'mesh_socket_seen': has_magazine_socket,
            'legacy_magazine_component': legacy_magazine,
            'assignments': {name: report_value(value) for name, value in fields.items()},
            'before': before, 'backup': pair.get('backup', True)}


def same_value(actual, expected):
    # Float fields come back as float32: 0.35 reads as 0.3499999940395355.
    if isinstance(expected, float) and isinstance(actual, (int, float)):
        return abs(actual - expected) < 1e-4
    return actual == expected


def apply_pair(plan):
    target = plan['target']
    if (plan['before']['Third Person Mesh'] == object_path(plan['mesh']) and
            all(same_value(plan['before'][name], value) for name, value in plan['assignments'].items())):
        print('VERIFIED', target, 'already matches donor; no asset write')
        return
    bp_name = target.rsplit('/', 1)[-1]
    if plan['backup']:
        backup_dir = '/Game/Variant_Shooter/Tests/LPSP_TP_Backups'
        backup_path = backup_dir + '/' + bp_name + '_BeforeTPKit'
        if not unreal.EditorAssetLibrary.does_asset_exist(backup_path):
            unreal.EditorAssetLibrary.make_directory(backup_dir)
            assert unreal.EditorAssetLibrary.duplicate_asset(target, backup_path), backup_path
            print('CREATED backup', backup_path)
    cdo = unreal.get_default_object(load_class(target))
    mesh_component = next(c for c in cdo.get_components_by_class(unreal.SkeletalMeshComponent)
                          if c.get_name() == 'Third Person Mesh')
    mesh_component.set_editor_property('skeletal_mesh_asset', plan['mesh'])
    print('MODIFIED', target, 'Third Person Mesh', plan['mesh_path'])
    for name, value in plan['fields'].items():
        cdo.set_editor_property(name, value)
        print('MODIFIED', target, name, object_path(value) if isinstance(value, unreal.Object) else value)
    assert unreal.BlueprintEditorLibrary.compile_blueprint(plan['blueprint']), 'Compile failed: ' + target
    assert unreal.EditorAssetLibrary.save_loaded_asset(plan['blueprint'], False), 'Save failed: ' + target
    saved = unreal.get_default_object(load_class(target))
    for name, expected in plan['fields'].items():
        actual = saved.get_editor_property(name)
        assert same_value(actual, expected), (target, name, actual, expected)
    saved_mesh = next(c for c in saved.get_components_by_class(unreal.SkeletalMeshComponent)
                      if c.get_name() == 'Third Person Mesh')
    assert saved_mesh.get_editor_property('skeletal_mesh_asset') == plan['mesh']
    print('VERIFIED', target, 'saved fields and third person mesh')


def main():
    assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world(), 'Stop PIE first'
    config = json.loads(CONFIG.read_text(encoding='utf-8'))
    pairs = config.get('mappings')
    assert isinstance(pairs, list) and pairs, 'mappings must be a non-empty list'
    shared_anim = load_class(config.get('shared_anim', SHARED_ANIM))
    drop_class = load_class(config.get('drop_class', DROP_CLASS))
    base_class = load_class(BASE_WEAPON)
    plans = [plan_pair(pair, shared_anim, drop_class, base_class) for pair in pairs]
    targets = [p['target'] for p in plans]
    if len(set(targets)) != len(targets):
        raise ValueError('Target Blueprint appears twice')
    if config.get('apply', False):
        legacy = [p['target'] for p in plans if p['legacy_magazine_component']]
        if legacy:
            raise ValueError('Old InfimaTPMagazine component would duplicate the C++ magazine: %s' % legacy)
    report = {'apply': bool(config.get('apply', False)), 'mappings': []}
    for plan in plans:
        print('PLAN', plan['target'], '->', plan['donor'], 'mesh', plan['mesh_path'],
              'reload_mode', plan['reload_mode'], 'drop_magazine', plan['drop_magazine'],
              'inherits_base', plan['inherits_base'],
              'magazine_row', plan['has_magazine'], 'mesh_socket_seen', plan['mesh_socket_seen'],
              'legacy_magazine_component', plan['legacy_magazine_component'])
        report['mappings'].append({key: plan[key] for key in
                                   ('target', 'donor', 'family', 'group', 'reload_mode', 'inherits_base',
                                    'drop_magazine', 'mesh_path',
                                    'resolved_animations', 'has_magazine', 'mesh_socket_seen',
                                    'legacy_magazine_component',
                                    'assignments', 'before')})
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    if report['apply']:
        for plan in plans:
            apply_pair(plan)
        report['verified'] = True
        REPORT.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print('REPORT', REPORT)


main()
