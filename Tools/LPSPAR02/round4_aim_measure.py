import unreal,json
w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
assert w
unreal.SystemLibrary.execute_console_command(w,'polarity.npc.damage 0')
unreal.SystemLibrary.execute_console_command(w,'polarity.npc.freeze 1')
unreal.SystemLibrary.execute_console_command(w,'r.SetRes 1920x1080w')
subject=next(p for p in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.ShooterCharacter) if p.is_locally_controlled())
subject.set_editor_property('starting_weapon_class',unreal.load_class(None,'/Game/Variant_Shooter/Tests/LPSP_AR02/BP_AR02_Integration_Test.BP_AR02_Integration_Test_C'))
subject.equip_starting_weapon_animated()
pid=subject.get_editor_property('player_state').get_editor_property('player_id')
yaw=subject.get_actor_rotation().yaw
out=unreal.Paths.project_saved_dir()+'LPSP_AR02/round4_aim.jsonl'
open(out,'w').close()
s={'t':0.,'last':-1.,'handle':None}
for p in unreal.ObjectIterator(unreal.ShooterCharacter):
    if 'UEDPIE_' in p.get_path_name():p.get_editor_property('mesh').set_editor_property('visibility_based_anim_tick_option',unreal.VisibilityBasedAnimTickOption.ALWAYS_TICK_POSE_AND_REFRESH_BONES)
def tick(dt):
    try:
        s['t']+=dt;t=s['t']
        pitch=0 if t<1 or t>5 else 45 if t<3 else -45
        subject.get_controller().set_control_rotation(unreal.Rotator(pitch=pitch,yaw=yaw))
        subject.get_apex_movement().set_aiming(t>1)
        if t-s['last']>.1:
            s['last']=t
            for p in unreal.ObjectIterator(unreal.ShooterCharacter):
                if 'UEDPIE_' not in p.get_path_name():continue
                ps=p.get_editor_property('player_state')
                if not ps or ps.get_editor_property('player_id')!=pid:continue
                m=p.get_editor_property('mesh');weapon=p.get_current_weapon()
                r={'t':round(t,3),'role':str(p.get_local_role()),'actor_yaw':p.get_actor_rotation().yaw,
                   'pitch':p.get_aim_pitch_for_animation(),'weapon':weapon.get_name() if weapon else None}
                for n in ['root','ik_hand_gun','hand_r','ik_hand_r','head','spine_03','VB RHS_ik_hand_gun']:
                    tr=m.get_socket_transform(n,unreal.RelativeTransformSpace.RTS_COMPONENT)
                    rot=tr.rotation.rotator()
                    r[n]={'xyz':[tr.translation.x,tr.translation.y,tr.translation.z],'rot':[rot.pitch,rot.yaw,rot.roll]}
                if weapon:
                    wm=weapon.get_third_person_mesh()
                    r['weapon_rotation']=str(wm.get_world_rotation())
                    r['weapon_relative']=str(wm.get_relative_transform())
                    r['weapon_attach']=str(wm.get_attach_socket_name())
                with open(out,'a',encoding='utf-8') as f:f.write(json.dumps(r)+'\n')
        if t>6:
            unreal.unregister_slate_post_tick_callback(s['handle']);print('AIM DONE')
    except Exception as e:
        with open(out,'a',encoding='utf-8') as f:f.write(json.dumps({'error':str(e)})+'\n')
        unreal.unregister_slate_post_tick_callback(s['handle'])
s['handle']=unreal.register_slate_post_tick_callback(tick)
print('AIM ARMED')
