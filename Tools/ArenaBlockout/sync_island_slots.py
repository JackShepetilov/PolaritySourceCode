# Reads the author's in-editor arena placements on Lvl_Biome1_Island back into
# the layout source of truth (Island/biome1_island_layout.json).
#
# Author workflow: move arenas directly in the editor to SHOW where you want
# them - two ways, both work, mix freely:
#   1. Levels panel -> select the arena sublevel -> Edit Level Transform
#      (moves the whole arena properly), or just drag all its actors at once
#      (DON'T save the arena afterwards - the rebuild resets it cleanly);
#   2. drop a marker actor anywhere and name its LABEL "SLOT_<id>" (SLOT_G1,
#      SLOT_M3, SLOT_Citadel...) - marker location = wanted arena center,
#      marker yaw = wanted entrance yaw. Markers WIN over sublevel transforms.
#
# Then Claude runs this script (in-editor), it updates the JSON (positions,
# yaws, route corridor points), and the island is regenerated + rebuilt so the
# terrain pads, bridges and the <=30 deg route follow the new slots.
#
# The script only WRITES THE JSON - it never saves or modifies levels.
# Log tag: [SLOT_SYNC]

import json
import math
import os
import sys

import unreal

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.append(TOOLS_DIR)

LAYOUT = os.path.join(TOOLS_DIR, "Island", "biome1_island_layout.json")
ARENA_ROOT = "/Game/Variant_Shooter/Arenas/Biome1"


def log(msg):
    unreal.log("[SLOT_SYNC] {}".format(msg))


def warn(msg):
    unreal.log_warning("[SLOT_SYNC] {}".format(msg))


def norm_yaw(y):
    return ((y + 180.0) % 360.0) - 180.0


def main():
    with open(LAYOUT, encoding="utf-8") as f:
        layout = json.load(f)
    slots = {s["id"]: s for s in layout["slots"]}

    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    pkg = world.get_package().get_name()
    if not pkg.endswith("Lvl_Biome1_Island"):
        raise RuntimeError("Open Lvl_Biome1_Island first (current: {})".format(pkg))
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

    # markers: actor label SLOT_<id> wins over everything
    markers = {}
    for a in eas.get_all_level_actors():
        if a is None:
            continue
        label = a.get_actor_label()
        if label.upper().startswith("SLOT_"):
            sid = label[5:]
            for key in slots:
                if key.lower() == sid.lower():
                    loc = a.get_actor_location()
                    markers[key] = (loc.x, loc.y, loc.z, a.get_actor_rotation().yaw)
                    break

    changes = []
    for s in layout["slots"]:
        if s["kind"] != "arena":
            continue
        sid = s["id"]
        new = None
        src = ""
        if sid in markers:
            x, y, z, yaw = markers[sid]
            new = (x, y, z, yaw)
            src = "marker"
        else:
            name = "Lvl_" + s["default"]
            pkg_path = "{}/{}/{}".format(ARENA_ROOT, s["default"], name)
            sl = unreal.GameplayStatics.get_streaming_level(world, pkg_path) or \
                unreal.GameplayStatics.get_streaming_level(world, name)
            if sl is None:
                warn("{}: sublevel not found - skipped".format(sid))
                continue
            t = sl.get_editor_property("level_transform")
            loc = t.translation
            new = (loc.x, loc.y, loc.z, t.rotation.rotator().yaw)
            src = "level transform"
        ox, oy, oz = s["pos"]
        oyaw = float(s.get("yaw", 0.0))
        nx, ny, nz, nyaw = new
        moved = math.hypot(nx - ox, ny - oy) > 10.0 or abs(nz - oz) > 10.0
        turned = abs(norm_yaw(nyaw - oyaw)) > 0.5
        if not (moved or turned):
            continue
        # update the route corridor points that referenced the old position
        for ptp in layout["rules"].get("route_corridor", []):
            if math.hypot(ptp[0] - ox, ptp[1] - oy) < 150.0:
                ptp[0], ptp[1] = round(nx), round(ny)
        s["pos"] = [round(nx), round(ny), round(nz)]
        s["yaw"] = round(norm_yaw(nyaw), 1)
        changes.append("{} [{}]: ({:.0f},{:.0f},{:.0f}) yaw {:.0f} -> "
                       "({:.0f},{:.0f},{:.0f}) yaw {:.1f}".format(
                           sid, src, ox, oy, oz, oyaw, nx, ny, nz, s["yaw"]))

    if not changes:
        log("No slot changes detected (nothing moved beyond 10 uu / 0.5 deg)")
        return
    layout["comment"] = layout.get("comment", "") + " [slots synced from editor]"
    with open(LAYOUT, "w", encoding="utf-8") as f:
        json.dump(layout, f, ensure_ascii=False, indent=2)
    for c in changes:
        log("SYNCED " + c)
    log("RESULT: {} slot(s) written to biome1_island_layout.json - now regenerate "
        "the heightmap locally and rebuild the island".format(len(changes)))


main()
