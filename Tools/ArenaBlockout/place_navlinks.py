# Universal navlink placer for a biome-1 arena (run inside Lvl_ArenaTestRun where all
# arenas are loaded as sublevels, axis-aligned, X-offset only).
#   py "<...>/place_navlinks.py" <ArenaName>
# Finds raised platforms (floor/deco/wall, wide, top>150) of BLOCKOUT_<arena> and spawns
# BOTH_WAYS smart navlinks (drop-downs to ground) around each platform's perimeter, into
# the arena's OWN sublevel package. Idempotent: removes that arena's old navlinks first.
# Does NOT touch geometry. Criterion: reference_navlink_placement.md. Log tag: [NAVPLACE]
import unreal, sys

eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
world = ues.get_editor_world()
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)

ARENA = None
for a in sys.argv[1:]:
    if a and not a.startswith("-"):
        ARENA = a
        break
if not ARENA:
    raise RuntimeError("Usage: place_navlinks.py <ArenaName>")
TAG = "BLOCKOUT_" + ARENA
PKG = "/Game/Variant_Shooter/Arenas/Biome1/{}/Lvl_{}".format(ARENA, ARENA)

STEP = 1700.0       # spacing of drop links along an edge
OUT_MIN, OUT_MAX = 350.0, 950.0
MIN_HALF = 200.0    # ignore thin parapets/walls (>=200 half = >=400 wide = stand-able balcony/ledge)
MIN_TOP = 150.0     # only raised platforms


def log(m):
    unreal.log("[NAVPLACE] {}".format(m))


def vec(x, y, z):
    return unreal.Vector(float(x), float(y), float(z))


def make_navlink(pos, left, right, label):
    actor = eas.spawn_actor_from_class(unreal.NavLinkProxy, vec(*pos))
    if actor is None:
        return False
    link = unreal.NavigationLink()
    link.set_editor_property("left", vec(*left))
    link.set_editor_property("right", vec(*right))
    try:
        link.set_editor_property("direction", unreal.NavLinkDirection.BOTH_WAYS)
        link.set_editor_property("snap_radius", 350.0)
    except Exception:
        pass
    actor.set_editor_property("point_links", [link])
    # PointLinks ONLY — no SmartLinkComp. One editable set of Left/Right handles, so the
    # author can drag them by hand and have it actually take effect (no ghost second set).
    # NPCs jump on PointLinks fine: SetMoveSegment triggers on NodeFlags.IsNavLink(), and
    # NAV_DEBUG confirms jumps fired with CustomLinkId=none. Smart link is what duplicated.
    try:
        actor.set_editor_property("smart_link_is_relevant", False)
    except Exception:
        pass
    actor.set_actor_label(label)
    actor.set_folder_path("{}/Nav".format(ARENA))
    tags = list(actor.tags)
    tags.append(unreal.Name(TAG))
    actor.set_editor_property("tags", tags)
    return True


# --- make the arena sublevel current so navlinks land in its package ---
sl = unreal.GameplayStatics.get_streaming_level(world, PKG) or \
    unreal.GameplayStatics.get_streaming_level(world, "Lvl_" + ARENA)
made = False
if sl:
    try:
        unreal.EditorLevelUtils.make_level_current(sl)
        made = True
    except Exception as e:
        log("make_level_current: {}".format(e))
log("arena={} sublevel current={}".format(ARENA, made))
if not made:
    raise RuntimeError("Arena sublevel not loaded/current: " + PKG)

# --- remove this arena's existing navlinks ---
removed = 0
for a in list(eas.get_all_level_actors()):
    try:
        if a.get_class().get_name() == "NavLinkProxy" and TAG in [str(t) for t in a.tags]:
            eas.destroy_actor(a)
            removed += 1
    except Exception:
        pass
log("removed {} old navlinks".format(removed))

# --- collect ALL meshes (for landing checks), the ground deck, and raised platforms ---
MARGIN = 40.0        # AABB containment slack
BLOCK_MARGIN = 60.0  # a mesh within this of our top counts as a wall (can't drop there)
PROBE = 300.0        # how far outward to probe for the floor under a drop
MIN_DROP = 80.0      # ignore edges with no real height difference

