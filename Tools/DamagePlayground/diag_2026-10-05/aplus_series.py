import unreal, math
# A+ check (2026-10-05): a list of (label, target, from_local, aim_local); for each, teleport, aim with
# the camera, fire CLICKS single trigger pulls, one per few frames. Frame-based: an editor in the
# background runs at a few fps. Read [PROJ_DEBUG] / [ZONE_DEBUG] / [APLUS] in the log afterwards.
# Tank local space = actor space; the mesh sits 107 cm under the origin (mesh z 28 = local z -79).
try:
    SHOTS
except NameError:
    SHOTS = [("front_right_track", "DP_TANK_NORMAL", (550.0, 83.0, -40.0), (182.0, 83.0, -79.0))]
CLICKS = globals().get("CLICKS", 5)

w = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
pawns = {a.get_actor_label(): a for a in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.Pawn)}
pawn = unreal.GameplayStatics.get_player_pawn(w, 0)
cm = unreal.GameplayStatics.get_player_camera_manager(w, 0)
pc = unreal.GameplayStatics.get_player_controller(w, 0)
state = {"i": -1, "f": 0, "aim": None}

def V(t): return unreal.Vector(*t)

def start(i):
    label, tname, fl, al = SHOTS[i]
    xf = pawns[tname].get_actor_transform()
    pawn.set_actor_location(xf.transform_location(V(fl)), False, True)
    state["aim"] = xf.transform_location(V(al))
    unreal.log("[APLUS] start %s" % label)

def aim():
    d = state["aim"] - cm.get_camera_location()
    pc.set_control_rotation(unreal.Rotator(roll=0.0, pitch=math.degrees(math.atan2(d.z, math.hypot(d.x, d.y))),
                                           yaw=math.degrees(math.atan2(d.y, d.x))))

def _tick(dt):
    global _h
    if state["i"] < 0 or state["f"] >= 6 + CLICKS * 4:
        state["i"] += 1; state["f"] = 0
        if state["i"] >= len(SHOTS):
            pawn.do_stop_firing(); unreal.log("[APLUS] done")
            unreal.unregister_slate_post_tick_callback(_h); return
        start(state["i"])
    f = state["f"]
    aim()
    if f >= 6:
        k = (f - 6) % 4
        if k == 0: pawn.do_start_firing()
        elif k == 1: pawn.do_stop_firing()
    state["f"] += 1
_h = unreal.register_slate_post_tick_callback(_tick)
