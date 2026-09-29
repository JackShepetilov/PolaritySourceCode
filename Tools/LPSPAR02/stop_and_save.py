import unreal, json

# Stop PIE (blueprints cannot be saved while it runs) and save the AnimBP with the jump-gate fix.
AB = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
L = []


def log(m):
    L.append(str(m))
    print(m)


r = unreal.ToolsetRegistry.execute_tool('EditorToolset.EditorAppToolset', 'StopPIE', '{"options": {}}')
log('StopPIE: %s %s' % (r.is_complete, r.error))
log('game world still: %s' % (unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is not None))
log('save: %s' % unreal.EditorAssetLibrary.save_asset(AB, only_if_is_dirty=False))
log('status: %s' % unreal.load_asset(AB).get_editor_property('status'))
log('dirty content: %s' % [p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()])
open(unreal.Paths.project_saved_dir() + 'LPSP_AR02/stop_and_save.log', 'w', encoding='utf-8').write('\n'.join(L))
