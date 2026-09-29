import unreal
bp = unreal.load_asset('/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test')
for n in unreal.ObjectIterator(unreal.AnimGraphNode_TwoWayBlend):
    if n.get_outermost() == bp.get_outermost() and n.get_name() == 'AnimGraphNode_TwoWayBlend_7':
        print(n.get_editor_property('blend_node').export_text())
        binding=n.get_editor_property('binding')
        print('BINDING',binding)
        for p in n.list_all_pins(): print('PIN',p.get_pin_name(),p.get_pin_value())
print('DIRTY MAPS',unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages())
print('WORLD',unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world())
unreal.SystemLibrary.execute_console_command(unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world(),'LiveCoding.Compile')
