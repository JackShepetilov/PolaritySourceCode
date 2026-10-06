"""Drive PIE to look at tracers. One action per call (frames only advance between calls):
start -> setup -> fire / side -> (screenshot from PowerShell: Source/Tools/TurretBench/winshot.ps1) -> stop.
"""
import json

import unreal

UES = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
TAG = "TRACER_TEST_TMP"


def world():
    return UES.get_game_world()


def pawn():
    w = world()
    if not w:
        return None
    pc = unreal.GameplayStatics.get_player_controller(w, 0)  # editor test only: the one local PIE player
    return pc, pc.get_controlled_pawn()


def start():
    s = unreal.get_default_object(unreal.load_class(None, "/Script/UnrealEd.LevelEditorPlaySettings"))
    s.set_editor_property("NewWindowWidth", 1920)
    s.set_editor_property("NewWindowHeight", 1080)
    perf = unreal.get_default_object(unreal.load_class(None, "/Script/UnrealEd.EditorPerformanceSettings"))
    perf.set_editor_property("bThrottleCPUWhenNotForeground", False)
    for c in ("t.IdleWhenNotForeground 0", "polarity.player.god 1", "polarity.ai.IgnorePlayers 1"):
        unreal.SystemLibrary.execute_console_command(UES.get_editor_world(), c)
    r = unreal.ToolsetRegistry.execute_tool("EditorToolset.EditorAppToolset", "StartPIE", json.dumps(
        {"options": {"bSimulate": False, "playMode": "PlayMode_InEditorFloating", "warmupSeconds": 3.0}}))
    print("StartPIE:", r.is_complete, r.error)


def cmd(c):
    unreal.SystemLibrary.execute_console_command(world(), c)
    print("CMD:", c)


def setup(weapon=None, slomo=0.05):
    pc, p = pawn()
    print("pawn:", p.get_name() if p else None)
    if weapon:
        cls = unreal.EditorAssetLibrary.load_blueprint_class(weapon)
        print("added:", p.add_weapon_class_animated(cls))
    else:
        p.equip_starting_weapon_animated()
    cmd("slomo %s" % slomo)


def fire(on=True):
    pc, p = pawn()
    if on:
        p.do_start_firing()
    else:
        p.do_stop_firing()
    print("firing:", on)


def side(back=300.0, right=250.0, up=40.0, yaw=-60.0):
    """Camera beside and behind the player, looking along the shot, so the streak is seen from the side."""
    pc, p = pawn()
    w = world()
    for a in unreal.GameplayStatics.get_all_actors_with_tag(w, TAG):
        a.destroy_actor()
    loc = p.get_actor_location()
    fwd = pc.get_control_rotation().get_forward_vector()
    rgt = pc.get_control_rotation().get_right_vector()
    at = loc - fwd * back + rgt * right + unreal.Vector(0, 0, up)
    rot = pc.get_control_rotation()
    rot = unreal.Rotator(roll=0.0, pitch=rot.pitch, yaw=rot.yaw + yaw)
    cam = w.spawn_actor(unreal.CameraActor, at, rot) if hasattr(w, "spawn_actor") else \
        unreal.GameplayStatics.begin_deferred_actor_spawn_from_class(w, unreal.CameraActor, unreal.Transform(at, rot))
    if not hasattr(w, "spawn_actor"):
        unreal.GameplayStatics.finish_spawning_actor(cam, unreal.Transform(at, rot))
    cam.tags = [unreal.Name(TAG)]
    pc.set_view_target_with_blend(cam, 0.0)
    print("side cam at", at)


def own_view():
    pc, p = pawn()
    pc.set_view_target_with_blend(p, 0.0)
    print("view back to pawn")


def tracers():
    """Live tracer components: is the particle alive, where, how fast."""
    w = world()
    n = 0
    for a in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.load_class(None, "/Script/Polarity.ShooterProjectile")):
        if a.get_editor_property("hidden"):
            continue
        for c in a.get_components_by_class(unreal.NiagaraComponent):
            print(a.get_name(), c.get_asset().get_name() if c.get_asset() else None, "active:", c.is_active(),
                  "loc:", a.get_actor_location(), "vel:", a.get_velocity().length())
            n += 1
    print("visible rounds with tracer:", n)


def live_rounds():
    w = world()
    cls = unreal.load_class(None, "/Script/Polarity.ShooterProjectile")
    return [a for a in unreal.GameplayStatics.get_all_actors_of_class(w, cls) if not a.get_editor_property("hidden")]


def inspect():
    pc, p = pawn()
    cam = pc.player_camera_manager.get_camera_location()
    for a in live_rounds()[:6]:
        fx = a.get_components_by_class(unreal.NiagaraComponent)
        v = a.get_velocity()
        print("%s dist=%.0f speed=%.0f velDir=%s actorFwd=%s fxFwd=%s fxActive=%s" % (
            a.get_class().get_name(), (a.get_actor_location() - cam).length(), v.length(),
            v.normal(), a.get_actor_forward_vector(), fx[0].get_forward_vector() if fx else None,
            fx[0].is_active() if fx else None))
    print("live rounds:", len(live_rounds()))


def look_from_side(dist=400.0, back=200.0):
    """Time must be frozen. Moves the pawn beside the nearest live round and looks at it."""
    pc, p = pawn()
    cam = pc.player_camera_manager.get_camera_location()
    rounds = sorted(live_rounds(), key=lambda a: (a.get_actor_location() - cam).length())
    if not rounds:
        print("no live rounds")
        return
    r = rounds[min(1, len(rounds) - 1)]
    loc = r.get_actor_location()
    fwd = r.get_velocity().normal()
    side = fwd.cross(unreal.Vector(0, 0, 1)).normal()
    eye = loc + side * dist - fwd * back
    p.set_actor_location(eye - unreal.Vector(0, 0, 60), False, True)
    look = unreal.MathLibrary.find_look_at_rotation(eye, loc)
    pc.set_control_rotation(unreal.Rotator(roll=0.0, pitch=look.pitch, yaw=look.yaw))
    print("round at", loc, "flying", fwd, "| eye", eye)


def stop():
    if world():
        cmd("slomo 1")
    r = unreal.ToolsetRegistry.execute_tool("EditorToolset.EditorAppToolset", "StopPIE", "{}")
    print("StopPIE:", r.is_complete, r.error)


ACT = globals().get("ACT", "")
if ACT:
    exec(ACT)
