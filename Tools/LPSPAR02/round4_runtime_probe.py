import unreal
w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
assert w
unreal.SystemLibrary.execute_console_command(w,'npc.damage 0')
unreal.SystemLibrary.execute_console_command(w,'npc.freeze 1')
unreal.SystemLibrary.execute_console_command(w,'r.SetRes 1920x1080w')
for p in unreal.ObjectIterator(unreal.ShooterCharacter):
    if 'UEDPIE_' not in p.get_path_name(): continue
    if p.has_authority(): p.equip_starting_weapon_animated()
    ps=p.get_editor_property('player_state')
    print('PAWN',p.get_path_name(),'role',p.get_local_role(),'local',p.is_locally_controlled(),
        'id',ps.get_editor_property('player_id') if ps else None,'weapon',p.get_current_weapon(),
        'starting',p.get_editor_property('starting_weapon_class'))
for cls,names in [(unreal.ShooterWeapon,['start_reload','set_bullet_count','can_reload']),
                  (unreal.ApexMovementComponent,['set_aiming'])]:
    for name in names:
        print(name,getattr(cls,name,None).__doc__)
