import unreal

# Compile + save ABP_AR02_LPSP_Test, but only when PIE is off: blueprints silently refuse to save while
# a PIE session is running, and the edit would be lost on the next editor restart.
AB = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
TOOLS = 'editor_toolset.toolsets.blueprint.BlueprintTools'
L = []


def log(m):
    L.append(str(m))
    print(m)


gw = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
log('game world (must be False): %s' % (gw is not None))
if gw:
    log('PIE IS RUNNING - refusing to touch the asset, nothing written, nothing lost')
else:
    r = unreal.ToolsetRegistry.execute_tool(TOOLS, 'compile_blueprint',
                                            '{"blueprint": {"refPath": "%s.ABP_AR02_LPSP_Test"}}' % AB)
    log('compile: %s %s' % (r.is_complete, r.error))
    log('save: %s' % unreal.EditorAssetLibrary.save_asset(AB, only_if_is_dirty=False))
    try:
        log('dirty content: %s' % [p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()])
    except Exception as e:
        log('dirty ERR %s' % e)
    try:
        log('dirty maps: %s' % [p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()])
    except Exception as e:
        log('dirty maps ERR %s' % e)

open(unreal.Paths.project_saved_dir() + 'LPSP_AR02/save_abp.log', 'w', encoding='utf-8').write('\n'.join(L))
print('DONE save_abp')
