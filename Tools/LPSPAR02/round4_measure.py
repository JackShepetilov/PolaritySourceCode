import unreal
import json
import time

world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
assert world, 'PIE required'
subject = next(p for p in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.ShooterCharacter)
               if p.is_locally_controlled())
subject.set_editor_property('starting_weapon_class',unreal.load_class(None,
    '/Game/Variant_Shooter/Tests/LPSP_AR02/BP_AR02_Integration_Test.BP_AR02_Integration_Test_C'))
subject.equip_starting_weapon_animated()
player_id=subject.get_editor_property('player_state').get_editor_property('player_id')
for p in unreal.ObjectIterator(unreal.ShooterCharacter):
    if 'UEDPIE_' in p.get_path_name():
        p.get_editor_property('mesh').set_editor_property('visibility_based_anim_tick_option',
            unreal.VisibilityBasedAnimTickOption.ALWAYS_TICK_POSE_AND_REFRESH_BONES)
movement = subject.get_apex_movement()
controller = subject.get_controller()
start = subject.get_actor_location()
yaw = subject.get_actor_rotation().yaw
out = unreal.Paths.project_saved_dir() + 'LPSP_AR02/round4_measure.jsonl'
open(out, 'w').close()
state = {'time': 0.0, 'done': set(), 'last': -1.0, 'handle': None}

def once(key, at, action):
    if state['time'] >= at and key not in state['done']:
        state['done'].add(key)
        action()

def slide():
    subject.set_actor_location(start, False, True)
    movement.set_editor_property('velocity', subject.get_actor_forward_vector() * 750.0)
    movement.start_slide()

def tick(dt):
    try:
        if not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world():
            unreal.unregister_slate_post_tick_callback(state['handle'])
            return
        state['time'] += dt
        t = state['time']
        once('up', 1, lambda: controller.set_control_rotation(unreal.Rotator(pitch=45,yaw=yaw)))
        once('ads', .8, lambda: movement.set_aiming(True))
        once('down', 3, lambda: controller.set_control_rotation(unreal.Rotator(pitch=-45,yaw=yaw)))
        once('level', 5, lambda: controller.set_control_rotation(unreal.Rotator(pitch=0,yaw=yaw)))
        once('slide1', 6, slide)
        once('stand', 9, lambda: subject.un_crouch())
        once('slide2', 11, slide)
        if t - state['last'] >= .06:
            state['last'] = t
            for pawn in unreal.ObjectIterator(unreal.ShooterCharacter):
                if 'UEDPIE_' not in pawn.get_path_name():
                    continue
                ps=pawn.get_editor_property('player_state')
                if not ps or ps.get_editor_property('player_id') != player_id:
                    continue
                mesh = pawn.get_editor_property('mesh')
                mov = pawn.get_apex_movement()
                ai = mesh.get_anim_instance()
                def bone(n): return mesh.get_socket_transform(n, unreal.RelativeTransformSpace.RTS_COMPONENT)
                r = {'t': round(t,3), 'pawn':pawn.get_path_name(), 'pitch':pawn.get_aim_pitch_for_animation(),
                     'slide':mov.is_sliding, 'alpha':mov.get_slide_alpha(), 'duration':mov.get_slide_duration(),
                     'pelvis':bone('pelvis').translation.z,
                     'gun':str(bone('ik_hand_gun').rotation.rotator()),
                     'hand_gap':(bone('hand_r').translation-bone('ik_hand_gun').translation).length(),
                     'head':str(bone('head').translation),'spine':str(bone('spine_03').rotation.rotator()),
                     'aiming':ai.get_editor_property('Aiming'),
                     'weapon':str(pawn.get_current_weapon()),
                     'role':str(pawn.get_local_role())}
                with open(out,'a',encoding='utf-8') as f: f.write(json.dumps(r)+'\n')
        if t >= 15:
            movement.set_aiming(False)
            subject.un_crouch()
            subject.set_actor_location(start,False,True)
            unreal.unregister_slate_post_tick_callback(state['handle'])
            print('ROUND4 MEASURE COMPLETE',out)
    except Exception as e:
        with open(out,'a',encoding='utf-8') as f: f.write(json.dumps({'error':str(e)})+'\n')
        unreal.unregister_slate_post_tick_callback(state['handle'])

state['handle'] = unreal.register_slate_post_tick_callback(tick)
print('ARMED round4 measure')
