import unreal
w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
assert w
for p in unreal.ObjectIterator(unreal.ShooterCharacter):
    if 'UEDPIE_' not in p.get_path_name():continue
    ps=p.get_editor_property('player_state');weapon=p.get_current_weapon()
    print('PAWN',p.get_path_name(),'ID',ps.get_editor_property('player_id'),'LOCAL',p.is_locally_controlled(),'WEAPON',weapon)
    if weapon:
        print('OWNER',weapon.get_owner(),'ROLE',weapon.get_local_role(),'LOCALWEAPON',weapon.is_locally_controlled() if hasattr(weapon,'is_locally_controlled') else '-')
        for n in ['replicates','only_relevant_to_owner','net_use_owner_relevancy','reload_montage_tp','secondary_reload_montage_tp']:
            print(n,weapon.get_editor_property(n))
        m=weapon.get_third_person_mesh()
        print('MESH',m.get_editor_property('skeletal_mesh_asset'),'ANIM',m.get_anim_instance(),'ROT',m.get_world_rotation())
        for n in m.get_all_socket_names():
            if any(k in str(n).lower() for k in ['muzzle','grip']):print('SOCKET',n,m.get_socket_transform(n,unreal.RelativeTransformSpace.RTS_COMPONENT))
print('SERVER EQUIP API',getattr(unreal.ShooterCharacter,'server_request_equip_weapon',None))
unreal.SystemLibrary.execute_console_command(w,'summon CameraActor')
