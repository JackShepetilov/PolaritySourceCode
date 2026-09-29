import json

import unreal

# Which node sets Sequence Loop Weapon Jog / Look Offset in the shared TP AnimBP, and what feeds it.

ABP = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
OUT = unreal.Paths.project_saved_dir() + 'LPSP_AR02/abp_jog_probe.txt'
TOKENS = ('Sequence Loop Weapon Jog', 'Look Offset', 'Data Table Sequences', 'Data Table Blendspaces',
          'DataTableSequences', 'DataTableBlendspaces')

blueprint = unreal.EditorAssetLibrary.load_asset(ABP)
package = blueprint.get_outermost()
lines = []
for node in unreal.ObjectIterator(unreal.EdGraphNode):
    if node.get_outermost() != package:
        continue
    try:
        title = str(node.get_node_title())
    except Exception:
        continue
    if not any(token in title for token in TOKENS):
        continue
    entry = ['NODE %s | %s | graph=%s' % (node.get_name(), title.replace('\n', ' / '),
                                          node.get_outer().get_name())]
    for pin in node.list_all_pins():
        links = ['%s.%s' % (p.get_owning_node().get_name(), p.get_pin_name())
                 for p in pin.list_connected_pins()] if hasattr(pin, 'list_connected_pins') else []
        entry.append('   %-30s %-8s %s' % (pin.get_pin_name(), pin.get_pin_type(),
                                           ', '.join(links) if links else ''))
    lines.append('\n'.join(entry))
with open(OUT, 'w', encoding='utf-8') as handle:
    handle.write('\n'.join(lines))
print('JOG PROBE DONE', len(lines), OUT)
