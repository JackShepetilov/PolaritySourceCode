import json

import unreal

# Which mesh the Infima magazine row carries, and what the BP magazine component holds by default.
# Read-only.

BP = '/Game/Variant_Shooter/Tests/LPSP_AR02/BP_AR02_Integration_Test'
DT = '/Game/InfimaGames/LowPolyShooterPack/Data/Weapons/Settings/DT_LPSP_WEP_Settings_Magazines'


def props(obj, limit=200):
    out = {}
    for name in dir(obj):
        if name.startswith('_'):
            continue
        try:
            value = getattr(obj, name)
        except Exception:
            continue
        if callable(value):
            continue
        out[name] = str(value)[:limit]
    return out


table = unreal.EditorAssetLibrary.load_asset(DT)
print('DT', table, bool(table))
rows = unreal.DataTableFunctionLibrary.get_data_table_row_names(table)
print('ROWS', list(rows))
row_struct = table.get_editor_property('row_struct')
print('ROW_STRUCT', row_struct)
try:
    ok, row = unreal.DataTableFunctionLibrary.get_data_table_row(table, 'Assault-Rifle-02', row_struct)
    print('ROW_OK', ok)
    print('ROW_FIELDS', json.dumps(props(row), default=str))
except Exception as exc:
    print('ROW_ERR', exc, [n for n in dir(unreal.DataTableFunctionLibrary) if 'row' in n.lower()])

blueprint = unreal.EditorAssetLibrary.load_asset(BP)
print('BP_STATUS', blueprint.get_editor_property('status'))
subsystem = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
handles = subsystem.k2_gather_subobject_data_for_blueprint(blueprint)
for handle in handles:
    data = subsystem.k2_find_subobject_data_from_handle(handle)
    obj = unreal.SubobjectDataBlueprintFunctionLibrary.get_object(data)
    name = unreal.SubobjectDataBlueprintFunctionLibrary.get_variable_name(data)
    if obj and isinstance(obj, unreal.StaticMeshComponent):
        try:
            mesh = obj.get_editor_property('static_mesh')
        except Exception as exc:
            mesh = 'ERR %s' % exc
        print('COMP', name, obj.get_name(), 'mesh=', mesh)
        print('COMP_ATTACH', obj.get_attach_parent())
