# Dense nav links for A9_Temple: drop-down links around the FULL perimeter of every
# raised platform so NPCs can leave a tier from any edge (fixes melee getting stuck
# on the elevated L5 navmesh island). Spawns into the A9 sublevel.
# Run: py "<...>/dense_a9_navlinks.py"   Log tag: [A9_NAV2]
import unreal

eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
world = ues.get_editor_world()
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
TAG = "BLOCKOUT_A9_Temple"
A9PKG = "/Game/Variant_Shooter/Arenas/Biome1/A9_Temple/Lvl_A9_Temple"


def log(m):
    unreal.log("[A9_NAV2] {}".format(m))


def vec(x, y, z):
    return unreal.Vector(float(x), float(y), float(z))


def by_label(label):
    for a in eas.get_all_level_actors():
        try:
            if a.get_actor_label() == label:
                return a
        except Exception:
            pass
    return None


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
    try:
        actor.set_editor_property("smart_link_is_relevant", True)
        sm = actor.get_editor_property("smart_link_comp")
        sm.set_editor_property("link_relative_start", vec(*left))
        sm.set_editor_property("link_relative_end", vec(*right))
        sm.set_editor_property("link_direction", unreal.NavLinkDirection.BOTH_WAYS)
        sm.set_editor_property("link_enabled", True)
    except Exception as e:
        log("smart {}: {}".format(label, e))
    actor.set_actor_label(label)
    actor.set_folder_path("A9_Temple/Nav")
    tags = list(actor.tags)
    tags.append(unreal.Name(TAG))
    actor.set_editor_property("tags", tags)
    return True


# make A9 sublevel current
a9_sl = unreal.GameplayStatics.get_streaming_level(world, A9PKG) or \
    unreal.GameplayStatics.get_streaming_level(world, "Lvl_A9_Temple")
made = False
if a9_sl:
    try:
        unreal.EditorLevelUtils.make_level_current(a9_sl)
        made = True
    except Exception as e:
        log("make current: {}".format(e))
log("A9 current: {}".format(made))

# delete existing A9 navlinks (rebuild dense)
removed = 0
for a in list(eas.get_all_level_actors()):
    try:
        if a.get_class().get_name() == "NavLinkProxy" and TAG in [str(t) for t in a.tags]:
            eas.destroy_actor(a)
            removed += 1
    except Exception:
        pass
log("removed {} old navlinks".format(removed))


def edge_links(plabel, drop_to_z, step, out, edges, tagid):
    p = by_label(plabel)
    if p is None:
        log("platform {} not found".format(plabel))
        return 0
    o, e = p.get_actor_bounds(False)
    cx, cy = o.x, o.y
    hx, hy = e.x, e.y
    top = o.z + e.z
    drop = (top - drop_to_z) + 10.0
    n = 0
    made_n = 0
    if "W" in edges:
        y = cy - hy + step * 0.5
        while y < cy + hy:
            if make_navlink([cx - hx, y, top + 10], [200, 0, 0], [-out, 0, -drop],
                            "BLK_A9_Temple_NL_{}_W{}".format(tagid, n)):
                made_n += 1
            y += step
            n += 1
    if "E" in edges:
        n = 0
        y = cy - hy + step * 0.5
        while y < cy + hy:
            if make_navlink([cx + hx, y, top + 10], [-200, 0, 0], [out, 0, -drop],
                            "BLK_A9_Temple_NL_{}_E{}".format(tagid, n)):
                made_n += 1
            y += step
            n += 1
    if "S" in edges:
        n = 0
        x = cx - hx + step * 0.5
        while x < cx + hx:
            if make_navlink([x, cy - hy, top + 10], [0, 200, 0], [0, -out, -drop],
                            "BLK_A9_Temple_NL_{}_S{}".format(tagid, n)):
                made_n += 1
            x += step
            n += 1
    if "N" in edges:
        n = 0
        x = cx - hx + step * 0.5
        while x < cx + hx:
            if make_navlink([x, cy + hy, top + 10], [0, -200, 0], [0, out, -drop],
                            "BLK_A9_Temple_NL_{}_N{}".format(tagid, n)):
                made_n += 1
            x += step
            n += 1
    log("{}: {} drop-links (top {:.0f} -> z{:.0f})".format(plabel, made_n, top, drop_to_z))
    return made_n


total = 0
total += edge_links("BLK_A9_Temple_L5", 0.0, 1800.0, 820.0, "WENS", "L5")   # L5 top800 -> ground: out ~= drop so the arc is ~45deg, not vertical
total += edge_links("BLK_A9_Temple_L2_n", 0.0, 1500.0, 460.0, "WES", "L2n")  # L2 top400 -> ground
total += edge_links("BLK_A9_Temple_L4", 800.0, 1500.0, 460.0, "WE", "L4")    # L4 top1200 -> L5 (sides only; N/S are stairs)

# pavilions: crown <-> ground
for plabel, tagid in (("BLK_A9_Temple_pav_nw", "pavNW"), ("BLK_A9_Temple_pav_ne", "pavNE")):
    p = by_label(plabel)
    if p is None:
        continue
    o, e = p.get_actor_bounds(False)
    top = o.z + e.z
    if make_navlink([o.x, o.y, top + 10], [0, 0, 0], [0, -(e.y + 600), -(top + 10)],
                    "BLK_A9_Temple_NL_{}".format(tagid)):
        total += 1

log("TOTAL navlinks: {}".format(total))

# rebuild nav + save
try:
    unreal.SystemLibrary.execute_console_command(world, "RebuildNavigation")
except Exception as e:
    log("RebuildNav: {}".format(e))
les.save_current_level()
try:
    pkg = unreal.load_package(A9PKG)
    if pkg:
        unreal.EditorLoadingAndSavingUtils.save_packages([pkg], False)
except Exception as e:
    log("save pkg: {}".format(e))
log("RESULT: DONE")
