# Level metrics dumper for Polarity (companion to snap_level.py).
#
# Writes Build/<Level>_summary.json for the CURRENTLY OPEN level: actor class
# counts, bounding box of the central structure, Z plateaus (floor levels),
# modular mesh usage, and gameplay actor positions. Read-only, never saves.
#
# Usage:
#   py "<...>/Source/Tools/ArenaBlockout/dump_level_summary.py" [radius_uu]
#     radius_uu: only StaticMeshActors within this XY distance of the origin
#                count toward the structure bbox (default 10000).
#
# Log filter tag: [LEVEL_DUMP]

import json
import math
import os
import sys

import unreal

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
OUT_DIR = os.path.join(TOOLS_DIR, "Build")

GAMEPLAY_CLASSES = (
    "ArenaSpawnPoint", "PlayerStart", "NavMeshBoundsVolume", "NavLinkProxy",
    "BP_ArenaManager_C", "BP_AICombatCoordinator_C", "BP_Antenna_C",
    "BP_Plate_C", "BP_EMF_AcceleratorPlate_C", "BP_NoFlyZone_C",
    "BP_CheckpointActor_C",
)


def log(msg):
    unreal.log("[LEVEL_DUMP] {}".format(msg))


def main():
    radius = float(sys.argv[1]) if len(sys.argv) > 1 else 10000.0
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    name = world.get_name() if world else "Unknown"
    actors = [a for a in eas.get_all_level_actors() if a is not None]

    counts = {}
    for a in actors:
        cn = a.get_class().get_name()
        counts[cn] = counts.get(cn, 0) + 1

    mins = [None] * 3
    maxs = [None] * 3
    z_hist = {}
    mesh_usage = {}
    sma_count = 0
    for a in actors:
        if a.get_class().get_name() != "StaticMeshActor":
            continue
        loc = a.get_actor_location()
        if math.hypot(loc.x, loc.y) > radius:
            continue
        origin, extent = a.get_actor_bounds(False)
        if max(extent.x, extent.y, extent.z) > 200000.0:
            continue
        # Skip the giant ground plate: anything wider than 2x radius is backdrop.
        if max(extent.x, extent.y) > radius:
            continue
        sma_count += 1
        lo = (origin.x - extent.x, origin.y - extent.y, origin.z - extent.z)
        hi = (origin.x + extent.x, origin.y + extent.y, origin.z + extent.z)
        for i in range(3):
            mins[i] = lo[i] if mins[i] is None else min(mins[i], lo[i])
            maxs[i] = hi[i] if maxs[i] is None else max(maxs[i], hi[i])
        z_key = int(round(lo[2] / 50.0) * 50)
        z_hist[z_key] = z_hist.get(z_key, 0) + 1
        try:
            comp = a.static_mesh_component
            mesh = comp.static_mesh
            mesh_name = mesh.get_name() if mesh else "<none>"
        except Exception:
            mesh_name = "<error>"
        if mesh_name not in mesh_usage:
            s = a.get_actor_scale3d()
            mesh_usage[mesh_name] = {"count": 0, "first_scale": [s.x, s.y, s.z]}
        mesh_usage[mesh_name]["count"] += 1

    gameplay = []
    for a in actors:
        cn = a.get_class().get_name()
        if cn not in GAMEPLAY_CLASSES:
            continue
        loc = a.get_actor_location()
        r = a.get_actor_rotation()
        s = a.get_actor_scale3d()
        gameplay.append({
            "class": cn, "label": a.get_actor_label(),
            "pos": [round(loc.x, 1), round(loc.y, 1), round(loc.z, 1)],
            "yaw": round(r.yaw, 1),
            "scale": [round(s.x, 2), round(s.y, 2), round(s.z, 2)],
        })

    summary = {
        "level": name,
        "actor_total": len(actors),
        "class_counts": dict(sorted(counts.items(), key=lambda kv: -kv[1])),
        "structure_radius_filter": radius,
        "structure_sma_count": sma_count,
        "structure_bbox": None,
        "z_plateaus_top": None,
        "mesh_usage_top": dict(sorted(mesh_usage.items(), key=lambda kv: -kv[1]["count"])[:20]),
        "gameplay_actors": gameplay,
    }
    if sma_count:
        center = [(mins[i] + maxs[i]) * 0.5 for i in range(3)]
        extent = [(maxs[i] - mins[i]) * 0.5 for i in range(3)]
        summary["structure_bbox"] = {
            "center": [round(c, 1) for c in center],
            "extent": [round(e, 1) for e in extent],
            "min_z": round(mins[2], 1), "max_z": round(maxs[2], 1),
        }
        summary["z_plateaus_top"] = dict(
            sorted(z_hist.items(), key=lambda kv: -kv[1])[:12])

    if not os.path.isdir(OUT_DIR):
        os.makedirs(OUT_DIR)
    out_path = os.path.join(OUT_DIR, "{}_summary.json".format(name))
    with open(out_path, "w", encoding="utf-8") as f:
        json.dump(summary, f, indent=2, ensure_ascii=False)
    log("Summary written: {}".format(out_path))


main()
