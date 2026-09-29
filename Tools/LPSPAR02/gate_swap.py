import unreal

# Fix the slide phase gates. FAnimNode_BlendListByBool::GetActiveChildIndex() returns
# "bActiveValue ? 0 : 1" (engine source: "intentionally flipped boolean sense"), so BlendPose_0 is the
# TRUE branch. The current wiring has them the other way round, which is why a slide played the EXIT
# chain (Out_Idle_Crouch held at its last frame) instead of Into/Loop.
# Every break is done on the receiver pin, never on an output pin.
AB = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
GRAPH = 'AnimGraph'
TOOLS = 'editor_toolset.toolsets.blueprint.BlueprintTools'
pkg = unreal.load_asset(AB).get_outermost()
L = []


def log(m):
    L.append(str(m))
    print(m)


def find(name):
    for n in unreal.ObjectIterator(unreal.EdGraphNode):
        try:
            if n.get_outermost() == pkg and n.get_outer() and n.get_outer().get_name() == GRAPH and n.get_name() == name:
                return n
        except Exception:
            pass
    return None


def pin(node, name, in_side=True):
    if not node:
        return None
    for p in node.list_all_pins():
        if str(p.get_pin_name()) == name and (p.get_pin_direction() == unreal.EdGraphPinDirection.EGPD_INPUT) == in_side:
            return p
    return None


SWAPS = (
    # gate, source for the TRUE branch (BlendPose_0), source for the FALSE branch (BlendPose_1)
    ('AnimGraphNode_BlendListByBool_4', 'AnimGraphNode_BlendListByBool_3', 'AnimGraphNode_BlendListByBool_6'),
    ('AnimGraphNode_BlendListByBool_3', 'AnimGraphNode_SequencePlayer_5', 'AnimGraphNode_SequencePlayer_6'),
    ('AnimGraphNode_BlendListByBool_6', 'AnimGraphNode_SequencePlayer_1', 'AnimGraphNode_SequencePlayer_7'),
)

for gate_name, true_src, false_src in SWAPS:
    gate = find(gate_name)
    log('')
    log('%s' % gate_name)
    if not gate:
        log('   MISSING')
        continue
    p0 = pin(gate, 'BlendPose_0', True)
    p1 = pin(gate, 'BlendPose_1', True)
    src_true = pin(find(true_src), 'Pose', False)
    src_false = pin(find(false_src), 'Pose', False)
    if not (p0 and p1 and src_true and src_false):
        log('   pins missing: p0=%s p1=%s srcTrue=%s srcFalse=%s' % (bool(p0), bool(p1), bool(src_true), bool(src_false)))
        continue
    log('   before: p0 <- %s | p1 <- %s'
        % ([x.get_owning_node().get_name() for x in p0.list_connected_pins()],
           [x.get_owning_node().get_name() for x in p1.list_connected_pins()]))
    p0.break_pin_links()
    p1.break_pin_links()
    log('   p0 <- %-30s : %s' % (true_src, p0.try_create_connection(src_true)))
    log('   p1 <- %-30s : %s' % (false_src, p1.try_create_connection(src_false)))
    log('   after : p0 <- %s | p1 <- %s'
        % ([x.get_owning_node().get_name() for x in p0.list_connected_pins()],
           [x.get_owning_node().get_name() for x in p1.list_connected_pins()]))
    # a re-entry should restart the clip, not resume it at its end
    try:
        inner = gate.get_editor_property('node')
        inner.set_editor_property('reset_child_on_activation', True)
        gate.set_editor_property('node', inner)
        log('   reset_child_on_activation = %s' % gate.get_editor_property('node').get_editor_property('reset_child_on_activation'))
    except Exception as e:
        log('   reset_child_on_activation ERR %s' % str(e)[:80])

r = unreal.ToolsetRegistry.execute_tool(TOOLS, 'compile_blueprint',
                                        '{"blueprint": {"refPath": "%s.ABP_AR02_LPSP_Test"}}' % AB)
log('')
log('compile: %s %s' % (r.is_complete, r.error))
log('game world (must be False): %s' % (unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is not None))
log('save: %s' % unreal.EditorAssetLibrary.save_asset(AB, only_if_is_dirty=False))
try:
    log('dirty content: %s' % [p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()])
except Exception as e:
    log('dirty ERR %s' % e)

open(unreal.Paths.project_saved_dir() + 'LPSP_AR02/gate_swap.log', 'w', encoding='utf-8').write('\n'.join(L))
print('DONE gate_swap')
