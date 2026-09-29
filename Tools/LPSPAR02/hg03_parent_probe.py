import unreal

for path in ('/Game/InfimaGames/LowPolyShooterPack/Usable/Weapons/BP_LPSP_WEP_Handgun_03',
             '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'):
    cls = unreal.EditorAssetLibrary.load_blueprint_class(path)
    print('CLASS', cls.get_path_name())
    try:
        print('PARENT', unreal.BlueprintService.get_parent_class(path))
    except Exception as exc:
        print('PARENT_ERR', exc)
