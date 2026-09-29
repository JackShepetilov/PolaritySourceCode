import unreal, json

BS = unreal.BlueprintService
AB = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
GRAPH = 'AnimGraph'
pkg = unreal.load_asset(AB).get_outermost()
JSON = unreal.Paths.project_saved_dir() + 'LPSP_AR02/slide_nodes.json'

graph_obj = None
for n in unreal.ObjectIterator(unreal.EdGraphNode):
    if n.get_outermost() == pkg and n.get_outer().get_name() == GRAPH:
        graph_obj = n.get_outer()
        break


def names(substr):
    return {n.get_name() for n in unreal.ObjectIterator(unreal.EdGraphNode)
            if n.get_outermost() == pkg and n.get_outer() == graph_obj and substr in n.get_name()}


def find(name):
    for n in unreal.ObjectIterator(unreal.EdGraphNode):
        if n.get_outermost() == pkg and n.get_outer() == graph_obj and n.get_name() == name:
            return n
    return None


def create(key, x, y, substr):
    before = names(substr)
    BS.create_node_by_key(AB, GRAPH, key, x, y)
    new = names(substr) - before
    if len(new) != 1:
        print('  !! ambiguous for', key, sorted(new))
        return None
    return find(sorted(new)[0])


# ---- comparison node candidates ----
for term in ('Less Than', 'Greater Than', 'Float Less', 'PromotableOperator'):
    ks = BS.discover_nodes(AB, term, '', 6)
    print('--- %r' % term)
    for k in ks:
        print('    ', k.spawner_key, '|', k.display_name, '|', k.node_class)

WAREHOUSE = {}
try:
    WAREHOUSE = json.load(open(JSON, encoding='utf-8'))
except Exception:
    pass

STATE = [
    ('PawnOwner', 'FUNC AnimInstance::TryGetPawnOwner', 'K2Node_CallFunction', -3500.0, -4200.0),
    ('CastChar', 'SPAWN K2Node_DynamicCast|Cast To PolarityCharacter', 'K2Node_DynamicCast', -3100.0, -4200.0),
    ('Apex', 'FUNC PolarityCharacter::GetApexMovement', 'K2Node_CallFunction', -2700.0, -4200.0),
    ('IsSliding', 'FUNC ApexMovementComponent::IsSliding', 'K2Node_CallFunction', -2300.0, -4200.0),
    ('SlideDuration', 'FUNC ApexMovementComponent::GetSlideDuration', 'K2Node_CallFunction', -2300.0, -4500.0),
]
for tag, key, substr, x, y in STATE:
    node = create(key, x, y, substr)
    if node:
        WAREHOUSE[tag] = node.get_name()
        print('created %-13s -> %s' % (tag, node.get_name()))

open(JSON, 'w', encoding='utf-8').write(json.dumps(WAREHOUSE, indent=2))
print('saved node names:', WAREHOUSE)

print('\n--- pins of created state nodes ---')
for tag in ('PawnOwner', 'CastChar', 'Apex', 'IsSliding', 'SlideDuration'):
    nm = WAREHOUSE.get(tag)
    n = find(nm) if nm else None
    if n:
        print('%-13s %s' % (tag, [(str(p.get_pin_name()), str(p.get_pin_direction())) for p in n.list_all_pins()]))
