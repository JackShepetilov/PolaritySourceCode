import unreal

# Save the repaired graph and report the final wiring of the slide branch (currently detached).
AB = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
GRAPH = 'AnimGraph'
BS = unreal.BlueprintService
pkg = unreal.load_asset(AB).get_outermost()
L = []


def log(m):
    L.append(str(m))
    print(m)


def find(name):
    for n in unreal.ObjectIterator(unreal.EdGraphNode):
        if n.get_outermost() == pkg and n.get_outer() and n.get_outer().get_name() == GRAPH and n.get_name() == name:
            return n
    return None


log('world: %s' % (unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is not None))
log('save: %s' % unreal.EditorAssetLibrary.save_asset(AB, only_if_is_dirty=False))
for nm in ('AnimGraphNode_Fabrik_8', 'AnimGraphNode_ComponentToLocalSpace_5', 'AnimGraphNode_LocalToComponentSpace_5',
           'AnimGraphNode_UseCachedPose_19', 'AnimGraphNode_LayeredBoneBlend_3', 'AnimGraphNode_BlendListByBool_5'):
    n = find(nm)
    if not n:
        log('%s MISSING' % nm)
        continue
    log('%s:' % nm)
    for p in n.list_all_pins():
        q = p.list_connected_pins()
        if q or p.get_pin_direction() == unreal.EdGraphPinDirection.EGPD_OUTPUT:
            log('   %-14s %s' % (str(p.get_pin_name()), [x.get_owning_node().get_name() for x in q]))
open(unreal.Paths.project_saved_dir() + 'LPSP_AR02/final_state.log', 'w', encoding='utf-8').write('\n'.join(L))
