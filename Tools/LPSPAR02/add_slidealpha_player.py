import unreal, json

# Declare SlideAlpha in the player's third person graphs so the native push reaches them
# (PolarityCharacter::PushCrouchSlideAlphasToAnim pushes by name, opt-in per graph).
TOOLS = 'editor_toolset.toolsets.blueprint.BlueprintTools'
BS = unreal.BlueprintService
TARGETS = ['/Game/Variant_Shooter/Anims/ABP_TP_Rifle', '/Game/Variant_Shooter/Anims/ABP_TP_Pistol']
LOG = unreal.Paths.project_saved_dir() + 'LPSP_AR02/player_anim_vars.log'
L = []


def log(m):
    L.append(str(m))
    print(m)


for path in TARGETS:
    a = unreal.load_asset(path)
    if not a:
        log('MISSING %s' % path)
        continue
    info = BS.get_blueprint_info(path)
    names = [v.variable_name for v in (info.variables or [])] if info else []
    log('%s: %d vars, SlideAlpha=%s' % (path, len(names), 'SlideAlpha' in names))
    if 'SlideAlpha' in names:
        log('   already there, nothing to do')
    else:
        r = unreal.ToolsetRegistry.execute_tool(
            TOOLS, 'add_variable',
            json.dumps({'blueprint': {'refPath': path + '.' + path.split('/')[-1]},
                        'name': 'SlideAlpha', 'type_name': 'float'}))
        log('   add_variable: complete=%s err=%s' % (r.is_complete, r.error))
    c = unreal.ToolsetRegistry.execute_tool(
        TOOLS, 'compile_blueprint', json.dumps({'blueprint': {'refPath': path + '.' + path.split('/')[-1]}}))
    log('   compile: complete=%s err=%s' % (c.is_complete, c.error))
    log('   status: %s' % unreal.load_asset(path).get_editor_property('status'))
    log('   saved: %s' % unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False))
    info2 = BS.get_blueprint_info(path)
    names2 = [v.variable_name for v in (info2.variables or [])] if info2 else []
    log('   verify SlideAlpha: %s' % ('SlideAlpha' in names2))

open(LOG, 'w', encoding='utf-8').write('\n'.join(L))
