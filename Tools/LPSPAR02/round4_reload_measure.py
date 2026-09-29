import unreal,json
w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
subject=next(p for p in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.ShooterCharacter) if p.is_locally_controlled())
subject.set_editor_property('starting_weapon_class',unreal.load_class(None,'/Game/Variant_Shooter/Tests/LPSP_AR02/BP_AR02_Integration_Test.BP_AR02_Integration_Test_C'))
subject.equip_starting_weapon_animated()
pid=subject.get_editor_property('player_state').get_editor_property('player_id')
out=unreal.Paths.project_saved_dir()+'LPSP_AR02/round4_reload.jsonl'
open(out,'w').close()
s={'t':0.,'last':-1.,'done':set(),'handle':None}
def write(v):
    with open(out,'a',encoding='utf-8') as f:f.write(json.dumps(v)+'\n')
def reload():
    weapon=subject.get_current_weapon()
    assert 'AR02_Integration' in weapon.get_name(),str(weapon)
    write({'event':'reload','t':s['t'],'secondary':weapon.uses_secondary_reload(),'ok':weapon.start_reload()})
def tick(dt):
    try:
        s['t']+=dt
        for key,at,action in [('fire',2,subject.do_start_firing),('stop',2.4,subject.do_stop_firing),
                              ('tactical',3,reload),('empty_fire',8,subject.do_start_firing)]:
            if s['t']>=at and key not in s['done']:
                s['done'].add(key);action()
        weapon=subject.get_current_weapon()
        if s['t']>8 and weapon and not weapon.uses_secondary_reload() and 'empty' not in s['done']:
            s['done'].add('empty');subject.do_stop_firing();reload()
        if s['t']-s['last']>.1:
            s['last']=s['t']
            for p in unreal.ObjectIterator(unreal.ShooterCharacter):
                if 'UEDPIE_' not in p.get_path_name():continue
                ps=p.get_editor_property('player_state')
                if not ps or ps.get_editor_property('player_id')!=pid:continue
                weapon=p.get_current_weapon()
                body=p.get_editor_property('mesh').get_anim_instance()
                tp=weapon.get_third_person_mesh().get_anim_instance() if weapon else None
                write({'t':round(s['t'],3),'role':str(p.get_local_role()),
                    'body':str(body.get_current_active_montage()) if body else None,
                    'weapon':str(tp.get_current_active_montage()) if tp else None,
                    'secondary':weapon.uses_secondary_reload() if weapon else None,
                    'shots':weapon.get_third_person_shot_count() if weapon else None})
        if s['t']>20:
            subject.do_stop_firing()
            unreal.unregister_slate_post_tick_callback(s['handle']);print('RELOAD DONE')
    except Exception as e:
        write({'error':str(e)});unreal.unregister_slate_post_tick_callback(s['handle'])
s['handle']=unreal.register_slate_post_tick_callback(tick)
print('RELOAD ARMED')
