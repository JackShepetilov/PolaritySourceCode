import unreal

AB = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
info = unreal.BlueprintService.get_blueprint_info(AB)
var_names = [v.variable_name for v in (info.variables or [])]
print('Has SlideAlpha:', 'SlideAlpha' in var_names)
print('Has CrouchAlpha:', 'CrouchAlpha' in var_names)
for v in (info.variables or []):
    if 'slide' in v.variable_name.lower():
        print('Slide-related var:', v.variable_name, v.variable_type)
