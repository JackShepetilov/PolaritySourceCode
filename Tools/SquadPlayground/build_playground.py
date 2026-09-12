# Squad Playground generator for Polarity.
#
# Builds a blockout level purpose-made for faction-war testing with the squad spawn
# system (USquadLoadout / ASquadSpawnPoint / USquadScenario):
#
#                    [POI1]
#                   .      .
#             lane .          . lane      three walled compounds on an equilateral
#                .              .         triangle, 150 m on a side, one lane of
#           [POI2] ---------- [POI3]      scattered cover per pair
#                    lane
#
# Each POI is a walled compound whose gates face its two neighbours, with an interior mix of
# full/half cover. The POIs sit outside mutual sightline distance; the lanes carry sparse cover so
# a crossing attack has to commit.
#
# COVER REQUIREMENTS (proposal, tune here not in code elsewhere):
#   per POI : >= 6 full-height pieces (>= 200uu, blocks AI sight) +
#             >= 8 half-height pieces (~90-110uu, crouch/dash cover),
#             spacing 350..700uu between piece centers, two approach gates,
#             interior free of overlaps by construction (jittered ring+grid).
#   lanes   : >= 6 half/full pieces per pair, alternating sides of the straight line.
#
# Usage (editor running, this level may be reopened/rebuilt):
#   py "<project>/Source/Tools/SquadPlayground/build_playground.py" [seed]
#
# Log filter tag: [PLAYGROUND]

import math
import os
import random
import sys

import unreal

LEVEL_PATH = "/Game/SquadPlayground/L_SquadPlayground"
TAG = "SquadPlayground"
CUBE_MESH = "/Engine/BasicShapes/Cube.Cube"

GROUND = dict(size=(36000, 30000, 100), z=-50)

# Three POIs on an equilateral triangle, 150 m on a side. A line gave the middle position two
# neighbours in opposite directions and no flanks; a triangle gives every position two approaches
# that are not each other, which is the smallest shape where holding ground means anything.
POI_SIDE = 15000                          # 150 m between neighbours
POI_RADIUS = POI_SIDE / (3.0 ** 0.5)      # circumradius: distance from the centre to each POI
POI_WALL_RADIUS = 2600       # compound wall ring radius
WALL_HEIGHT = 300            # full sight-blocker
FULL_COVER_H = 220           # full-height cover inside compounds
HALF_COVER_H = 100           # crouch/dash cover
COVER_MIN_SPACING = 350.0

def poi_positions():
    """POI1 north, POI2 south-west, POI3 south-east. Order fixed so scenario tags keep meaning
    the same place between rebuilds."""
    out = []
    for name, deg in (("POI1", 90.0), ("POI2", 210.0), ("POI3", 330.0)):
        ang = math.radians(deg)
        out.append((name, POI_RADIUS * math.cos(ang), POI_RADIUS * math.sin(ang)))
    return out


PERIMETER_SEGMENTS = 16      # wall segments around each POI
GATE_HALF_WIDTH_DEG = 22.0

FULL_COVERS_PER_POI = 6
HALF_COVERS_PER_POI = 8
CORRIDOR_COVERS = 6


def log(msg):
    unreal.log("[PLAYGROUND] {}".format(msg))


def warn(msg):
    unreal.log_warning("[PLAYGROUND] {}".format(msg))


def vec(x, y, z):
    return unreal.Vector(x, y, z)


def level_disk_path(path):
    root = unreal.Paths.project_content_dir()
    rel = path.replace("/Game/", "", 1)
    return os.path.join(root, rel + ".umap")


def open_or_create_level(les):
    # Same unsaved-work guard as ArenaBlockout/build_arena.py: a scripted load silently
    # discards dirty maps. Refuse loudly instead of eating the author's work.
    dirty = unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()
    foreign = [p for p in dirty if p.get_name() != LEVEL_PATH]
    if foreign:
        names = ", ".join(p.get_name() for p in foreign)
        raise RuntimeError(
            "UNSAVED map changes ({}) - save everything in the editor first".format(names))

    try:
        unreal.AssetRegistryHelpers.get_asset_registry().wait_for_completion()
    except Exception:
        pass

    # Already standing in a freshly made (never saved) playground: creating it a second time
    # asserts the editor dead with "World Memory Leaks", because the world being replaced is the
    # one this call is about to build. Happens whenever a previous run died before saving.
    current = les.get_current_level()
    current_name = current.get_outer().get_name() if current else ""
    if current_name.endswith(LEVEL_PATH.rsplit("/", 1)[-1]):
        log("Already in {} - reusing the open world".format(current_name))
        return

    if os.path.isfile(level_disk_path(LEVEL_PATH)) or unreal.EditorAssetLibrary.does_asset_exist(LEVEL_PATH):
        if not les.load_level(LEVEL_PATH):
            raise RuntimeError("Failed to load " + LEVEL_PATH)
        log("Loaded existing level {}".format(LEVEL_PATH))
    else:
        if not les.new_level(LEVEL_PATH):
            raise RuntimeError("Failed to create " + LEVEL_PATH)
        log("Created new level {}".format(LEVEL_PATH))


