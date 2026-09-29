import unreal

# Does get_nodes_in_graph expose the Slot nodes (and their ids) in the main AnimGraph?

ABP = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
BS = unreal.BlueprintService
nodes = BS.get_nodes_in_graph(ABP, 'AnimGraph')
print('NODES', len(nodes))
hits = []
for info in nodes:
    title = str(info.node_title).replace('\n', ' ')
    type_name = str(info.node_type)
    if 'Slot' in type_name or 'Slot' in title or title.endswith("Standing'") or title.endswith("Aiming'"):
        hits.append((str(info.node_id), type_name, title))
print('HITS', len(hits))
for node_id, type_name, title in hits[:20]:
    print('  ', node_id, '|', type_name, '|', title)
