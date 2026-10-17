"""Grenades and medkits out of dead enemies [author, 2026-10-17].

No C++: every enemy already rolls ULootDropComponent.Loot on death, and AGrenadePickup / AMedkitPickup
already go into the bag. What was missing is one blueprint per kind of thing (a loot line names a
class, and the grenade type lives on the instance), a mesh that shows on the floor, and the lines.

Run one step at a time: Tools/ue_step.sh Loot build_drops <step>
    step_meshes()   project copies of the grenade, drone and medkit meshes with a box collision.
                    The originals have no simple collision, so a pickup made of them cannot
                    simulate and hangs where it spawned. Originals are not touched (same deal as
                    the optics copies in /Game/Variant_Shooter/Meshes/Optics/).
    step_types()    UGrenadeDefinition.PickupMesh on every DA_Grenade_*: the floor look of that type.
                    Also fixes the pile that falls out when a new kind takes a gadget place.
    step_pickups()  BP_GrenadePickup_<Type> and BP_MedkitPickup, falling on spawn.
    step_loot()     the lines in the enemies' loot lists. Chances are a first guess, tune per blueprint.
    check()         prints what is there.
Idempotent: every step looks first.
"""
import unreal, json

MESHES = "/Game/Variant_Shooter/Meshes/Pickups"
DROPS = "/Game/Variant_Shooter/Blueprints/Pickups/Drops"
GREN = "/Game/Variant_Shooter/Blueprints/Grenades"
AI = "/Game/Variant_Shooter/Blueprints/AI/BPs/"
BPT = "editor_toolset.toolsets.blueprint.BlueprintTools"

# copy name: (original, scale on the floor)
MESH_COPIES = {
    "SM_Pickup_GrenadeFrag": "/Game/InfimaGames/ArtCore/Weapons/Grenades/Meshes/SM_IG_Grenade_Frag",
    "SM_Pickup_FPVDrone": "/Game/FPV_Drone_Game_v03/SM_TF2_FPV_Game_v03",
    "SM_Pickup_Medkit": "/Game/Weapons/Healthpack/Mesh/SM_healthpackFull",
}
TYPE_MESH = {   # grenade type: (mesh copy, scale). Frag, flash and microwave share the one grenade model.
    "Frag": ("SM_Pickup_GrenadeFrag", 1.6),
    "Flash": ("SM_Pickup_GrenadeFrag", 1.6),
    "Microwave": ("SM_Pickup_GrenadeFrag", 1.6),
    "FPV": ("SM_Pickup_FPVDrone", 0.3),
}
MEDKIT_SCALE = 0.5

# All in one group per kind: at most one grenade and at most one medkit per body. Tried in this
# order, so the later ones are rarer still. Count is actors, not grenades: each holds one.
GRENADE_LINES = [("Frag", 0.15), ("Flash", 0.10), ("Microwave", 0.06), ("FPV", 0.04)]
MEDKIT_CHANCE = 0.20

# Infantry only. Children of BP_ShooterNPC that keep its list (Sniper, Tier*, PushTest) get the
# lines from it; the ones below it have lists of their own. Drones, turrets, the tank: nothing.
ENEMIES = ["BP_ShooterNPC", "BP_GrenadierNPC", "BP_JuggernautNPC", "BP_MeleeNPC"]


def log(s):
    print(s)
    unreal.log("[LOOT_BUILD] " + s)


def _compile_save(path):
    res = unreal.ToolsetRegistry.execute_tool(BPT, "compile_blueprint",
        json.dumps({"blueprint": {"refPath": path + "." + path.split("/")[-1]}}))
    bp = unreal.load_asset(path)
    ok = unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)
    log("COMPILED+SAVED: {} status {} saved {} err {}".format(path, bp.get_editor_property("status"), ok, res.error))


def step_meshes():
    sub = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    for name, src in MESH_COPIES.items():
        dst = MESHES + "/" + name
        if not unreal.EditorAssetLibrary.does_asset_exist(dst):
            unreal.EditorAssetLibrary.duplicate_asset(src, dst)
            log("CREATED: " + dst)
        m = unreal.load_asset(dst)
        if sub.get_simple_collision_count(m) == 0:
            sub.add_simple_collisions(m, unreal.ScriptCollisionShapeType.BOX)
            log("MODIFIED: box collision on " + dst)
        unreal.EditorAssetLibrary.save_asset(dst, only_if_is_dirty=False)