def clear_tagged(eas):
    removed = 0
    for actor in list(eas.get_all_level_actors()):
        try:
            if TAG in [str(t) for t in actor.tags]:
                eas.destroy_actor(actor)
                removed += 1
        except Exception:
            pass
    log("Removed {} previously generated actors".format(removed))


def finish(actor, label, folder):
    actor.set_actor_label(label)
    actor.set_folder_path(folder)
    tags = list(actor.tags)
    tags.append(unreal.Name(TAG))
    actor.set_editor_property("tags", tags)
    return actor


class Builder(object):
    def __init__(self, eas, seed):
        self.eas = eas
        self.rng = random.Random(seed)
        self.placed = []  # [(x, y)] centers already occupied, for spacing checks

    def box(self, cx, cy, cz, sx, sy, sz, label, folder, yaw_deg=0.0):
        mesh = unreal.EditorAssetLibrary.load_asset(CUBE_MESH)
        if not mesh:
            raise RuntimeError("Cube mesh missing")
        actor = self.eas.spawn_actor_from_class(unreal.StaticMeshActor, vec(cx, cy, cz))
        comp = actor.static_mesh_component
        comp.set_static_mesh(mesh)
        actor.set_actor_scale3d(vec(sx / 100.0, sy / 100.0, sz / 100.0))
        if yaw_deg != 0.0:
            # Rotator() is positional (roll, pitch, yaw): passing yaw third silently builds a
            # pitched wall. Name the argument, and set_actor_rotation wants teleport_physics.
            actor.set_actor_rotation(unreal.Rotator(roll=0.0, pitch=0.0, yaw=yaw_deg), False)
        return finish(actor, label, folder)

    def far_enough(self, x, y, min_spacing=COVER_MIN_SPACING):
        for px, py in self.placed:
            if math.hypot(x - px, y - py) < min_spacing:
                return False
        return True

    def claim(self, x, y):
        self.placed.append((x, y))

    def ground(self):
        self.box(0, 0, GROUND["z"], GROUND["size"][0], GROUND["size"][1], GROUND["size"][2],
                 "BLK_PG_Ground", "Ground")

    def poi(self, cx, cy, side_name):
        """One walled compound with gates and an interior cover mix.

        Gates face the other two POIs. On a line two fixed openings east and west were the same
        thing; on a triangle a compound whose gates ignore its neighbours is a compound nobody can
        attack except through a wall."""
        folder = "POI_{}".format(side_name)

        gates = []
        for _, ox, oy in poi_positions():
            if abs(ox - cx) < 1.0 and abs(oy - cy) < 1.0:
                continue
            gates.append(math.degrees(math.atan2(oy - cy, ox - cx)) % 360.0)

        # Perimeter: full-height wall segments on a ring, gates left open
        seg_len = 2.0 * math.pi * POI_WALL_RADIUS / PERIMETER_SEGMENTS * 1.15  # overlap corners
        for i in range(PERIMETER_SEGMENTS):
            ang = math.radians(i * 360.0 / PERIMETER_SEGMENTS)
            deg = math.degrees(ang)
            in_gate = any(abs((deg - g + 180.0) % 360.0 - 180.0) < GATE_HALF_WIDTH_DEG
                          for g in gates)
            if in_gate:
                continue
            wx = cx + math.cos(ang) * POI_WALL_RADIUS
            wy = cy + math.sin(ang) * POI_WALL_RADIUS
            yaw = -(i * 360.0 / PERIMETER_SEGMENTS)
            self.box(wx, wy, WALL_HEIGHT / 2.0, seg_len, 80.0, WALL_HEIGHT,
                     "BLK_PG_{}_Wall_{:02d}".format(side_name, i), folder, yaw)

        # Interior covers: full-height first (sight blockers), then half-height fill,
        # golden-angle scatter with spacing rejection - no overlaps by construction.
        made_full = 0
        made_half = 0
        attempt = 0
        while made_full + made_half < FULL_COVERS_PER_POI + HALF_COVERS_PER_POI and attempt < 400:
            attempt += 1
            idx = made_full + made_half
            ang = idx * math.radians(137.507) + self.rng.uniform(-0.1, 0.1)
            radius = 500.0 + 1700.0 * math.sqrt(self.rng.random())
            x = cx + math.cos(ang) * radius
            y = cy + math.sin(ang) * radius
            if not self.far_enough(x, y):
                continue
            # Front-load the full-height quota, then fill with half cover
            want_full = made_full < FULL_COVERS_PER_POI and (
                made_full <= made_half or idx % 3 == 0)
            h = FULL_COVER_H if want_full else HALF_COVER_H
            w = self.rng.choice((280.0, 340.0, 420.0))
            d = self.rng.choice((120.0, 160.0))
            yaw = self.rng.uniform(0.0, 360.0)
            kind = "Full" if want_full else "Half"
            self.box(x, y, h / 2.0, w, d, h,
                     "BLK_PG_{}_{}_{}".format(side_name, kind, idx), folder, yaw)
            self.claim(x, y)
            if want_full:
                made_full += 1
            else:
                made_half += 1

    def corridor(self):
        """Sparse cover along each of the three crossings, so attacking any of them costs something.

        One lane per pair of POIs. Cover is scattered off the straight line rather than on it: a
        neat row reads as a fence and gets used as one, while an offset scatter makes an approach a
        series of choices."""
        points = poi_positions()
        lane = 0
        for i in range(len(points)):
            for j in range(i + 1, len(points)):
                _, ax, ay = points[i]
                _, bx, by = points[j]

                dx, dy = bx - ax, by - ay
                length = math.hypot(dx, dy)
                ux, uy = dx / length, dy / length
                px, py = -uy, ux  # across the lane

                # Only the open middle: the ends are inside the compounds' own cover.
                usable = length - 2.0 * POI_WALL_RADIUS
                step = usable / (CORRIDOR_COVERS + 1)

                for k in range(CORRIDOR_COVERS):
                    along = POI_WALL_RADIUS + step * (k + 1)
                    side = 1 if k % 2 == 0 else -1
                    across = side * self.rng.uniform(700.0, 2200.0)

                    x = ax + ux * along + px * across
                    y = ay + uy * along + py * across
                    if not self.far_enough(x, y):
                        continue

                    full = (k % 3 == 1)
                    h = FULL_COVER_H if full else HALF_COVER_H
                    w = self.rng.choice((320.0, 420.0))
                    self.box(x, y, h / 2.0, w, 140.0, h,
                             "BLK_PG_Lane{}_{}".format(lane, k), "Lanes", self.rng.uniform(0.0, 360.0))
                    self.claim(x, y)
                lane += 1


