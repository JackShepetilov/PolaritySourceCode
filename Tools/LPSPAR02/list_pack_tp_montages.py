import unreal

ar = unreal.AssetRegistryHelpers.get_asset_registry()
want = ('AM_TP_CH_AR_Fire', 'AM_TP_CH_AR_02_Reload_Empty', 'AM_TP_CH_AR_01_Reload_Empty',
        'AM_TP_CH_AR_01_Holster', 'AM_TP_CH_AR_01_Unholster', 'AM_WEP_AR_02_Fire')
paths = {}
for a in ar.get_assets(unreal.ARFilter(class_names=['AnimMontage'], package_paths=['/Game/InfimaGames'],
                                       recursive_paths=True)):
    n = str(a.asset_name)
    if n in want or ('TP_CH_AR' in n and n.startswith('AM_')):
        paths.setdefault(n, str(a.package_name))

for n in sorted(paths):
    m = unreal.load_asset(paths[n])
    slots = []
    try:
        for t in m.get_editor_property('slot_anim_tracks'):
            slots.append(str(t.get_editor_property('slot_name')))
    except Exception as e:
        slots = ['err %s' % e]
    print('%-34s slots=%s' % (n, slots))
