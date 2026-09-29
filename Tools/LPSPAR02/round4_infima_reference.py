import unreal
from pathlib import Path
paths=['/Game/InfimaGames/LowPolyShooterPack/Core/Inventory/BPSC_LPSP_Inventory',
 '/Game/InfimaGames/LowPolyShooterPack/Usable/Weapons/BP_LPSP_WEP_AR_02',
 '/Game/InfimaGames/LowPolyShooterPack/Core/Characters/ABP_LPSP_TP_PCH',
 '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test']
lines=[]
for path in paths:
    bp=unreal.load_asset(path)
    if not bp:continue
    lines.append('\nASSET '+path)
    for n in unreal.ObjectIterator(unreal.EdGraphNode):
        if n.get_outermost()!=bp.get_outermost() or not hasattr(n,'get_node_title'):continue
        title=str(n.get_node_title())
        if not any(x.lower() in title.lower() for x in ['Attach','FABRIK','ik_hand','Aim Offset','Slot','Look','Grip']):continue
        lines.append(n.get_outer().get_name()+' '+n.get_name()+' '+title)
        try:lines.append(n.get_editor_property('node').export_text())
        except Exception:pass
        for pin in n.list_all_pins():
            lines.append(' '+str(pin.get_pin_name())+' = '+str(pin.get_pin_value())+' -> '+str([x.get_owning_node().get_name()+'.'+str(x.get_pin_name()) for x in pin.list_connected_pins()]))
    if 'WEP_AR_02' in path:
        cdo=unreal.get_default_object(bp.generated_class())
        for c in cdo.get_components_by_class(unreal.SceneComponent):
            lines.append('COMP '+c.get_name()+' '+str(c.get_relative_transform()))
for name in ['BS_ALPW_TP_Look','BS_TP_CH_Unarmed_Look']:
    bs=unreal.load_asset('/Game/InfimaGames/AnimatedLowPolyWeapons/Art/Characters/Animations/_Base/'+name)
    if bs:
        lines.append(name)
        for v in bs.get_editor_property('blend_parameters'):lines.append(v.export_text())
        for v in bs.get_editor_property('sample_data'):lines.append(v.export_text())
for suffix in ['Reload','Reload_Empty']:
    m=unreal.load_asset('/Game/InfimaGames/AnimatedLowPolyWeapons/Art/Characters/Animations/ARs/AM_TP_CH_AR_02_'+suffix)
    lines.append(m.get_name()+': '+str([s.export_text() for s in m.get_editor_property('slot_anim_tracks')]))
Path(unreal.Paths.project_saved_dir(),'LPSP_AR02/round4_infima_reference.txt').write_text('\n'.join(lines),encoding='utf-8')
print('WROTE infima reference',len(lines))
