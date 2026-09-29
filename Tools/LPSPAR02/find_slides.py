import unreal

ar = unreal.AssetRegistryHelpers.get_asset_registry()
filter = unreal.ARFilter(
    class_names=['AnimSequence'],
    package_paths=['/Game'],
    recursive_paths=True
)
assets = ar.get_assets(filter)
slides = [str(a.package_name) for a in assets if 'slide' in str(a.package_name).lower()]
print('Total slide anim sequences in asset registry:', len(slides))
for s in sorted(slides):
    if 'FootOut' in s:
        print(' ', s)
