# Surgical fix for A9_Temple in Lvl_ArenaTestRun: re-place exit-blocker fields to
# the CURRENT ground perimeter and (re)create nav links ONLY where the author wants:
#   - drop-downs from L3/L5 top (Z~800) to L1 ground, on the SIDE edges (away from
#     the central stairs/ramps)
#   - on the "black platforms" (pavilions pav_nw / pav_ne)
# Does NOT clear or rebuild anything else - the author's manual layout (L5, perimeter
# walls, centering, raised turret) is untouched. Run: py "<...>/fix_a9_nav.py"
# Log tag: [A9_NAV]

import unreal

eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
TAG = "BLOCKOUT_A9_Temple"


def log(m):
    unreal.log("[A9_NAV] {}".format(m))


def by_label(label):
    for a in eas.get_all_level_actors():
        try:
            if a.get_actor_label() == label:
                return a
        except Exception:
            pass
    return None


def vec(x, y, z):
    return unreal.Vector(float(x), float(y), float(z))


def make_navlink(pos, left, right, label):
    actor = eas.spawn_actor_from_class(unreal.NavLinkProxy, vec(*pos))
    if actor is None:
        log("FAILED spawn navlink {}".format(label))
        return None
    link = unreal.NavigationLink()
    link.set_editor_property("left", vec(*left))
    link.set_editor_property("right", vec(*right))
    try:
        link.set_editor_property("direction", unreal.NavLinkDirection.BOTH_WAYS)
        link.set_editor_property("snap_radius", 200.0)
    except Exception as e:
        log("link props: {}".format(e))
    actor.set_editor_property("point_links", [link])
    try:
        actor.set_editor_property("smart_link_is_relevant", True)
        smart = actor.get_editor_property("smart_link_comp")
        smart.set_editor_property("link_relative_start", vec(*left))
        smart.set_editor_property("link_relative_end", vec(*right))
        smart.set_editor_property("link_direction", unreal.NavLinkDirection.BOTH_WAYS)
        smart.set_editor_property("link_enabled", True)
    except Exception as e:
        log("smart link {}: {}".format(label, e))
    actor.set_actor_label(label)
    tags = list(actor.tags)
    tags.append(unreal.Name(TAG))
    actor.set_editor_property("tags", tags)
    return actor


# --- 1. remove any existing A9 navlinks (so we start clean) ---
removed = 0
for a in list(eas.get_all_level_actors()):
    try:
        if a.get_class().get_name() == "NavLinkProxy" and TAG in [str(t) for t in a.tags]:
            eas.destroy_actor(a)
            removed += 1
    except Exception:
        pass
log("Removed {} old navlinks".format(removed))

# --- 2. re-place exit-blocker fields onto the CURRENT ground perimeter ---
g = by_label("BLK_A9_Temple_ground")
if g is None:
    log("ground not found - aborting field re-place")
else:
    go, ge = g.get_actor_bounds(False)
    cx, cy = go.x, go.y
    hx, hy = ge.x, ge.y
    H = 1800.0
    sz_h = (hx * 2.0) / 100.0 + 0.4   # field length along X
    sz_v = (hy * 2.0) / 100.0 + 0.4   # field length along Y
    field_cfg = {
        "BLK_A9_Temple_fld_n": (vec(cx, cy - hy, H), unreal.Vector(sz_h, 0.2, 36.0)),
        "BLK_A9_Temple_fld_s": (vec(cx, cy + hy, H), unreal.Vector(sz_h, 0.2, 36.0)),
        "BLK_A9_Temple_fld_w": (vec(cx - hx, cy, H), unreal.Vector(0.2, sz_v, 36.0)),
        "BLK_A9_Temple_fld_e": (vec(cx + hx, cy, H), unreal.Vector(0.2, sz_v, 36.0)),
    }
    moved = 0
    for label, (loc, scale) in field_cfg.items():
        f = by_label(label)
        if f is None:
            log("field {} not found".format(label))
            continue
        f.set_actor_location(loc, False, False)
        f.set_actor_scale3d(scale)
        moved += 1
    log("Re-placed {} fields to ground perimeter cx={:.0f} cy={:.0f} hx={:.0f} hy={:.0f}".format(
        moved, cx, cy, hx, hy))

# --- 3. spawn nav links: drop-downs on side edges + pavilions ---
# A9 sits at world X offset +84000 in TestRun. Use the live ground bounds for the
# side X edges so the drops land just past the elevated L5/L3 platform.
OX = g.get_actor_location().x if g else 84000.0   # 84000
# L5 platform top is Z~800; its side faces are near X = OX +/- 3500 (L5 half-size 3500).
west_x = OX - 3500.0
east_x = OX + 3500.0
drops = [
    ([west_x, 1500, 810], [200, 0, 0], [-750, 0, -810], "BLK_A9_Temple_NL_drop_w1"),
    ([west_x, 5600, 810], [200, 0, 0], [-750, 0, -810], "BLK_A9_Temple_NL_drop_w2"),
    ([east_x, 1500, 810], [-200, 0, 0], [750, 0, -810], "BLK_A9_Temple_NL_drop_e1"),
    ([east_x, 5600, 810], [-200, 0, 0], [750, 0, -810], "BLK_A9_Temple_NL_drop_e2"),
]
pav_links = []
for plabel, drop_dir in (("BLK_A9_Temple_pav_nw", -900), ("BLK_A9_Temple_pav_ne", -900)):
    p = by_label(plabel)
    if p is None:
        log("pavilion {} not found".format(plabel))
        continue
    po, pe = p.get_actor_bounds(False)
    top_z = po.z + pe.z
    pav_links.append(([po.x, po.y, top_z + 10], [0, 0, 0], [0, drop_dir, -(top_z + 10)],
                      plabel.replace("pav_", "NL_pav_")))

made = 0
for pos, left, right, label in drops + pav_links:
    if make_navlink(pos, left, right, label):
        made += 1
log("Spawned {} navlinks (4 side-drops + pavilions)".format(made))

# --- 4. rebuild navigation + save ---
try:
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    unreal.SystemLibrary.execute_console_command(world, "RebuildNavigation")
    log("RebuildNavigation issued")
except Exception as e:
    log("RebuildNavigation failed: {}".format(e))

les.save_current_level()  # persistent TestRun (navlinks live here)
try:
    a9pkg = unreal.load_package("/Game/Variant_Shooter/Arenas/Biome1/A9_Temple/Lvl_A9_Temple")
    if a9pkg:
        unreal.EditorLoadingAndSavingUtils.save_packages([a9pkg], False)  # A9 sublevel (moved fields)
        log("Saved A9 sublevel package (fields)")
except Exception as e:
    log("save A9 pkg: {}".format(e))
log("RESULT: DONE")
