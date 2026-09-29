import unreal

# PIE probe for the AR_02 test NPC: bone scale blow-ups, active montages, key anim instance values.
# Read-only. Run while PIE is up.


def main():
    w = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
    if not w:
        print('NO PIE')
        return
    cls = unreal.load_class(None, '/Game/Variant_Shooter/Tests/LPSP_AR02/BP_ShooterNPC_AR02_Test.BP_ShooterNPC_AR02_Test_C')
    npcs = unreal.GameplayStatics.get_all_actors_of_class(w, cls)
    print('npcs', len(npcs))
    for n in npcs:
        m = n.get_editor_property('mesh')
        ai = m.get_anim_instance()
        print('npc', n.get_name(), 'loc', n.get_actor_location(), 'anim', ai.get_class().get_name() if ai else None)
        names = m.get_all_socket_names()
        bad = []
        for b in [m.get_bone_name(i) for i in range(m.get_num_bones())]:
            t = m.get_socket_transform(b, unreal.RelativeTransformSpace.RTS_COMPONENT)
            s = t.scale3d
            mx = max(abs(s.x), abs(s.y), abs(s.z))
            if mx > 1.5 or mx < 0.5:
                bad.append((str(b), round(s.x, 2), round(s.y, 2), round(s.z, 2)))
        print('scaled bones', len(bad), bad[:25])
        if ai:
            mt = ai.get_current_active_montage()
            print('montage', mt.get_name() if mt else None)
            for v in ['Actor Weapon', 'Aiming', 'Crouching', 'Is Moving', 'Running', 'Lowered', 'Holstered',
                      'Third Person', 'Shot Count', 'Lean Alpha', 'Look Offset', 'Data Table Sequences',
                      'Sequence Loop Weapon Jog', 'SlideAlpha']:
                try:
                    val = ai.get_editor_property(v)
                    print('  ', v, '=', val.get_name() if hasattr(val, 'get_name') else val)
                except Exception as e:
                    print('  ', v, 'ERR')
        wep = n.get_editor_property('weapon') if hasattr(n, 'weapon') else None
        print('weapon', wep)


main()
