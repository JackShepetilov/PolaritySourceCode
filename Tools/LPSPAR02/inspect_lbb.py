import unreal

AB = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
asset = unreal.load_asset(AB)
pkg = asset.get_outermost()

print('=== INSPECTING LAYERED BONE BLEND NODES ===')
for n in unreal.ObjectIterator(unreal.EdGraphNode):
    if n.get_outermost() == pkg and 'LayeredBoneBlend' in n.get_name():
        parent_name = n.get_outer().get_name()
        title = str(n.get_node_title()).replace('\r', '').replace('\n', ' ') if hasattr(n, 'get_node_title') else ''
        try:
            inner = n.get_editor_property('node')
            filters = inner.get_editor_property('layer_setup')
            print('Node: %s in %s [%s]' % (n.get_name(), parent_name, title))
            print('  MeshSpaceRotationBlend:', inner.get_editor_property('mesh_space_rotation_blend'))
            print('  MeshSpaceScaleBlend:', inner.get_editor_property('mesh_space_scale_blend'))
            print('  CurveBlendOption:', inner.get_editor_property('curve_blend_option'))
            for f in filters:
                branch = f.get_editor_property('branch_filter')
                for b in branch:
                    print('    Bone: %s, Depth: %d' % (b.get_editor_property('bone_name'), b.get_editor_property('blend_depth')))
        except Exception as e:
            print('  Err:', e)
