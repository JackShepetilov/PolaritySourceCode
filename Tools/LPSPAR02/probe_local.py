import unreal

# Local (parent-relative) bone scale of the AR_02 test NPC, plus a few anim instance values.
# Set MOVE = True in globals to order a short walk first (the sample is then taken next call).


def main(move=False):
    w = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
    if not w:
        print('NO PIE')
        return
    cls = unreal.load_class(None, '/Game/Variant_Shooter/Tests/LPSP_AR02/BP_ShooterNPC_AR02_Test.BP_ShooterNPC_AR02_Test_C')
    n = unreal.GameplayStatics.get_all_actors_of_class(w, cls)[0]
    m = n.get_editor_property('mesh')
    ai = m.get_anim_instance()
    if move:
        c = n.get_controller()
        dest = n.get_actor_location() + n.get_actor_right_vector() * 800.0
        print('move result', c.move_to_location(dest, 50.0, True, True, False, True))
        print('MOVE ordered', dest)
    vals = {}
    for v in ['Is Moving', 'Aiming', 'Crouching', 'Running', 'Lowered', 'Jumping', 'Tactical Sprinting']:
        try:
            vals[v] = ai.get_editor_property(v)
        except Exception:
            pass
    print('speed %.0f' % n.get_velocity().length(), vals)
    res = []
    for i in range(m.get_num_bones()):
        b = m.get_bone_name(i)
        par = m.get_parent_bone(b)
        t = m.get_socket_transform(b, unreal.RelativeTransformSpace.RTS_COMPONENT)
        ps = 1.0
        if par and str(par) != 'None':
            ps = m.get_socket_transform(par, unreal.RelativeTransformSpace.RTS_COMPONENT).scale3d.x
        ls = t.scale3d.x / max(1e-6, ps)
        if abs(ls - 1) > 0.05:
            res.append('%s:%.2f' % (b, ls))
    print('local-scaled', len(res), ' '.join(res))