all_meshes = []      # (label, minx, maxx, miny, maxy, top)
ground_top = None
ground_bb = None     # [minx, maxx, miny, maxy] union of ground/deck pieces
platforms = []
for a in eas.get_all_level_actors():
    try:
        if TAG not in [str(t) for t in a.tags] or a.get_class().get_name() != "StaticMeshActor":
            continue
        o, e = a.get_actor_bounds(False)
        top = o.z + e.z
        lbl = a.get_actor_label()
        bb = (o.x - e.x, o.x + e.x, o.y - e.y, o.y + e.y)
        all_meshes.append((lbl, bb[0], bb[1], bb[2], bb[3], top))
        mat = ""
        smc = a.static_mesh_component
        if smc:
            m = smc.get_material(0)
            mat = (m.get_name() if m else "").lower()
        is_surface = ("floor" in mat or "deco" in mat or "wall" in mat)
        wide = min(e.x, e.y) >= MIN_HALF
        if top <= MIN_TOP:                       # ground / deck piece
            if is_surface and wide:
                if ground_top is None or top < ground_top:
                    ground_top = top
                if ground_bb is None:
                    ground_bb = list(bb)
                else:
                    ground_bb[0] = min(ground_bb[0], bb[0]); ground_bb[1] = max(ground_bb[1], bb[1])
                    ground_bb[2] = min(ground_bb[2], bb[2]); ground_bb[3] = max(ground_bb[3], bb[3])
            continue
        if is_surface and wide:
            platforms.append((lbl, o.x, o.y, e.x, e.y, top))
    except Exception:
        pass
if ground_top is None:
    ground_top = 0.0
log("ground_top={:.0f}, raised platforms={}, all meshes={}".format(ground_top, len(platforms), len(all_meshes)))


def floor_under(px, py, self_label, plat_top):
    """Highest walkable surface strictly below plat_top at (px,py).
    Returns (floor_z, blocked): blocked=True if a wall as tall as us sits there."""
    best = None
    for (lbl, mnx, mxx, mny, mxy, mtop) in all_meshes:
        if lbl == self_label:
            continue
        if px < mnx - MARGIN or px > mxx + MARGIN or py < mny - MARGIN or py > mxy + MARGIN:
            continue
        if mtop >= plat_top - BLOCK_MARGIN:
            return (None, True)                  # wall in the way -> no drop here
        if best is None or mtop > best:
            best = mtop
    if ground_bb and ground_bb[0] - MARGIN <= px <= ground_bb[1] + MARGIN \
            and ground_bb[2] - MARGIN <= py <= ground_bb[3] + MARGIN:
        if best is None or ground_top > best:
            best = ground_top
    return (best, False)


# --- spawn perimeter drop-links per platform (only where landing is walkable) ---
total = 0
for (lbl, cx, cy, hx, hy, top) in platforms:
    sid = lbl.replace("BLK_{}_".format(ARENA), "")
    n = 0
    kept = 0

    def place(ox, oy, dirx, diry):
        """ox,oy = link origin on the platform's top edge; dir = outward unit vector."""
        global total, n, kept
        n += 1
        floor_z, blocked = floor_under(ox + dirx * PROBE, oy + diry * PROBE, lbl, top)
        if blocked or floor_z is None:
            return                               # wall, or void -> skip this edge
        drop = top - floor_z
        if drop < MIN_DROP:
            return                               # same height as neighbour -> skip
        out = max(OUT_MIN, min(OUT_MAX, drop))
        if make_navlink([ox, oy, top + 10], [-dirx * 200.0, -diry * 200.0, 0],
                        [dirx * out, diry * out, -drop],
                        "BLK_{}_NL_{}_{}".format(ARENA, sid, n)):
            total += 1
            kept += 1

    # West / East edges: iterate over Y
    y = cy - hy + STEP * 0.5
    while y < cy + hy:
        place(cx - hx, y, -1.0, 0.0)             # west: drop to -X
        place(cx + hx, y, 1.0, 0.0)              # east: drop to +X
        y += STEP
    # South / North edges: iterate over X
    x = cx - hx + STEP * 0.5
    while x < cx + hx:
        place(x, cy - hy, 0.0, -1.0)             # south: drop to -Y
        place(x, cy + hy, 0.0, 1.0)              # north: drop to +Y
        x += STEP
    log("  {} (top {:.0f}) -> {} links of {} edges".format(sid, top, kept, n))

log("TOTAL navlinks for {}: {}".format(ARENA, total))

# --- rebuild nav + save sublevel ---
try:
    unreal.SystemLibrary.execute_console_command(world, "RebuildNavigation")
except Exception as e:
    log("RebuildNav: {}".format(e))
les.save_current_level()
try:
    pkg = unreal.load_package(PKG)
    if pkg:
        unreal.EditorLoadingAndSavingUtils.save_packages([pkg], False)
except Exception as e:
    log("save pkg: {}".format(e))
log("RESULT: DONE ({})".format(ARENA))
