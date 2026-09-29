import unreal

# What the graph API can do from python: function graphs, pin defaults, node property writes.
# Read-only.

AB = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'

names = [m for m in dir(unreal.BlueprintService) if not m.startswith('_')]
print('BlueprintService (%d):' % len(names))
print('   ' + ', '.join(sorted(names)))
print('BlueprintEditorLibrary func/graph/pin:',
      [m for m in dir(unreal.BlueprintEditorLibrary) if any(k in m.lower() for k in ('func', 'graph', 'pin'))])

bp = unreal.load_asset(AB)
print('bp:', bp, 'status:', bp.get_editor_property('status'))

# The pack's "Set Aiming" node of the copy: what pins does it have and what can a pin do?
node = unreal.find_object(None, AB + '.ABP_AR02_LPSP_Test:EventGraph.K2Node_VariableSet_17')
print('set aiming node:', node)
if node:
    for p in node.list_all_pins():
        d = [m for m in dir(p) if not m.startswith('_')]
        print('  pin %s dir=%s' % (str(p.get_pin_name()), d))
        break
    print('  node props:', [m for m in dir(node) if not m.startswith('_')][:40])
