import unreal
assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
path='/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
b=unreal.load_asset(path)
for n in unreal.ObjectIterator(unreal.EdGraphNode):
    if n.get_outermost()!=b.get_outermost() or n.get_outer().get_name()!='AnimGraph':continue
    if n.get_name() in ['AnimGraphNode_SequencePlayer_5','AnimGraphNode_BlendListByBool_3','AnimGraphNode_BlendListByBool_4','AnimGraphNode_TwoWayBlend_7']:
        print('NODE',n.get_name())
        for p in n.list_all_pins():print(p.get_pin_name(),p.get_pin_value())
print('STATUS',b.get_editor_property('status'))
print('DIRTY MAPS',unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages())
print('DIRTY ASSETS',unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages())
print('WEAPON REF',unreal.BlueprintService.get_blueprint_info('/Game/InfimaGames/LowPolyShooterPack/Usable/Weapons/BP_LPSP_WEP_AR_02'))
