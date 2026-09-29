import unreal
for path in ['/Game/InfimaGames/LowPolyShooterPack/Core/Characters/ABP_LPSP_TP_PCH','/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test']:
    bp=unreal.load_asset(path)
    ns={n.get_name():n for n in unreal.ObjectIterator(unreal.EdGraphNode) if n.get_outermost()==bp.get_outermost() and n.get_outer().get_name()=='AnimGraph'}
    print(path)
    def trace(n,seen):
        if n in seen:return
        seen.add(n)
        print(n.get_name(),str(n.get_node_title()))
        if n.get_name()=='AnimGraphNode_LocalToComponentSpace_5':return
        for p in n.list_all_pins():
            if p.get_pin_direction()==unreal.EdGraphPinDirection.EGPD_INPUT and str(p.get_pin_name()) in ['ComponentPose','LocalPose','InputPin']:
                for q in p.list_connected_pins():trace(q.get_owning_node(),seen)
    trace(ns['AnimGraphNode_ComponentToLocalSpace_5'],set())
