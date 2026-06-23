# Delete ALL A9_Temple spawn points and re-place them cleanly on the ground,
# spawning them directly INTO the A9 sublevel (so ArenaManager auto-collects them).
# A9 sits at world X+84000 in Lvl_ArenaTestRun. Ground L1 = Z0 top, X[78500,89500]
# Y[-3950,8050]; raised L5 occupies X[80500,87500] Y[50,7050], so ground spawns go
# in the SOUTH apron (Y<50) + the W/E/N strips around L5.
# Run: py "<...>/respawn_a9_spawns.py"   Log tag: [A9_SPAWN]
import unreal

eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
world = ues.get_editor_world()
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
TAG = "BLOCKOUT_A9_Temple"
SPCLS = unreal.load_class(None, "/Script/Polarity.ArenaSpawnPoint")
A9PKG = "/Game/Variant_Shooter/Arenas/Biome1/A9_Temple/Lvl_A9_Temple"


def log(m):
    unreal.log("[A9_SPAWN] {}".format(m))


def vec(x, y, z):
    return unreal.Vector(float(x), float(y), float(z))


def rot(yaw):
    return unreal.Rotator(roll=0.0, pitch=0.0, yaw=float(yaw))


# --- delete all old A9 spawns ---
old = 0
for a in list(eas.get_all_level_actors()):
    try:
        if a.get_class().get_name() == "ArenaSpawnPoint" and TAG in [str(t) for t in a.tags]:
            eas.destroy_actor(a)
            old += 1
    except Exception:
        pass
log("deleted {} old spawns".format(old))

# --- make the A9 sublevel current so new actors are created inside its package ---
made = False
a9_sl = unreal.GameplayStatics.get_streaming_level(world, A9PKG)
if a9_sl is None:
    a9_sl = unreal.GameplayStatics.get_streaming_level(world, "Lvl_A9_Temple")
if a9_sl is not None:
    for fn in ("make_level_current",):
        try:
            unreal.EditorLevelUtils.make_level_current(a9_sl)
            made = True
            break
        except Exception as e:
            log("make_level_current err: {}".format(e))
log("A9 sublevel made current: {}".format(made))

# label, worldX, worldY, Z, yaw, air, air_height
# AUTHORED LOCATIONS (synced from live level 2026-06-13): author hand-fixed these and
# DELETED all 3 air spawns (Air_s/n/c) — keep this list as the source of truth.
SPAWNS = [
    ("S_s_w",  82000, -2600, 60,   90, False, 0),
    ("S_s_m",  84000, -2530, 60,   90, False, 0),
    ("S_s_e",  86000, -2600, 60,   90, False, 0),
    ("S_w_s",  79860,  1500, 60,    0, False, 0),
    ("S_w_n",  79870,  5200, 60,    0, False, 0),
    ("S_e_s",  88130,  1500, 60,  180, False, 0),
    ("S_e_n",  88110,  5200, 60,  180, False, 0),
    ("S_n_w",  82200,  7500, 60,  -90, False, 0),
    ("S_n_e",  85800,  7500, 60,  -90, False, 0),
]
made_n = 0
for label, x, y, z, yaw, air, h in SPAWNS:
    act = eas.spawn_actor_from_class(SPCLS, vec(x, y, z), rot(yaw))
    if act is None:
        log("FAILED spawn {}".format(label))
        continue
    if air:
        try:
            act.set_editor_property("air_spawn", True)
            act.set_editor_property("air_spawn_height", float(h))
        except Exception as e:
            log("air props {}: {}".format(label, e))
    act.set_actor_label("BLK_A9_Temple_" + label)
    act.set_folder_path("A9_Temple/Spawns")
    tags = list(act.tags)
    tags.append(unreal.Name(TAG))
    act.set_editor_property("tags", tags)
    made_n += 1
log("spawned {} new ({} ground, {} air)".format(
    made_n, sum(1 for s in SPAWNS if not s[5]), sum(1 for s in SPAWNS if s[5])))

# --- save A9 sublevel + persistent ---
les.save_current_level()
try:
    a9pkg = unreal.load_package(A9PKG)
    if a9pkg:
        unreal.EditorLoadingAndSavingUtils.save_packages([a9pkg], False)
except Exception as e:
    log("save A9 pkg: {}".format(e))
log("RESULT: DONE")