def step_types():
    for t, (mesh, scale) in TYPE_MESH.items():
        path = GREN + "/DA_Grenade_" + t
        d = unreal.load_asset(path)
        d.set_editor_property("pickup_mesh", unreal.load_asset(MESHES + "/" + mesh))
        d.set_editor_property("pickup_mesh_scale", unreal.Vector(scale, scale, scale))
        unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)
        log("MODIFIED: {} pickup mesh {} x{}".format(path, mesh, scale))


def _make_bp(name, parent):
    path = DROPS + "/" + name
    if not unreal.EditorAssetLibrary.does_asset_exist(path):
        fac = unreal.BlueprintFactory()
        fac.set_editor_property("parent_class", parent)
        unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, DROPS, unreal.Blueprint, fac)
        log("CREATED: " + path)
    return path


def step_pickups():
    for t in TYPE_MESH:
        path = _make_bp("BP_GrenadePickup_" + t, unreal.GrenadePickup)
        cls = unreal.EditorAssetLibrary.load_blueprint_class(path)
        cdo = unreal.get_default_object(cls)
        cdo.set_editor_property("type", unreal.load_asset(GREN + "/DA_Grenade_" + t))
        cdo.set_editor_property("count", 1)
        cdo.set_editor_property("start_simulating_physics", True)
        _compile_save(path)

    path = _make_bp("BP_MedkitPickup", unreal.MedkitPickup)
    cls = unreal.EditorAssetLibrary.load_blueprint_class(path)
    cdo = unreal.get_default_object(cls)
    cdo.set_editor_property("medkits", 1)
    cdo.set_editor_property("start_simulating_physics", True)
    mesh = cdo.get_components_by_class(unreal.StaticMeshComponent)[0]
    mesh.set_editor_property("static_mesh", unreal.load_asset(MESHES + "/SM_Pickup_Medkit"))
    mesh.set_editor_property("relative_scale3d", unreal.Vector(MEDKIT_SCALE, MEDKIT_SCALE, MEDKIT_SCALE))
    _compile_save(path)


def _line(cls, chance, group):
    e = unreal.LootDropEntry()
    e.set_editor_property("pickup_class", cls)
    e.set_editor_property("chance", chance)
    e.set_editor_property("count", 1)
    e.set_editor_property("condition", unreal.LootKillCondition.ANY)
    e.set_editor_property("group", group)
    return e


def step_loot():
    wanted = [(DROPS + "/BP_GrenadePickup_" + t, c, "Grenade") for t, c in GRENADE_LINES]
    wanted.append((DROPS + "/BP_MedkitPickup", MEDKIT_CHANCE, "Medkit"))
    for n in ENEMIES:
        path = AI + n
        cdo = unreal.get_default_object(unreal.EditorAssetLibrary.load_blueprint_class(path))
        comp = cdo.get_components_by_class(unreal.LootDropComponent)[0]
        loot = list(comp.get_editor_property("loot"))
        have = set(e.get_editor_property("pickup_class").get_path_name() for e in loot if e.get_editor_property("pickup_class"))
        added = 0
        for p, c, g in wanted:
            cls = unreal.EditorAssetLibrary.load_blueprint_class(p)
            if cls.get_path_name() in have:
                continue
            loot.append(_line(cls, c, g))
            added += 1
        if added:
            comp.set_editor_property("loot", loot)
            log("ADDED: {} lines to {}".format(added, path))
        _compile_save(path)


def check():
    names = ["BP_ShooterNPC", "BP_SniperNPC", "BP_ShooterNPC_TierWhite", "BP_ShooterNPC_TierBlue",
             "BP_ShooterNPC_TierPurple", "BP_ShooterNPC_PushTest", "BP_GrenadierNPC", "BP_JuggernautNPC", "BP_MeleeNPC"]
    for n in names:
        cdo = unreal.get_default_object(unreal.EditorAssetLibrary.load_blueprint_class(AI + n))
        loot = cdo.get_components_by_class(unreal.LootDropComponent)[0].get_editor_property("loot")
        ours = [e.get_editor_property("pickup_class").get_name() for e in loot
                if e.get_editor_property("pickup_class") and "Drops" in e.get_editor_property("pickup_class").get_path_name()]
        log("{}: {} lines, ours {}".format(n, len(loot), ours))
