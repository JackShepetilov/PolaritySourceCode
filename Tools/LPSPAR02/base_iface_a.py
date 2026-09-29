import unreal

# Step A: put the pack's interface on the base weapon Blueprint, compile and save, then report what the
# event discovery sees. Step B, a separate script, places the event nodes.

BP = '/Game/Variant_Shooter/Blueprints/Pickups/BP_ShooterWeaponBase'
IFACE = '/Game/InfimaGames/AnimatedLowPolyWeapons/Core/BPI_ALPW_Droppable_Magazine_Target'
BS = unreal.BlueprintService

assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world(), 'PIE is running'

print('ADD_IFACE', BS.add_interface(BP, IFACE))
blueprint = unreal.EditorAssetLibrary.load_asset(BP)
print('COMPILE', unreal.BlueprintEditorLibrary.compile_blueprint(blueprint))
print('SAVE', unreal.EditorAssetLibrary.save_loaded_asset(blueprint, False))
print('STATUS', blueprint.get_editor_property('status'))

for message in ('ALPW: Set Magazine Visibility', 'ALPW: Drop Magazine'):
    found = BS.discover_nodes(BP, message, '', 20)
    print('DISCOVER', message, 'count', len(found))
    for item in found[:6]:
        print('   ', str(getattr(item, 'spawner_key', ''))[:110], '|',
              str(getattr(item, 'display_name', ''))[:40])
print('STEP_A_DONE')
