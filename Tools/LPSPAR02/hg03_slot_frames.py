import json

import unreal

# Frame by frame: does the montage advance, does the DefaultSlot node carry weight, does a bone move.

OUT = unreal.Paths.project_saved_dir() + 'LPSP_AR02/hg03_slot_frames.jsonl'
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
assert world, 'PIE required'
subject = next(p for p in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.ShooterCharacter)
               if p.is_locally_controlled())
weapon = subject.get_current_weapon()
body = subject.get_editor_property('mesh')
anim = body.get_anim_instance()
montage = weapon.get_editor_property('HolsterMontageTP')
assert montage, 'HolsterMontageTP is empty'

open(OUT, 'w').close()
state = {'t': 0.0, 'handle': None, 'started': False, 'next': 0.0}


def write(payload):
    with open(OUT, 'a', encoding='utf-8') as handle:
        handle.write(json.dumps(payload, default=str) + '\n')


def safe(action):
    try:
        return action()
    except Exception as exc:
        return 'ERR %s' % exc


def sample(tag):
    hand = body.get_socket_transform('hand_r', unreal.RelativeTransformSpace.RTS_COMPONENT).translation
    write({'t': round(state['t'], 3), 'tag': tag,
           'hand_r': [round(hand.x, 2), round(hand.y, 2), round(hand.z, 2)],
           'montage': safe(lambda: str(anim.get_current_active_montage()).split('.')[-1]),
           'position': safe(lambda: round(anim.montage_get_position(montage), 3)),
           'slot_weight': safe(lambda: round(anim.blueprint_get_slot_montage_local_weight('DefaultSlot'), 3))})


def tick(dt):
    state['t'] += dt
    if not state['started'] and state['t'] >= 0.5:
        state['started'] = True
        state['next'] = state['t']
        print('PLAY', safe(lambda: anim.montage_play(montage, 1.0)))
        sample('play')
    elif state['started'] and state['t'] >= state['next']:
        state['next'] = state['t'] + 0.1
        sample('frame')
        if state['t'] >= 2.2:
            unreal.unregister_slate_post_tick_callback(state['handle'])
            print('SLOT FRAMES DONE', OUT)


state['handle'] = unreal.register_slate_post_tick_callback(tick)
print('SLOT FRAMES ARMED')
