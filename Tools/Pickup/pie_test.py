"""Hand pickup in PIE, no keys held by anybody [2026-10-17].

  Tools/ue_step.sh Pickup pie_test start     - StartPIE
  Tools/ue_step.sh Pickup pie_test setup     - ignore AI, equip
  Tools/ue_step.sh Pickup pie_test weapon    - a rifle on the floor in front, look at it
  Tools/ue_step.sh Pickup pie_test ammo      - an ammo box on the floor in front, look at it
  Tools/ue_step.sh Pickup pie_test press     - the take key, slowed down (polarity.debug.timescale)
  Tools/ue_step.sh Pickup pie_test state     - what the arms play, what is in the hands
  Tools/ue_step.sh Pickup pie_test normal    - time back to 1
  Tools/ue_step.sh Pickup pie_test stop      - StopPIE
Log: [PICKUP_DEBUG], [GRAPPLE_FETCH].
"""
import json
import unreal

RIFLE = "/Game/Variant_Shooter/Blueprints/Pickups/Weapons/Kinemation/ARs/BP_MX16A4"
AMMO = "/Game/Variant_Shooter/Blueprints/Pickups/Ammo/BP_AmmoPickup"
PICKUP_MONTAGE = "/Game/Variant_Shooter/Anims/Pickup/AM_HandPickup.AM_HandPickup"
SLOW = 0.03


def _world():
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()


def _pawn():
    w = _world()
    return (w, unreal.GameplayStatics.get_player_pawn(w, 0)) if w else (None, None)


def _fp(pawn):
    for c in pawn.get_components_by_class(unreal.SkeletalMeshComponent):
        if c.get_name() == "First Person Mesh":
            return c
    return None


def start():
    res = unreal.ToolsetRegistry.execute_tool("EditorToolset.EditorAppToolset", "StartPIE",
        json.dumps({"options": {"bSimulate": False, "playMode": "PlayMode_InEditorFloating", "warmupSeconds": 3}}))
    print("STARTPIE complete=%s error=%s" % (res.is_complete, res.error))


def stop():
    res = unreal.ToolsetRegistry.execute_tool("EditorToolset.EditorAppToolset", "StopPIE", "{}")
    print("STOPPIE complete=%s error=%s" % (res.is_complete, res.error))


def setup():
    w, pawn = _pawn()
    if not pawn:
        print("NO PIE"); return
    unreal.SystemLibrary.execute_console_command(w, "polarity.ai.IgnorePlayers 1")
    pawn.equip_starting_weapon_animated()
    print("PAWN", pawn.get_class().get_name(), "montage", pawn.get_editor_property("hand_pickup_montage_fp"),
          "grab", pawn.get_editor_property("hand_pickup_grab_time"))


def _place(path):
    w, pawn = _pawn()
    if not pawn:
        print("NO PIE"); return
    pc = pawn.get_controller()
    yaw = pc.get_control_rotation().yaw
    pc.set_control_rotation(unreal.Rotator(roll=0, pitch=0, yaw=yaw))
    unreal.load_object(None, path + "." + path.split("/")[-1] + "_C")
    unreal.SystemLibrary.execute_console_command(w, "polarity.spawn.actor " + path)
    print("SPAWNED", path, "in front of", pawn.get_actor_location())


def weapon():
    _place(RIFLE)


def ammo():
    _place(AMMO)


def look():
    """Look at the nearest thing the take key can take."""
    w, pawn = _pawn()
    if not pawn:
        print("NO PIE"); return
    cam = unreal.GameplayStatics.get_player_camera_manager(w, 0)
    eye = cam.get_camera_location()
    best = None
    for cls in (unreal.DroppedRangedWeapon, unreal.InventoryPickup):
        for a in unreal.GameplayStatics.get_all_actors_of_class(w, cls):
            d = a.get_actor_location().distance(pawn.get_actor_location())
            if best is None or d < best[0]:
                best = (d, a)
    if not best:
        print("NOTHING TO TAKE"); return
    origin, extent = best[1].get_actor_bounds(True)
    rot = unreal.MathLibrary.find_look_at_rotation(eye, origin)
    pawn.get_controller().set_control_rotation(rot)
    print("LOOK at", best[1].get_name(), "dist %.0f" % best[0], "pitch %.0f" % rot.pitch)


def press():
    w, pawn = _pawn()
    if not pawn:
        print("NO PIE"); return
    pc = pawn.get_controller()
    sub = unreal.SubsystemBlueprintLibrary.get_local_player_subsystem(pc, unreal.EnhancedInputLocalPlayerSubsystem)
    action = pawn.get_editor_property("fetch_action")
    unreal.SystemLibrary.execute_console_command(w, "polarity.debug.timescale %.3f" % SLOW)
    sub.inject_input_vector_for_action(action, unreal.Vector(1.0, 0.0, 0.0), [], [])
    print("PRESSED", action.get_name(), "slow", SLOW)


def state():
    w, pawn = _pawn()
    if not pawn:
        print("NO PIE"); return
    anim = _fp(pawn).get_anim_instance()
    m = unreal.load_object(None, PICKUP_MONTAGE)
    cur = anim.get_current_active_montage()
    wpn = pawn.get_current_weapon()
    print("STATE pickup playing %s pos %.2f | active %s | weapon %s hidden %s" % (
        anim.montage_is_playing(m), anim.montage_get_position(m), cur.get_name() if cur else None,
        wpn.get_class().get_name() if wpn else None, wpn.is_hidden() if wpn else None))


def normal():
    w, pawn = _pawn()
    if w:
        unreal.SystemLibrary.execute_console_command(w, "polarity.debug.timescale 1")
        print("TIME 1")
