# Dump geometry (StaticMeshActor platforms) of EVERY BLOCKOUT_<arena> sublevel that is
# currently loaded (run inside Lvl_ArenaTestRun, which streams all biome-1 arenas).
# Output: Build/all_arena_geo.json — one row per mesh: arena, label, loc, ext(half), top, mat.
# Read-only. Run: py "<...>/dump_all_arena_geo.py"   Log tag: [GEO_DUMP]
import unreal, json, os

eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def log(m):
    unreal.log("[GEO_DUMP] {}".format(m))


rows = []
arenas = {}
for a in eas.get_all_level_actors():
    try:
        tags = [str(t) for t in a.tags]
    except Exception:
        continue
    arena = None
    for t in tags:
        if t.startswith("BLOCKOUT_") and t != "BLOCKOUT_TestRun":
            arena = t[len("BLOCKOUT_"):]
            break
    if not arena:
        continue
    cls = a.get_class().get_name()
    if cls != "StaticMeshActor":
        continue
    o, e = a.get_actor_bounds(False)
    loc = a.get_actor_location()
    rot = a.get_actor_rotation()
    mat = ""
    try:
        smc = a.static_mesh_component
        if smc:
            m = smc.get_material(0)
            mat = m.get_name() if m else ""
    except Exception:
        pass
    rows.append({
        "arena": arena,
        "label": a.get_actor_label(),
        "loc": [round(loc.x), round(loc.y), round(loc.z)],
        "ext": [round(e.x), round(e.y), round(e.z)],
        "top": round(o.z + e.z),
        "yaw": round(rot.yaw, 1),
        "mat": mat,
    })
    arenas[arena] = arenas.get(arena, 0) + 1

out = r"C:\Users\Professional\Documents\Unreal Projects\Polarity_Main5_6\Source\Tools\ArenaBlockout\Build\all_arena_geo.json"
with open(out, "w", encoding="utf-8") as f:
    json.dump(rows, f, indent=0, ensure_ascii=False)
log("dumped {} meshes across arenas: {}".format(len(rows), arenas))
