# Generic level snapshot tool for Polarity (companion to build_arena.py).
#
# Captures the CURRENTLY OPEN level to PNGs without touching/saving it. Used to
# review hand-made levels that have no blockout JSON spec.
#
# Usage (in-editor console or via UnrealClaude run_console_command):
#   py "<...>/Source/Tools/ArenaBlockout/snap_level.py"
#       -> auto-framed set: top + 4 iso views, derived from level bounds
#   py "<...>/Source/Tools/ArenaBlockout/snap_level.py" "<...>/my_shots.json"
#       -> custom shots: [{"id": "entry", "pos": [x,y,z], "pitch": -10, "yaw": 90, "fov": 90}, ...]
#
# Output: Build/Screenshots/<LevelName>_<shotid>.png (1600x900).
# Transient capture actor + lights are destroyed afterwards; the level is never saved.
# Transient sun/fill lights are only spawned when the level has no DirectionalLight.
#
# Log filter tag: [LEVEL_SNAP]

import json
import math
import os
import sys

import unreal

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
OUT_DIR = os.path.join(TOOLS_DIR, "Build", "Screenshots")

# Actor classes that must not contribute to auto-framing bounds.
BOUNDS_SKIP_CLASSES = (
    "Brush", "WorldSettings", "RecastNavMesh", "NavMeshBoundsVolume",
    "PostProcessVolume", "LightmassImportanceVolume", "SkyAtmosphere",
    "ExponentialHeightFog", "SkyLight", "DirectionalLight", "VolumetricCloud",
)


def log(msg):
    unreal.log("[LEVEL_SNAP] {}".format(msg))


def warn(msg):
    unreal.log_warning("[LEVEL_SNAP] {}".format(msg))


def vec(xyz):
    return unreal.Vector(float(xyz[0]), float(xyz[1]), float(xyz[2]))


def rot(spec):
    return unreal.Rotator(
        roll=float(spec.get("roll", 0.0)),
        pitch=float(spec.get("pitch", 0.0)),
        yaw=float(spec.get("yaw", 0.0)),
    )


def collect_candidates(actors):
    """(label, origin, extent) for gameplay-relevant actors (meshes + /Game/ BPs)."""
    out = []
    for a in actors:
        if a is None:
            continue
        cls = a.get_class()
        cls_name = cls.get_name()
        if cls_name in BOUNDS_SKIP_CLASSES or cls_name.endswith("Volume"):
            continue
        is_bp = cls.get_path_name().startswith("/Game/")
        if cls_name != "StaticMeshActor" and not is_bp:
            continue
        origin, extent = a.get_actor_bounds(False)
        if extent.x <= 1 and extent.y <= 1 and extent.z <= 1:
            continue
        # Some actors (e.g. sky/atmosphere BPs with fixed bounds) report absurd extents
        # that would catapult the auto-framed cameras into space - skip them.
        if max(extent.x, extent.y, extent.z) > 200000.0:
            warn("Skipping '{}' ({}): extent {:.0f} uu exceeds sanity cap".format(
                a.get_actor_label(), cls_name, max(extent.x, extent.y, extent.z)))
            continue
        tagged = any(str(t).startswith("BLOCKOUT_") for t in a.tags)
        out.append((a.get_actor_label(), origin, extent, tagged))
    return out


