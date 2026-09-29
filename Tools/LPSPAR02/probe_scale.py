import unreal

# Samples the AR_02 test NPC: bones whose component-space scale left 0.5..1.5, the montage playing,
# and the NPC's distance to the nearest player. Read-only, run repeatedly during PIE.


def main():
    w = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
    if not w:
        print('NO PIE')
        return
    cls = unreal.load_class(None, '/Game/Variant_Shooter/Tests/LPSP_AR02/BP_ShooterNPC_AR02_Test.BP_ShooterNPC_AR02_Test_C')
    for n in unreal.GameplayStatics.get_all_actors_of_class(w, cls):
        m = n.get_editor_property('mesh')
        ai = m.get_anim_instance()
        bad = []
        for i in range(m.get_num_bones()):
            b = m.get_bone_name(i)
            s = m.get_socket_transform(b, unreal.RelativeTransformSpace.RTS_COMPONENT).scale3d
            mx = max(abs(s.x), abs(s.y), abs(s.z))
            if mx > 1.5 or mx < 0.5:
                bad.append('%s=%.1f' % (b, mx))
        mt = ai.get_current_active_montage() if ai else None
        print(n.get_name(), 'hp?', 'montage', mt.get_name() if mt else None, 'scaled', len(bad), ' '.join(bad[:12]))
        for v in ['Aiming', 'Crouching', 'Is Moving', 'Running', 'Lowered', 'Holstered', 'Jumping']:
            try:
                print('  ', v, ai.get_editor_property(v))
            except Exception:
                pass


main()
