import unreal

# Step 1e: find additive anims the engine will treat as full poses (ABPT_ANIM_FRAME / ANIM_SCALED with no
# ref_pose_seq). Those double bone scale under Apply Additive. Walks /Game/InfimaGames subfolders through
# the asset registry (no load of the whole Content), loads only AnimSequences.
# Output: Saved/LPSP_AR02/bad_additives.txt


def main():
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    bad, total = [], 0
    for root in ['/Game/InfimaGames/AnimatedLowPolyWeapons/Art/Characters/Animations',
                 '/Game/InfimaGames/AnimatedLowPolyWeapons/Art/Weapons',
                 '/Game/InfimaGames/LowPolyShooterPack']:
        f = unreal.ARFilter(package_paths=[root], recursive_paths=True,
                            class_paths=[unreal.TopLevelAssetPath('/Script/Engine', 'AnimSequence')])
        for d in ar.get_assets(f):
            a = d.get_asset()
            if not a:
                continue
            total += 1
            t = a.get_editor_property('additive_anim_type')
            if t == unreal.AdditiveAnimationType.AAT_NONE:
                continue
            rpt = a.get_editor_property('ref_pose_type')
            if rpt in (unreal.AdditiveBasePoseType.ABPT_ANIM_FRAME, unreal.AdditiveBasePoseType.ABPT_ANIM_SCALED) \
                    and not a.get_editor_property('ref_pose_seq'):
                bad.append(a.get_path_name())
    out = unreal.Paths.project_saved_dir() + 'LPSP_AR02/bad_additives.txt'
    open(out, 'w', encoding='utf-8').write('\n'.join(bad))
    print('checked', total, 'bad', len(bad))
    for b in bad[:30]:
        print('  BAD', b)
    w = unreal.load_asset('/Game/InfimaGames/AnimatedLowPolyWeapons/Art/Characters/Animations/_Common/A_FP_PCH_Walk_F')
    rp = w.get_editor_property('ref_pose_seq')
    print('Walk_F base:', rp.get_path_name() if rp else None)


main()
