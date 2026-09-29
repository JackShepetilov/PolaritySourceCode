import json

import unreal

# Stop PIE (graph services read empty while it runs) and dump the body slot chain of the shared TP AnimBP:
# where the Action Standing / Action Aiming slots sit, what feeds them and what they feed.

ABP = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
OUT = unreal.Paths.project_saved_dir() + 'LPSP_AR02/slot_chain.txt'

editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
if editor.get_game_world():
    result = unreal.ToolsetRegistry.execute_tool('EditorToolset.EditorAppToolset', 'StopPIE', '{}')
    print('StopPIE', result.is_complete, result.error)

blueprint = unreal.EditorAssetLibrary.load_asset(ABP)
package = blueprint.get_outermost()
lines = []
for node in unreal.ObjectIterator(unreal.EdGraphNode):
    if node.get_outermost() != package:
        continue
    try:
        title = str(node.get_node_title()).replace('\n', ' / ')
    except Exception:
        continue
    if not (title.startswith('Slot') or 'Slot' in title):
        continue
    entry = ['NODE %s | %s | graph=%s' % (node.get_name(), title, node.get_outer().get_name())]
    for pin in node.list_all_pins():
        try:
            links = ['%s.%s' % (p.get_owning_node().get_name(), p.get_pin_name())
                     for p in pin.list_connected_pins()]
        except Exception:
            links = ['ERR']
        entry.append('   %-24s %-8s %s' % (pin.get_pin_name(), str(pin.get_pin_type()).split('.')[-1],
                                           ', '.join(links) if links else ''))
    lines.append('\n'.join(entry))
with open(OUT, 'w', encoding='utf-8') as handle:
    handle.write('\n'.join(lines))
print('SLOT CHAIN DONE', len(lines), OUT)