def gameplay_actors(b, eas):
    # Through finish(), like everything else here: an untagged actor is invisible to clear_tagged
    # and quietly multiplies on every rerun.
    # Outside the triangle, behind the southern edge: close enough to walk in, far enough that the
    # player is not standing in somebody's objective at spawn.
    start = eas.spawn_actor_from_class(
        unreal.PlayerStart, vec(0.0, -(POI_RADIUS + POI_WALL_RADIUS + 2000.0), 150.0))
    finish(start, "PG_PlayerStart", "Gameplay")

    # Squad spawn points at the POI centres; loadouts stay None - create DA_Squad* assets
    # and assign them in the editor (or reference them from scenarios only).
    # One spawn point per corner of the triangle. Equidistant on purpose: no position is naturally
    # the middle one, so which POI matters is decided by the scenario rather than by geometry.
    for tag, x, y in poi_positions():
        point = eas.spawn_actor_from_class(unreal.SquadSpawnPoint, vec(x, y, 120.0))
        point.set_editor_property("point_tag", unreal.Name(tag))
        point.set_editor_property("spawn_radius", 600.0)
        # Python drops the leading b of a bool UPROPERTY: bSpawnOnBeginPlay -> spawn_on_begin_play
        point.set_editor_property("spawn_on_begin_play", False)
        finish(point, "PG_SpawnPoint_{}".format(tag), "Squad")

    # Navigation over the whole playable strip
    nav_bounds = eas.spawn_actor_from_class(unreal.NavMeshBoundsVolume, vec(0, 0, GROUND["z"] + 1000.0))
    nav_bounds.set_actor_scale3d(vec(GROUND["size"][0] / 200.0, GROUND["size"][1] / 200.0, 20.0))
    finish(nav_bounds, "BLK_PG_NavBounds", "Nav")
    log("NavMeshBoundsVolume spawned - REBUILD navigation after opening the level")

    nav = eas.spawn_actor_from_class(unreal.RecastNavMesh, vec(0, 0, 0))
    finish(nav, "BLK_PG_RecastNavMesh", "Nav")


def lights(eas):
    sun = eas.spawn_actor_from_class(unreal.DirectionalLight, vec(0, 0, 4000.0),
                                     unreal.Rotator(roll=0.0, pitch=-45.0, yaw=30.0))
    finish(sun, "BLK_PG_Sun", "Lighting")
    sky = eas.spawn_actor_from_class(unreal.SkyLight, vec(0, 0, 4000.0))
    finish(sky, "BLK_PG_SkyLight", "Lighting")


def main():
    seed = int(sys.argv[1]) if len(sys.argv) > 1 else 20260825
    log("seed={}".format(seed))

    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

    open_or_create_level(les)
    clear_tagged(eas)

    b = Builder(eas, seed)
    b.ground()
    for tag, x, y in poi_positions():
        b.poi(x, y, tag)
    b.corridor()
    gameplay_actors(b, eas)
    lights(eas)

    saved = unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    log("Saved: {}".format(saved))
    log("Done. Level: {} | POI1/POI2/POI3 on a {:.0f} m triangle".format(LEVEL_PATH, POI_SIDE / 100.0))


if __name__ == "__main__":
    main()
