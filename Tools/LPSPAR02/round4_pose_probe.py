import unreal,json
from pathlib import Path
bp=unreal.load_asset('/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test')
lines=[]
for n in unreal.ObjectIterator(unreal.AnimGraphNode_SequencePlayer):
    if n.get_outermost()!=bp.get_outermost():continue
    for p in n.list_all_pins():
        if str(p.get_pin_name())=='Sequence' and p.get_pin_value() and any(x in str(p.get_pin_value()) for x in ['AR_02','Unarmed']):
            lines.append(n.get_path_name()+' '+str(p.get_pin_value()))
paths=unreal.EditorAssetLibrary.list_assets('/Game/InfimaGames/AnimatedLowPolyWeapons/Art/Characters/Animations/ARs',False,False)
for path in paths:
    if not any(x in path for x in ['A_TP_CH_AR_02_Idle','A_TP_CH_AR_02_Aim']):continue
    a=unreal.load_asset(path)
    if not isinstance(a,unreal.AnimSequence):continue
    pose=unreal.AnimPoseExtensions.get_anim_pose_at_time(a,0,unreal.AnimPoseEvaluationOptions())
    lines.append('POSE '+path)
    for bone in ['root','ik_hand_gun','hand_r','head']:
        t=unreal.AnimPoseExtensions.get_bone_pose(pose,bone,unreal.AnimPoseSpaces.WORLD)
        lines.append(bone+' '+str(t))
Path(unreal.Paths.project_saved_dir(),'LPSP_AR02/round4_pose_probe.txt').write_text('\n'.join(lines),encoding='utf-8')
print('WROTE POSES',len(lines))
