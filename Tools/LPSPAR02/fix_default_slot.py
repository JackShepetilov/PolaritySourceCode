import unreal

# One DefaultSlot node at the point where the standing and aiming state slots merge.
# The engine rejects two slot nodes with the same name ("SLOTNODE ... already exists"), so the earlier
# pair is reduced to one: the second is removed and the standing/aiming pair is restored.

ABP = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
GRAPH = 'AnimGraph'
ACTION_STANDING = '904821CA475BE88FC7B07FB8FC6006E7'
ACTION_AIMING = '023A2FF2479FE2049B7147B0F91964B5'
OVERLAY_STANDING = '68B01A5442534F231AEBC8806A6B0CEB'
OVERLAY_AIMING = '6E304C1D45234617D4D285B71EF6CD48'
MERGE = 'FC6B120B443FB9153DDB5C9DB7888F6A'          # Blend Poses by bool, standing vs aiming
AFTER_MERGE = '8D2846E74548C325018ED1854CDCB8FB'   # Inertialization.Source
KEEP_SLOT = '949F3CE8427D0F07BC4176BC0913D066'
DROP_SLOT = '1D4158784820484E75E368992C548180'

assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world(), 'PIE is running'
BS = unreal.BlueprintService


def step(label, result):
    print(label, result)
    assert result, label


step('drop_duplicate_source', BS.disconnect_pin(ABP, GRAPH, DROP_SLOT, 'Source'))
step('drop_duplicate_output', BS.disconnect_pin(ABP, GRAPH, DROP_SLOT, 'Pose'))
step('aiming_to_overlay', BS.connect_nodes(ABP, GRAPH, ACTION_AIMING, 'Pose', OVERLAY_AIMING, 'Source'))
step('delete_duplicate', BS.delete_node(ABP, GRAPH, DROP_SLOT))

step('standing_to_overlay', BS.connect_nodes(ABP, GRAPH, ACTION_STANDING, 'Pose', OVERLAY_STANDING, 'Source'))
step('break_merge_out', BS.disconnect_pin(ABP, GRAPH, MERGE, 'Pose'))
step('break_keep_source', BS.disconnect_pin(ABP, GRAPH, KEEP_SLOT, 'Source'))
step('merge_to_slot', BS.connect_nodes(ABP, GRAPH, MERGE, 'Pose', KEEP_SLOT, 'Source'))
step('slot_to_after_merge', BS.connect_nodes(ABP, GRAPH, KEEP_SLOT, 'Pose', AFTER_MERGE, 'Source'))

blueprint = unreal.EditorAssetLibrary.load_asset(ABP)
print('COMPILE', unreal.BlueprintEditorLibrary.compile_blueprint(blueprint))
print('SAVE', unreal.EditorAssetLibrary.save_loaded_asset(blueprint, False))
print('STATUS', blueprint.get_editor_property('status'))
print('DONE')
