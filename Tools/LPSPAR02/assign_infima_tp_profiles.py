import unreal
import json
from pathlib import Path

assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
TABLE_ROOT = '/Game/InfimaGames/LowPolyShooterPack/Data/Weapons/Animations/'
DONOR_ROOT = '/Game/InfimaGames/LowPolyShooterPack/Usable/Weapons/'
SHARED_ANIM = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
PAIRS = (
    ('/Game/Variant_Shooter/Tests/LPSP_AR02/BP_AR02_Integration_Test', 'AR_02'),
    ('/Game/Variant_Shooter/Blueprints/Pickups/Weapons/Kinemation/Pistols/BP_M1911', 'Handgun_03'),
)
shared_anim = unreal.EditorAssetLibrary.load_blueprint_class(SHARED_ANIM)
assert shared_anim, SHARED_ANIM

for target, family in PAIRS:
    bp = unreal.EditorAssetLibrary.load_asset(target)
    cdo = unreal.get_default_object(unreal.EditorAssetLibrary.load_blueprint_class(target))
    donor = unreal.EditorAssetLibrary.load_blueprint_class(DONOR_ROOT + 'BP_LPSP_WEP_' + family)
    poses = unreal.EditorAssetLibrary.load_asset(TABLE_ROOT + 'DT_LPSP_CH_' + family + '_Poses')
    sequences = unreal.EditorAssetLibrary.load_asset(TABLE_ROOT + 'DT_LPSP_CH_' + family + '_Sequences')
    blendspaces = unreal.EditorAssetLibrary.load_asset(TABLE_ROOT + 'DT_LPSP_CH_' + family + '_Blendspaces')
    drop_class = unreal.EditorAssetLibrary.load_blueprint_class(
        '/Game/InfimaGames/AnimatedLowPolyWeapons/Core/BP_ALPW_Magazine')
    assert bp and donor and poses and sequences and blendspaces and drop_class, (target, family)
    cdo.set_editor_property('ThirdPersonAnimInstanceClass', shared_anim)
    cdo.set_editor_property('InfimaTPDonorClass', donor)
    cdo.set_editor_property('InfimaTPPosesTable', poses)
    cdo.set_editor_property('InfimaTPSequencesTable', sequences)
    cdo.set_editor_property('InfimaTPBlendspacesTable', blendspaces)
    cdo.set_editor_property('InfimaTPMagazineDropClass', drop_class)
    cdo.set_editor_property('bUseTPAnimBPDefaultRecoil', family == 'AR_02')
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    assert unreal.EditorAssetLibrary.save_loaded_asset(bp, False)
    saved = unreal.get_default_object(unreal.EditorAssetLibrary.load_blueprint_class(target))
    assert saved.get_editor_property('InfimaTPDonorClass') == donor
    assert saved.get_editor_property('InfimaTPPosesTable') == poses
    assert saved.get_editor_property('InfimaTPSequencesTable') == sequences
    assert saved.get_editor_property('InfimaTPBlendspacesTable') == blendspaces
    assert saved.get_editor_property('InfimaTPMagazineDropClass') == drop_class
    assert saved.get_editor_property('ThirdPersonAnimInstanceClass') == shared_anim
    print('MODIFIED', target, 'donor', donor.get_name(), 'poses', poses.get_name(),
          'sequences', sequences.get_name(), 'blendspaces', blendspaces.get_name(),
          'drop', drop_class.get_name())

# The old AR_02 test Blueprint used to register itself from Tick. Once all equipped
# weapons are registered by C++, a holstered AR_02 must not overwrite M1911's profile.
bridge = Path(unreal.Paths.project_saved_dir()) / 'LPSP_AR02/weapon_event_nodes.json'
ids = json.loads(bridge.read_text(encoding='utf-8'))
ar_path = PAIRS[0][0]
connections = unreal.BlueprintService.get_connections(ar_path, 'EventGraph')
register_edges = [edge for edge in connections
                  if edge.source_node_id == ids['Adapter']
                  and edge.source_pin_name == 'then'
                  and edge.target_node_id == ids['Register']]
if register_edges:
    assert len(register_edges) == 1, register_edges
    assert unreal.BlueprintService.disconnect_pin(ar_path, 'EventGraph', ids['Adapter'], 'then')
    ar_bp = unreal.EditorAssetLibrary.load_asset(ar_path)
    assert unreal.BlueprintEditorLibrary.compile_blueprint(ar_bp)
    assert unreal.EditorAssetLibrary.save_loaded_asset(ar_bp, False)
    print('MODIFIED', ar_path, 'removed old per-weapon Tick registration')
else:
    print('VERIFIED', ar_path, 'old Tick registration already absent')
