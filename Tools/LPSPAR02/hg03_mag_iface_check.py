import json

import unreal

# Whole chain, no reload timing needed: the pack's notify lives inside the reload montage, so playing the
# montage on the body fires the interface event on the weapon, which forwards to the C++ helpers.

OUT = unreal.Paths.project_saved_dir() + 'LPSP_AR02/hg03_mag_iface.jsonl'
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
assert world, 'PIE required'
subject = next(p for p in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.ShooterCharacter)
               if p.is_locally_controlled())
weapon = subject.get_current_weapon()
assert weapon and 'M1911' in weapon.get_name(), 'equip BP_M1911 first'
body = subject.get_editor_property('mesh')
anim = body.get_anim_instance()
montage = weapon.get_editor_property('WeaponMeshSecondaryReloadAnimationTP')
assert montage, 'WeaponMeshSecondaryReloadAnimationTP is empty'
gun_anim = weapon.get_third_person_mesh().get_anim_instance()

open(OUT, 'w').close()
state = {'t': 0.0, 'handle': None, 'done': set()}


def write(payload):
    with open(OUT, 'a', encoding='utf-8') as handle:
        handle.write(json.dumps(payload, default=str) + '\n')


def safe(action):
    try:
        return action()
    except Exception as exc:
        return 'ERR %s' % exc


def report(tag):
    mesh = weapon.get_third_person_mesh()
    mags = []
    for child in mesh.get_children_components(False):
        if isinstance(child, unreal.StaticMeshComponent):
            mags.append({'name': child.get_name(), 'visible': safe(lambda: child.is_visible()),
                         'mesh': safe(lambda: str(child.get_editor_property('static_mesh')).split('/')[-1][:40])})
    props = [{'name': a.get_name(), 'class': a.get_class().get_name()}
             for a in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor)
             if 'Magazine' in a.get_name() or 'Magazine' in a.get_class().get_name()]
    write({'t': round(state['t'], 3), 'tag': tag,
           'parts': mags,
           'spawned_prop': safe(lambda: weapon.get_editor_property('SpawnedMagazineProp').get_name()),
           'magazine_actors': props[:4],
           'reloading': weapon.is_reloading()})


def once(key, at, action):
    if state['t'] >= at and key not in state['done']:
        state['done'].add(key)
        try:
            action()
        except Exception as exc:
            write({'t': round(state['t'], 3), 'action_error': key, 'error': str(exc)})


def tick(dt):
    state['t'] += dt
    once('idle', 0.6, lambda: report('idle'))
    once('play', 1.0, lambda: (anim.montage_play(montage, 1.0), report('montage_started')))
    once('during', 1.5, lambda: report('during_montage'))
    once('after', 2.4, lambda: report('after_montage'))
    once('restore', 2.6, lambda: (weapon.set_magazine_visible(True), report('after_restore')))
    if state['t'] >= 3.2:
        unreal.unregister_slate_post_tick_callback(state['handle'])
        print('MAG IFACE CHECK DONE', OUT)


state['handle'] = unreal.register_slate_post_tick_callback(tick)
print('MAG IFACE CHECK ARMED')
