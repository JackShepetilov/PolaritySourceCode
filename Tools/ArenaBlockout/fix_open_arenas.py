# Place PointLinks-only navlinks on whatever biome-1 arenas are ALREADY LOADED in the
# current editor world. Does NOT load levels and does NOT save (so it never touches the
# author's unsaved work on disk) -- it's a live, hand-draggable, reversible preview.
# Same platform/floor logic as place_navlinks.py. Run: py "<...>/fix_open_arenas.py"
# Log tag: [NAVFIX]
import unreal

ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
world = ues.get_editor_world()

STEP = 1700.0
OUT_MIN, OUT_MAX = 350.0, 950.0
MIN_HALF = 200.0
MIN_TOP = 150.0
MARGIN = 40.0
BLOCK_MARGIN = 60.0
PROBE = 300.0
MIN_DROP = 80.0


def log(m):
    unreal.log("[NAVFIX] {}".format(m))


def vec(x, y, z):
    return unreal.Vector(float(x), float(y), float(z))


def make_navlink(pos, left, right, label, tag):
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
    try:
        actor.set_editor_property("smart_link_is_relevant", False)  # PointLinks only
    except Exception:
        pass
    actor.set_actor_label(label)
    tags = list(actor.tags)
    tags.append(unreal.Name(tag))
    actor.set_editor_property("tags", tags)
    return True


# --- discover which arenas are loaded right now ---
loaded = {}
for a in eas.get_all_level_actors():
    try:
        tags = [str(t) for t in a.tags]
    except Exception:
        continue
    for t in tags:
        if t.startswith("BLOCKOUT_") and t != "BLOCKOUT_TestRun":
            loaded.setdefault(t[len("BLOCKOUT_"):], True)
log("loaded arenas in current world: {}".format(sorted(loaded)))

grand = 0
for ARENA in sorted(loaded):
    TAG = "BLOCKOUT_" + ARENA

    # remove this arena's old navlinks (in current world only)
    removed = 0
    for a in list(eas.get_all_level_actors()):
        try:
            if a.get_class().get_name() == "NavLinkProxy" and TAG in [str(t) for t in a.tags]:
                eas.destroy_actor(a)
                removed += 1
        except Exception:
            pass

    all_m = []
    ground_top = None
    ground_bb = None
    platforms = []
    for a in eas.get_all_level_actors():
        try:
            if TAG not in [str(t) for t in a.tags] or a.get_class().get_name() != "StaticMeshActor":
                continue
            o, e = a.get_actor_bounds(False)
            top = o.z + e.z
            lbl = a.get_actor_label()
            bb = (o.x - e.x, o.x + e.x, o.y - e.y, o.y + e.y)
            all_m.append((lbl, bb[0], bb[1], bb[2], bb[3], top))
            mat = ""
            smc = a.static_mesh_component
            if smc:
                m = smc.get_material(0)
                mat = (m.get_name() if m else "").lower()
            is_surface = ("floor" in mat or "deco" in mat or "wall" in mat)
            wide = min(e.x, e.y) >= MIN_HALF
            if top <= MIN_TOP:
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

    def floor_under(px, py, self_label, plat_top):
        best = None
        for (lbl, mnx, mxx, mny, mxy, mtop) in all_m:
            if lbl == self_label:
                continue
            if px < mnx - MARGIN or px > mxx + MARGIN or py < mny - MARGIN or py > mxy + MARGIN:
                continue
            if mtop >= plat_top - BLOCK_MARGIN:
                return (None, True)
            if best is None or mtop > best:
                best = mtop
        if ground_bb and ground_bb[0] - MARGIN <= px <= ground_bb[1] + MARGIN \
                and ground_bb[2] - MARGIN <= py <= ground_bb[3] + MARGIN:
            if best is None or ground_top > best:
                best = ground_top
        return (best, False)

    total = 0
    for (lbl, cx, cy, hx, hy, top) in platforms:
        sid = lbl.replace("BLK_{}_".format(ARENA), "")
        n = [0]

        def place(ox, oy, dx, dy):
            fz, blocked = floor_under(ox + dx * PROBE, oy + dy * PROBE, lbl, top)
            if blocked or fz is None:
                return
            drop = top - fz
            if drop < MIN_DROP:
                return
            out = max(OUT_MIN, min(OUT_MAX, drop))
            n[0] += 1
            if make_navlink([ox, oy, top + 10], [-dx * 200.0, -dy * 200.0, 0],
                            [dx * out, dy * out, -drop],
                            "BLK_{}_NL_{}_{}".format(ARENA, sid, n[0]), TAG):
                return True

        cnt0 = total
        y = cy - hy + STEP * 0.5
        while y < cy + hy:
            if place(cx - hx, y, -1.0, 0.0):
                total += 1
            if place(cx + hx, y, 1.0, 0.0):
                total += 1
            y += STEP
        x = cx - hx + STEP * 0.5
        while x < cx + hx:
            if place(x, cy - hy, 0.0, -1.0):
                total += 1
            if place(x, cy + hy, 0.0, 1.0):
                total += 1
            x += STEP
        log("  {}: {} top{:.0f} -> {} links".format(ARENA, sid, top, total - cnt0))

    grand += total
    log("{}: removed {} old, placed {} PointLinks-only navlinks (NOT saved)".format(
        ARENA, removed, total))

try:
    unreal.SystemLibrary.execute_console_command(world, "RebuildNavigation")
except Exception as e:
    log("RebuildNav: {}".format(e))
log("DONE. total placed = {} (live preview, nothing saved to disk)".format(grand))
