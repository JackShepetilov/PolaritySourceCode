"""Configure the AR_02 integration test after the native FP/TP split is compiled."""
import unreal
import json
from pathlib import Path

BASE = '/Game/Variant_Shooter/Tests/LPSP_AR02'
WEAPON = BASE + '/BP_AR02_Integration_Test'
ANIM = BASE + '/ABP_AR02_LPSP_Test'
OUT = Path(unreal.Paths.project_saved_dir()) / 'LPSP_AR02'
BS = unreal.BlueprintService


def main():
    assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world(), 'Stop PIE first'
    for path in (WEAPON, ANIM):
        backup = BASE + '/Backup/' + path.rsplit('/', 1)[-1] + '_BeforePhase1Split'
        if not unreal.EditorAssetLibrary.does_asset_exist(backup):
            assert unreal.EditorAssetLibrary.duplicate_asset(path, backup)
            assert unreal.EditorAssetLibrary.save_asset(backup, False)
            print('CREATED backup', backup)

    wbp = unreal.load_asset(WEAPON)
    wc = unreal.get_default_object(wbp.generated_class())
    fp_fields = ['pack_weapon_settings', 'weapon_pose_from_animation', 'draw_montage',
                 'holster_montage', 'reload_montage', 'secondary_reload_montage',
                 'weapon_mesh_fire_animation', 'weapon_mesh_reload_animation',
                 'weapon_mesh_secondary_reload_animation', 'weapon_mesh_last_shot_animation']
    fp_before = {k: str(wc.get_editor_property(k)) for k in fp_fields}
    chars = '/Game/InfimaGames/AnimatedLowPolyWeapons/Art/Characters/Animations/ARs/'
    guns = '/Game/InfimaGames/AnimatedLowPolyWeapons/Art/Weapons/ARs/Animations/'
    settings = {
        'third_person_weapon_pose_from_animation': True,
        'draw_montage_tp': chars + 'AM_TP_CH_AR_02_Unholster',
        'holster_montage_tp': chars + 'AM_TP_CH_AR_02_Holster',
        'reload_montage_tp': chars + 'AM_TP_CH_AR_02_Reload_Empty',
        'secondary_reload_montage_tp': chars + 'AM_TP_CH_AR_02_Reload',
        'weapon_mesh_fire_animation_tp': guns + 'AM_WEP_AR_02_Fire',
        'weapon_mesh_reload_animation_tp': guns + 'AM_TP_WEP_AR_02_Reload_Empty',
        'weapon_mesh_secondary_reload_animation_tp': guns + 'AM_TP_WEP_AR_02_Reload',
    }
    for key, val in settings.items():
        asset = unreal.load_asset(val) if isinstance(val, str) else val
        assert asset is not None, val
        wc.set_editor_property(key, asset)
        print('MODIFIED', WEAPON, key, val)
    # AR_02 is automatic, magazine-fed, and has no separate last-shot/bolt/closing montage.
    for key in ('firing_montage_tp', 'cycle_action_montage_tp', 'reload_end_montage_tp',
                'weapon_mesh_last_shot_animation_tp', 'weapon_mesh_reload_end_animation_tp'):
        wc.set_editor_property(key, None)
    assert fp_before == {k: str(wc.get_editor_property(k)) for k in fp_fields}
    unreal.BlueprintEditorLibrary.compile_blueprint(wbp)
    assert unreal.EditorAssetLibrary.save_asset(WEAPON, False)

    # The existing per-weapon Tick bridge supplies Actor Weapon on each machine. Read cosmetic
    # shot state there; the old OnShotFired bridge was authority-only and reset clients every Tick.
    marker = OUT / 'phase1_bridge_nodes.json'
    if not marker.exists():
        def node(ref, kind, **params):
            return dict(ref=ref, type=kind, params=params)
        r = BS.build_graph(ANIM, 'EventGraph', [
            node('WeaponCast', 'cast', target_class='/Script/Polarity.ShooterWeapon'),
            node('Aiming', 'function_call', **{'class': 'ShooterWeapon', 'function': 'IsThirdPersonAimingAfterShot'}),
            node('Shots', 'function_call', **{'class': 'ShooterWeapon', 'function': 'GetThirdPersonShotCount'}),
            node('SetAim', 'variable_set', variable='Aiming'),
            node('SetShots', 'variable_set', variable='Shot Count'),
        ], [], [], False, False)
        assert r and not r.errors and not r.nodes_failed, str(r)
        ids = dict(r.ref_to_node_id)
        marker.write_text(json.dumps(ids, indent=2), encoding='utf-8')
        print('CREATED', ANIM, ids)
    else:
        ids = json.loads(marker.read_text(encoding='utf-8'))
    old = json.loads((OUT / 'PolarityRegisterWeapon_nodes.json').read_text(encoding='utf-8'))
    def con(a, ap, b, bp):
        assert BS.connect_nodes(ANIM, 'EventGraph', a, ap, b, bp), (a, ap, b, bp)
    cast_pin = next(p.pin_name for p in BS.get_node_pins(ANIM, 'EventGraph', ids['WeaponCast'])
                    if not p.is_input and p.pin_type == 'object')
    BS.disconnect_pin(ANIM, 'EventGraph', old['SetWeapon'], 'then')
    con(old['SetWeapon'], 'then', ids['WeaponCast'], 'execute')
    con(old['Event'], 'Weapon', ids['WeaponCast'], 'Object')
    for ref in ('Aiming', 'Shots'):
        con(ids['WeaponCast'], cast_pin, ids[ref], 'self')
    con(ids['WeaponCast'], 'then', ids['SetAim'], 'execute')
    con(ids['Aiming'], 'ReturnValue', ids['SetAim'], 'Aiming')
    con(ids['SetAim'], 'then', ids['SetShots'], 'execute')
    con(ids['Shots'], 'ReturnValue', ids['SetShots'], 'Shot Count')
    # Retain the public event for compatibility, but it no longer increments a second counter.
    shot = json.loads((OUT / 'PolarityWeaponShot_nodes.json').read_text(encoding='utf-8'))
    BS.disconnect_pin(ANIM, 'EventGraph', shot['Event'], 'then')

    abp = unreal.load_asset(ANIM)
    ac = unreal.get_default_object(abp.generated_class())
    ac.set_editor_property('Aiming', False)
    # These three Blend Poses nodes already receive Aiming by wire. Their stale bindings refer
    # to deleted pack fields and are only a second, broken input route.
    for n in unreal.ObjectIterator(unreal.AnimGraphNode_Base):
        if n.get_outermost() != abp.get_outermost():
            continue
        if 'Invalid field' in n.get_editor_property('error_msg'):
            assert 'BlendListByBool' in n.get_class().get_name(), n.get_path_name()
            active = next(p for p in n.list_all_pins() if str(p.get_pin_name()) == 'bActiveValue')
            assert active.list_connected_pins(), n.get_path_name()
            n.set_editor_property('binding', None)
            print('MODIFIED stale binding', n.get_path_name())
    unreal.BlueprintEditorLibrary.compile_blueprint(abp)
    assert unreal.EditorAssetLibrary.save_asset(ANIM, False)
    print('SAVED phase 1 assets; FP settings preserved; verify separately in PIE')


if __name__ == '__main__':
    main()
