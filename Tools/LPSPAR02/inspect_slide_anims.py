import unreal

# Inspect all slide animations in /Game/Variant_Shooter/Tests/LPSP_AR02/SlideAnims/
# Verify skeleton, additive settings, sequence length, and root motion status.

PATH = '/Game/Variant_Shooter/Tests/LPSP_AR02/SlideAnims'
assets = unreal.EditorAssetLibrary.list_assets(PATH, recursive=False, include_folder=False)
print('Total slide assets found:', len(assets))

foot_out = []
for a_path in sorted(assets):
    name = a_path.split('/')[-1].split('.')[0]
    if 'FootOut' not in name:
        continue
    anim = unreal.load_asset(a_path)
    if not anim or not isinstance(anim, unreal.AnimSequence):
        continue
    skel = anim.get_editor_property('skeleton')
    add_type = anim.get_editor_property('additive_anim_type')
    ref_type = anim.get_editor_property('ref_pose_type')
    ref_seq = anim.get_editor_property('ref_pose_seq')
    num_frames = anim.get_editor_property('number_of_sampled_keys')
    
    skel_name = skel.get_name() if skel else 'None'
    ref_name = ref_seq.get_name() if ref_seq else 'None'
    
    foot_out.append({
        'name': name,
        'skel': skel_name,
        'add_type': str(add_type),
        'ref_type': str(ref_type),
        'ref_seq': ref_name,
        'frames': num_frames
    })

print('\n=== FootOut Animations (%d) ===' % len(foot_out))
for item in foot_out:
    print('  %s | Skel: %s | Add: %s (RefType: %s, Base: %s) | Frames: %s' % (
        item['name'], item['skel'], item['add_type'], item['ref_type'], item['ref_seq'], item['frames']))