def median(values):
    s = sorted(values)
    n = len(s)
    return s[n // 2] if n % 2 else (s[n // 2 - 1] + s[n // 2]) * 0.5


def level_bounds(actors):
    """Combined bounds of the level's main actor cluster.

    Prefers BLOCKOUT_*-tagged actors (clean arena framing). Otherwise rejects
    XY outliers around the median center - hand-made levels tend to contain
    stray debris (test checkpoints, far-away pickups) that would wreck framing."""
    cands = collect_candidates(actors)
    if not cands:
        return None, None
    tagged = [c for c in cands if c[3]]
    if tagged:
        cands = tagged
        log("Framing from {} BLOCKOUT-tagged actors".format(len(cands)))
    else:
        mx = median([c[1].x for c in cands])
        my = median([c[1].y for c in cands])
        dists = sorted(math.hypot(c[1].x - mx, c[1].y - my) for c in cands)
        p75 = dists[int(len(dists) * 0.75)]
        threshold = max(15000.0, p75 * 2.5)
        kept = []
        for c in cands:
            if math.hypot(c[1].x - mx, c[1].y - my) > threshold:
                warn("Outlier skipped: '{}' is {:.0f} uu from level median".format(
                    c[0], math.hypot(c[1].x - mx, c[1].y - my)))
            else:
                kept.append(c)
        cands = kept
        log("Framing from {} actors (median outlier filter, threshold {:.0f})".format(
            len(cands), threshold))
    mins = [None, None, None]
    maxs = [None, None, None]
    for _, origin, extent, _ in cands:
        lo = (origin.x - extent.x, origin.y - extent.y, origin.z - extent.z)
        hi = (origin.x + extent.x, origin.y + extent.y, origin.z + extent.z)
        for i in range(3):
            mins[i] = lo[i] if mins[i] is None else min(mins[i], lo[i])
            maxs[i] = hi[i] if maxs[i] is None else max(maxs[i], hi[i])
    center = [(mins[i] + maxs[i]) * 0.5 for i in range(3)]
    extent = [(maxs[i] - mins[i]) * 0.5 for i in range(3)]
    log("Bounds: center=({:.0f},{:.0f},{:.0f}) extent=({:.0f},{:.0f},{:.0f})".format(
        center[0], center[1], center[2], extent[0], extent[1], extent[2]))
    return center, extent


def auto_shots(center, extent):
    """Top-down + 4 isometric views framed around the bounds (fov 90)."""
    cx, cy, cz = center
    ex, ey, ez = extent
    half = max(ex, ey)
    shots = [{
        "id": "top",
        "pos": [cx, cy, cz + ez + half * 1.15 + 500.0],
        "pitch": -90.0, "yaw": 0.0, "fov": 90.0,
    }]
    dist = max(ex, ey, ez * 2.0) * 1.8 + 500.0
    for name, yaw in (("iso_ne", 225.0), ("iso_se", 315.0), ("iso_sw", 45.0), ("iso_nw", 135.0)):
        pitch = -35.0
        r = unreal.Rotator(roll=0.0, pitch=pitch, yaw=yaw)
        fwd = unreal.MathLibrary.get_forward_vector(r)
        shots.append({
            "id": name,
            "pos": [cx - fwd.x * dist, cy - fwd.y * dist, cz - fwd.z * dist],
            "pitch": pitch, "yaw": yaw, "fov": 90.0,
        })
    return shots


def take_shots(shots, label):
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    if not os.path.isdir(OUT_DIR):
        os.makedirs(OUT_DIR)
    actors = list(eas.get_all_level_actors())

    has_sun = any(a is not None and isinstance(a, unreal.DirectionalLight) for a in actors)
    rt = unreal.RenderingLibrary.create_render_target2d(
        world, 1600, 900, unreal.TextureRenderTargetFormat.RTF_RGBA8)
    cap = eas.spawn_actor_from_class(unreal.SceneCapture2D, unreal.Vector(0, 0, 0))
    comp = cap.capture_component2d
    comp.set_editor_property("texture_target", rt)
    comp.set_editor_property("capture_every_frame", False)
    transient = [cap]

    if not has_sun:
        sun = eas.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 3000),
                                         unreal.Rotator(roll=0.0, pitch=-55.0, yaw=35.0))
        fill = eas.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 3000),
                                          unreal.Rotator(roll=0.0, pitch=-30.0, yaw=215.0))
        try:
            fill.get_component_by_class(unreal.DirectionalLightComponent).set_editor_property(
                "intensity", 3.0)
        except Exception as e:
            warn("fill light intensity: {}".format(e))
        transient += [sun, fill]
        log("Level has no DirectionalLight -> transient sun+fill spawned for capture")
    else:
        log("Level has own DirectionalLight -> capturing with level lighting")

    # Camera-mounted shadowless flash so interiors are readable.
    flash = eas.spawn_actor_from_class(unreal.PointLight, unreal.Vector(0, 0, 0))
    try:
        flash_comp = flash.get_component_by_class(unreal.PointLightComponent)
        flash_comp.set_editor_property("intensity", 30000.0)
        flash_comp.set_editor_property("attenuation_radius", 12000.0)
        flash_comp.set_editor_property("cast_shadows", False)
    except Exception as e:
        warn("flash light setup: {}".format(e))
    transient.append(flash)

    saved = []
    try:
        for shot in shots:
            flash.set_actor_location(vec(shot["pos"]), False, False)
            cap.set_actor_location_and_rotation(vec(shot["pos"]), rot(shot), False, False)
            comp.set_editor_property("fov_angle", float(shot.get("fov", 90.0)))
            comp.set_editor_property("capture_source",
                                     unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
            comp.capture_scene()
            comp.capture_scene()  # second pass: let eye adaptation settle
            name = "{}_{}.png".format(label, shot.get("id", "shot"))
            unreal.RenderingLibrary.export_render_target(world, rt, OUT_DIR, name)
            saved.append(name)
            log("Screenshot saved: {}".format(os.path.join(OUT_DIR, name)))
    finally:
        for a in transient:
            try:
                eas.destroy_actor(a)
            except Exception:
                pass
    log("Done: {} shots for level '{}' -> {}".format(len(saved), label, OUT_DIR))


def main():
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    label = world.get_name() if world else "Unknown"
    log("Snapping level '{}'".format(label))

    shots = None
    if len(sys.argv) > 1 and sys.argv[1].strip():
        shots_path = sys.argv[1].strip()
        with open(shots_path, "r", encoding="utf-8") as f:
            shots = json.load(f)
        log("Custom shots loaded: {} ({})".format(shots_path, len(shots)))
    else:
        eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
        center, extent = level_bounds(eas.get_all_level_actors())
        if center is None:
            warn("No boundable actors found - nothing to frame, aborting")
            return
        shots = auto_shots(center, extent)

    take_shots(shots, label)


main()
