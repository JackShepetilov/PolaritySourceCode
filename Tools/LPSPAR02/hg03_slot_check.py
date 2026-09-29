import json

import unreal

# Does a montage authored in DefaultSlot now move the body? Played directly on the body anim instance,
# with a bone measured before and during, so the answer is a number and not an impression.

OUT = unreal.Paths.project_saved_dir() + 'LPSP_AR02/hg03_slot_check.jsonl'
MONTAGE = '/Game/InfimaGames/AnimatedLowPolyWeapons/Art/Characters/Animations/Handguns/AM_TP_CH_Handgun_Holster'
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
assert world, 'PIE required'
subject = next(p for p in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.ShooterCharacter)
               if p.is_locally_controlled())
weapon = subject.get_current_weapon()
assert weapon and 'M1911' in weapon.get_name(), 'equip BP_M1911 first'
body = subject.get_editor_property('mesh')
anim = body.get_anim_instance()
montage = weapon.get_editor_property('HolsterMontageTP')
assert montage, 'HolsterMontageTP is empty on the weapon'

open(OUT, 'w').close()
state = {'t': 0.0, 'handle': None, 'done': set()}


def write(payload):
    with open(OUT, 'a', encoding='utf-8') as handle:
        handle.write(json.dumps(payload, default=str) + '\n')


def sample(tag):
    hand = body.get_socket_transform('hand_r', unreal.RelativeTransformSpace.RTS_COMPONENT).translation
    head = body.get_socket_transform('head', unreal.RelativeTransformSpace.RTS_COMPONENT).translation
    write({'t': round(state['t'], 3), 'tag': tag,
           'hand_r': [round(hand.x, 2), round(hand.y, 2), round(hand.z, 2)],
           'head': [round(head.x, 2), round(head.y, 2), round(head.z, 2)],
           'body_montage': str(anim.get_current_active_montage()),
           'reloading': weapon.is_reloading()})


def once(key, at, action):
    if state['t'] >= at and key not in state['done']:
        state['done'].add(key)
        try:
            action()
        except Exception as exc:
            write({'t': round(state['t'], 3), 'action_error': key, 'error': str(exc)})


def tick(dt):
    try:
        state['t'] += dt
        once('idle', 0.6, lambda: sample('idle'))
        once('play', 1.0, lambda: (anim.montage_play(montage, 1.0), sample('play_started')))
        once('during1', 1.35, lambda: sample('during_1'))
        once('during2', 1.65, lambda: sample('during_2'))
        once('shot', 1.7, lambda: unreal.SystemLibrary.execute_console_command(
            world, 'HighResShot 1920x1080 filename=hg03_slot_holster.png'))
        once('after', 2.4, lambda: sample('after'))
        if state['t'] >= 3.0:
            unreal.unregister_slate_post_tick_callback(state['handle'])
            print('SLOT CHECK COMPLETE', OUT)
    except Exception as exc:
        write({'error': str(exc)})
        unreal.unregister_slate_post_tick_callback(state['handle'])


state['handle'] = unreal.register_slate_post_tick_callback(tick)
print('SLOT CHECK ARMED')
