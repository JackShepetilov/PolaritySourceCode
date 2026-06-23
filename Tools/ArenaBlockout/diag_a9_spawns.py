# Diagnose + fix A9_Temple spawns and report nav state. Run: py "<...>/diag_a9_spawns.py"
#  - each A9 ArenaSpawnPoint: line-trace down to find the floor; snap onto it (+15),
#    or report NO FLOOR (spawn is off the ground / inside a mesh).
#  - dump all A9 navlinks (MCP get_level_actors is blind to them).
#  - probe navmesh on pavilion crowns + L5 edges to see why melee get stuck.
# Log tag: [A9_DIAG]
import unreal

eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
world = ues.get_editor_world()
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
TAG = "BLOCKOUT_A9_Temple"


def log(m):
    unreal.log("[A9_DIAG] {}".format(m))


def trace_floor(loc):
    start = unreal.Vector(loc.x, loc.y, loc.z + 1200)
    end = unreal.Vector(loc.x, loc.y, loc.z - 4000)
    hit = unreal.SystemLibrary.line_trace_single(
        world, start, end, unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, False, [],
        unreal.DrawDebugTrace.NONE, True)
    if hit and hit.blocking_hit:
        return hit.impact_point.z, hit.get_editor_property("hit_actor")
    return None, None


nav = unreal.NavigationSystemV1.get_navigation_system(world)


def has_navmesh(x, y, z, extent=80.0):
    try:
        res = unreal.NavigationSystemV1.project_point_to_navigation(
            world, unreal.Vector(x, y, z), nav_data=None,
            project_extent=unreal.Vector(extent, extent, 400.0))
        # returns projected location; treat near-z as valid
        return res
    except Exception as e:
        return None


# --- spawns ---
spawn_fixed = 0
spawn_bad = 0
for a in eas.get_all_level_actors():
    try:
        if TAG not in [str(t) for t in a.tags]:
            continue
        if a.get_class().get_name() != "ArenaSpawnPoint":
            continue
        loc = a.get_actor_location()
        fz, hitactor = trace_floor(loc)
        label = a.get_actor_label()
        if fz is None:
            log("BAD SPAWN {} at ({:.0f},{:.0f},{:.0f}) - NO FLOOR below".format(
                label, loc.x, loc.y, loc.z))
            spawn_bad += 1
            continue
        dz = (fz + 15.0) - loc.z
        hitname = hitactor.get_actor_label() if hitactor else "?"
        if abs(dz) > 5.0:
            a.set_actor_location(unreal.Vector(loc.x, loc.y, fz + 15.0), False, False)
            log("snapped {} dz={:+.0f} -> z={:.0f} (floor='{}')".format(label, dz, fz + 15.0, hitname))
            spawn_fixed += 1
        else:
            log("ok {} z={:.0f} (floor='{}')".format(label, loc.z, hitname))
    except Exception as e:
        log("spawn err: {}".format(e))
log("SPAWNS: {} snapped, {} bad (no floor)".format(spawn_fixed, spawn_bad))

# --- navlinks dump ---
nl = 0
for a in eas.get_all_level_actors():
    try:
        if a.get_class().get_name() == "NavLinkProxy" and TAG in [str(t) for t in a.tags]:
            loc = a.get_actor_location()
            log("navlink {} at ({:.0f},{:.0f},{:.0f})".format(a.get_actor_label(), loc.x, loc.y, loc.z))
            nl += 1
    except Exception:
        pass
log("navlinks: {}".format(nl))

# --- navmesh probes on pavilions (top ~500) and L5 edges ---
for lbl in ("BLK_A9_Temple_pav_nw", "BLK_A9_Temple_pav_ne", "BLK_A9_Temple_L5"):
    p = None
    for a in eas.get_all_level_actors():
        if a.get_actor_label() == lbl:
            p = a
            break
    if p is None:
        log("probe: {} not found".format(lbl))
        continue
    o, e = p.get_actor_bounds(False)
    top = o.z + e.z
    res = has_navmesh(o.x, o.y, top + 30)
    log("navmesh on '{}' crown (z~{:.0f}): {}".format(lbl, top, "YES" if res else "no"))

les.save_current_level()
log("RESULT: DONE")
