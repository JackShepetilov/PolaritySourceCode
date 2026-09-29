import unreal, time

# Per-frame sampler (slate post-tick) for the NPC's weapon meshes: the previous sampler logged the
# COMPONENT transform of the weapon mesh, which cannot show a bone-level flip. This one logs the
# root bone of the third person weapon mesh (component and world), the same for the first person
# copy, what each mesh is playing, and Shot Count. That is what separates:
#   - the weapon mesh's own animation (PlayWeaponMeshAnimation sends ONE asset to BOTH meshes),
#   - the ik_hand_gun bone from the LPSP AnimBP,
#   - the component transform.
# Writes Saved/LPSP_AR02/weapon_bone_samples.txt, stops after DURATION s or when PIE ends.

DURATION = 40.0
OUT = unreal.Paths.project_saved_dir() + 'LPSP_AR02/weapon_bone_samples.txt'
NPC = '/Game/Variant_Shooter/Tests/LPSP_AR02/BP_ShooterNPC_AR02_Test.BP_ShooterNPC_AR02_Test_C'
S = {'h': None, 't0': time.time(), 'lines': []}


def r3(r):
    return 'p%.0f y%.0f r%.0f' % (r.pitch, r.yaw, r.roll)


def asset_of(mesh):
    ai = mesh.get_anim_instance()
    if not ai:
        return 'noanim'
    try:
        a = ai.get_editor_property('CurrentAsset') if hasattr(ai, 'CurrentAsset') else None
    except Exception:
        a = None
    if a is None:
        try:
            a = ai.get_animation_asset()
        except Exception:
            a = None
    return (a.get_name() if a else '?') + '|' + ai.get_class().get_name()


def _tick(dt):
    t = time.time() - S['t0']
    w = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
    if t > DURATION or not w:
        unreal.unregister_slate_post_tick_callback(S['h'])
        open(OUT, 'w', encoding='utf-8').write('\n'.join(S['lines']))
        return
    cls = unreal.load_class(None, NPC)
    npcs = unreal.GameplayStatics.get_all_actors_of_class(w, cls)
    if not npcs:
        return
    n = npcs[0]
    body = n.get_editor_property('mesh')
    wps = [a for a in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.load_class(None, '/Script/Polarity.ShooterWeapon'))]
    wep = None
    for a in wps:
        if a.get_attach_parent_actor() == n:
            wep = a
            break
    if not wep:
        return
    parts = []
    for mesh in wep.get_components_by_class(unreal.SkeletalMeshComponent):
        skel = mesh.get_skinned_asset()
        if not skel:
            continue
        par = mesh.get_attach_parent()
        tag = 'TP' if par == body else 'FP'
        rb = mesh.get_socket_transform('root', unreal.RelativeTransformSpace.RTS_WORLD).rotation.rotator()
        rbc = mesh.get_socket_transform('root', unreal.RelativeTransformSpace.RTS_COMPONENT).rotation.rotator()
        parts.append('%s rootW=%s rootC=%s comp=%s mesh=%s playing=%s' % (
            tag, r3(rb), r3(rbc), r3(mesh.get_world_rotation()), skel.get_name(), asset_of(mesh)))
    ikw = body.get_socket_transform('ik_hand_gun', unreal.RelativeTransformSpace.RTS_WORLD).rotation.rotator()
    ai = body.get_anim_instance()
    S['lines'].append('%.2f shots=%d ik_hand_gunW=%s | %s' % (
        t, ai.get_editor_property('Shot Count'), r3(ikw), ' | '.join(parts)))


S['h'] = unreal.register_slate_post_tick_callback(_tick)
print('WEAPON BONE SAMPLER armed', DURATION, '->', OUT)
