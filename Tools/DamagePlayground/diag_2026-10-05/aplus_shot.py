import unreal, math, time
# A+ check (2026-10-05): put the player at a point in a target's local space, aim at another local
# point, fire a short burst. Edit the three lines below per run. Tank local space = actor space
# (mesh sits 107 cm under the actor origin: mesh z 28 = actor z -79).
TARGET = "DP_TANK_NORMAL"
FROM_LOCAL = unreal.Vector(900.0, 83.0, -40.0)
AIM_LOCAL = unreal.Vector(182.0, 83.0, -79.0)
BURST = 0.5

w = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
tgt = [a for a in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.Pawn) if a.get_actor_label() == TARGET][0]
xf = tgt.get_actor_transform()
pawn = unreal.GameplayStatics.get_player_pawn(w, 0)
src = xf.transform_location(FROM_LOCAL)
pawn.set_actor_location(src, False, True)
cm = unreal.GameplayStatics.get_player_camera_manager(w, 0)
pc = unreal.GameplayStatics.get_player_controller(w, 0)
p = xf.transform_location(AIM_LOCAL)
def aim():
    d = p - cm.get_camera_location()
    yaw = math.degrees(math.atan2(d.y, d.x)); pitch = math.degrees(math.atan2(d.z, math.hypot(d.x, d.y)))
    pc.set_control_rotation(unreal.Rotator(roll=0.0, pitch=pitch, yaw=yaw))
aim()
print("SHOT", TARGET, "from", src, "aim", p, "weapon", pawn.get_current_weapon())
t0 = time.time()
started = [False]
def _tick(dt):
    global _h
    el = time.time() - t0
    if el < 0.3:
        aim()
    if not started[0] and el > 0.3:
        print("EYE", cm.get_camera_location())
        pawn.do_start_firing(); started[0] = True
    if started[0] and el > 0.3 + BURST:
        pawn.do_stop_firing()
        unreal.unregister_slate_post_tick_callback(_h)
_h = unreal.register_slate_post_tick_callback(_tick)
