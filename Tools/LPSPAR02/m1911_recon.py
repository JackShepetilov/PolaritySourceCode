import json

import unreal

# Pistol (M1911 / Handgun_03) reconnaissance before any write:
#   1. stop PIE if it is running (blueprints are not saved during PIE),
#   2. what the weapon's Third Person Mesh component actually carries now,
#   3. which slots the pack's handgun montages have (the AR02 empty reload needed a project copy),
#   4. what the magazine-drop notify expects from the weapon.
# Read-only apart from stopping PIE.

BP = '/Game/Variant_Shooter/Blueprints/Pickups/Weapons/Kinemation/Pistols/BP_M1911'
MONTAGES = (
    '/Game/InfimaGames/AnimatedLowPolyWeapons/Art/Weapons/Handguns/Animations/AM_TP_WEP_Handgun_03_Reload',
    '/Game/InfimaGames/AnimatedLowPolyWeapons/Art/Weapons/Handguns/Animations/AM_TP_WEP_Handgun_03_Reload_Empty',
    '/Game/InfimaGames/AnimatedLowPolyWeapons/Art/Weapons/Handguns/Animations/AM_TP_WEP_Handgun_03_Fire',
    '/Game/InfimaGames/AnimatedLowPolyWeapons/Art/Characters/Animations/Handguns/AM_TP_CH_Handgun_Reload',
    '/Game/InfimaGames/AnimatedLowPolyWeapons/Art/Characters/Animations/Handguns/AM_TP_CH_Handgun_Reload_Empty',
    '/Game/InfimaGames/AnimatedLowPolyWeapons/Art/Characters/Animations/Handguns/AM_TP_CH_Handgun_Holster',
    '/Game/InfimaGames/AnimatedLowPolyWeapons/Art/Characters/Animations/Handguns/AM_TP_CH_Handgun_Unholster',
)

editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
world = editor.get_game_world()
print('PIE_BEFORE', bool(world))
if world:
    result = unreal.ToolsetRegistry.execute_tool('EditorToolset.EditorAppToolset', 'StopPIE', '{}')
    print('StopPIE', result.is_complete, result.error)
    print('PIE_AFTER', bool(editor.get_game_world()))

blueprint = unreal.EditorAssetLibrary.load_asset(BP)
subsystem = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
for handle in subsystem.k2_gather_subobject_data_for_blueprint(blueprint):
    data = subsystem.k2_find_subobject_data_from_handle(handle)
    obj = unreal.SubobjectDataBlueprintFunctionLibrary.get_object(data)
    if not obj or not isinstance(obj, unreal.SkeletalMeshComponent):
        continue
    print('SKEL_COMPONENT', obj.get_name(),
          'mesh=', obj.get_editor_property('skeletal_mesh_asset'),
          'anim=', obj.get_editor_property('anim_class'))

for path in MONTAGES:
    montage = unreal.EditorAssetLibrary.load_asset(path)
    if not montage:
        print('MONTAGE', path, 'MISSING')
        continue
    slots, notifies = [], []
    try:
        for track in montage.get_editor_property('slot_anim_tracks'):
            slots.append(str(track.get_editor_property('slot_name')))
    except Exception as exc:
        slots = ['ERR %s' % exc]
    try:
        for notify in montage.get_editor_property('notifies'):
            name = str(notify.get_editor_property('notify_name'))
            cls = str(notify.get_editor_property('notify_class'))
            notifies.append('%s | %s' % (name, cls))
    except Exception as exc:
        notifies = ['ERR %s' % exc]
    print('MONTAGE', path.split('/')[-1])
    print('   slots', slots)
    print('   length', montage.get_editor_property('sequence_length'))
    print('   notifies', notifies)
print('DONE')
