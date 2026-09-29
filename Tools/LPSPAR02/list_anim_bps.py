import unreal

BS = unreal.BlueprintService
ar = unreal.AssetRegistryHelpers.get_asset_registry()
f = unreal.ARFilter(class_names=['AnimBlueprint'], package_paths=['/Game'], recursive_paths=True)
assets = ar.get_assets(f)
paths = sorted(str(a.package_name) for a in assets)
print('AnimBlueprints in /Game:', len(paths))

for p in paths:
    info = BS.get_blueprint_info(p)
    names = [v.variable_name for v in (info.variables or [])] if info else []
    marks = []
    for key in ('SlideAlpha', 'CrouchAlpha', 'Aiming', 'Crouching'):
        if key in names:
            marks.append(key)
    tag = ' <== ' + ','.join(marks) if marks else ''
    print('   %-78s vars=%3d%s' % (p, len(names), tag))
