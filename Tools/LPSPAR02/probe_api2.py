import unreal
import json

# READ ONLY. What is available for creating a variable-getter node in the AnimGraph, and can an existing
# K2Node_VariableGet be repointed at another variable?
L = []


def log(m):
    L.append(str(m))


for cls in ('BlueprintService', 'BlueprintGraphService', 'AnimGraphService'):
    c = getattr(unreal, cls, None)
    log('')
    log('=== %s ===' % cls)
    if not c:
        log('MISSING')
        continue
    log(', '.join(sorted(m for m in dir(c) if not m.startswith('_'))))

log('')
log('=== K2Node_VariableGet properties ===')
pkg = unreal.load_asset('/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test').get_outermost()
for n in unreal.ObjectIterator(unreal.EdGraphNode):
    try:
        if n.get_outermost() == pkg and n.get_name() == 'K2Node_VariableGet_1':
            log('node: %s class %s' % (n.get_name(), n.get_class().get_name()))
            log('attrs: %s' % [a for a in dir(n) if not a.startswith('_')])
            for prop in ('variable_reference', 'variable_name', 'self_context'):
                try:
                    log('   %s = %s' % (prop, n.get_editor_property(prop)))
                except Exception as e:
                    log('   %s ERR %s' % (prop, str(e)[:70]))
    except Exception:
        pass

log('')
log('=== BlueprintTools tool names ===')
try:
    raw = unreal.ToolsetRegistry.get_toolset_json_schema('editor_toolset.toolsets.blueprint.BlueprintTools')
    try:
        raw = raw.get_value_as_json_string()
    except Exception:
        raw = str(raw)
    schema = json.loads(raw)
    names = []
    for path, info in (schema.get('tools') or {}).items():
        names.append('%s :: %s' % (path.split('/')[-1], str(info.get('description', ''))[:60]))
    log('\n'.join(sorted(names)))
except Exception as e:
    log('schema ERR %s' % str(e)[:300])

open(unreal.Paths.project_saved_dir() + 'LPSP_AR02/api_probe2.txt', 'w', encoding='utf-8').write('\n'.join(L))
print('WROTE api_probe2.txt lines=%d' % len(L))
