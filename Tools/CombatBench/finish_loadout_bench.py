"""Finish the hub in place, keeping hand edits and moving tagged groups only."""
import unreal

LEVEL = "/Game/Prototype/CombatBench/L_CombatBench"
TAG = "CombatBenchLoadout"


def guard():
    sub = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    if sub.get_game_world():
        raise RuntimeError("PIE is running")
    world = sub.get_editor_world()
    if not world or world.get_package().get_name() != LEVEL:
        raise RuntimeError("Open L_CombatBench first")
    return unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def layout():
    eas = guard()
    actors = eas.get_all_level_actors()

    def move(tag, anchor, target):
        a = next(a for a in actors if a.get_actor_label() == anchor)
        center, _ = a.get_actor_bounds(False)
        delta = unreal.Vector(*target) - center
        for item in actors:
            if item.actor_has_tag(tag):
                # Weapon tables share a tag; keep the left and right table with their own racks.
                if tag == "CombatBenchArmory" and (item.get_actor_location().y > 0) != (center.y > 0):
                    continue
                item.set_actor_location(item.get_actor_location() + delta, False, False)
        print("MOVED", tag, anchor, delta)

    move("CombatBenchUpgrades", "Upgrades_Wall_0", (-520, 1050, 295))
    move("CombatBenchArmory", "Armory_Table_L", (-100, 650, 45))
    move("CombatBenchArmory", "Armory_Table_R", (-100, -850, 45))
    move("CombatBenchAttachments", "Attach_TableNear", (-240, -1120, 45))
    move("CombatBenchGrenades", "Grenade_Stand", (-180, -320, 30))
    for a in actors:
        if isinstance(a, unreal.PlayerStart) and a.get_actor_label() == "CB_PlayerStart":
            tags = list(a.tags)
            if "CombatBenchHubSpawn" not in [str(t) for t in tags]:
                tags.append("CombatBenchHubSpawn")
                a.set_editor_property("tags", tags)
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    print("LAYOUT_SAVED")


def buttons():
    eas = guard()
    # Idempotent updates reuse the same actors. No common assets or map actors are deleted.
    existing = {a.get_actor_label(): a for a in eas.get_all_level_actors() if a.actor_has_tag(TAG)}
    specs = [("Bench_Ammo20", unreal.BenchLoadoutAction.GIVE_AMMO, -280),
             ("Bench_SavePreset", unreal.BenchLoadoutAction.SAVE_PRESET, 0),
             ("Bench_RestorePreset", unreal.BenchLoadoutAction.RESTORE_PRESET, 280)]
    for name, action, y in specs:
        a = existing.get(name)
        if not a:
            a = eas.spawn_actor_from_class(unreal.BenchLoadoutButton, unreal.Vector(-450, y, 150),
                                          unreal.Rotator(roll=0, pitch=0, yaw=0))
            a.set_actor_label(name)
            a.set_folder_path("CombatBench/Loadout")
            a.set_editor_property("tags", [TAG])
        a.set_editor_property("action", action)
        print("BUTTON", name, a.get_actor_location(), action)
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    print("BUTTONS_SAVED")


def verify():
    eas = guard()
    actors = eas.get_all_level_actors()
    for a in actors:
        if a.actor_has_tag(TAG) or a.actor_has_tag("CombatBenchHubSpawn"):
            print("VERIFY", a.get_actor_label(), a.get_actor_location(), [str(t) for t in a.tags])
    # Read actual geometry bounds, not mesh pivots (the bench cubes use corner pivots).
    for name in ("Upgrades_Wall_0", "Hub_Wall_0", "Armory_Table_L", "Armory_Table_R", "Grenade_Stand"):
        a = next(a for a in actors if a.get_actor_label() == name)
        c, e = a.get_actor_bounds(False)
        print("BOUNDS", name, c, e)
    print("DIRTY", [p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()])


def spear():
    eas = guard()
    path = "/Game/Variant_Shooter/Blueprints/Pickups/Weapons/Kinemation/ARs/BP_SiGMCXSpear"
    cls = unreal.EditorAssetLibrary.load_blueprint_class(path)
    if not cls or not unreal.MathLibrary.class_is_child_of(cls, unreal.ShooterWeapon):
        raise RuntimeError("BP_SiGMCXSpear is missing or is not a ShooterWeapon")
    actors = eas.get_all_level_actors()
    racks = [a for a in actors if isinstance(a, unreal.BenchWeaponRack)]
    matches = [a for a in racks if a.get_editor_property("weapon_class") == cls]
    if matches:
        print("SPEAR_ALREADY_PRESENT", matches[0].get_actor_location())
        return
    left = [a for a in racks if a.get_actor_location().y > 0]
    first = min(left, key=lambda a: a.get_actor_location().x)
    pos = first.get_actor_location() - unreal.Vector(85, 0, 0)
    table = next(a for a in actors if a.get_actor_label() == "Armory_Table_L")
    center, extent = table.get_actor_bounds(False)
    scale = table.get_actor_scale3d()
    scale.x *= (extent.x * 2 + 85) / (extent.x * 2)
    table.set_actor_scale3d(scale)
    grown_center, _ = table.get_actor_bounds(False)
    table.set_actor_location(table.get_actor_location() + center - unreal.Vector(42.5, 0, 0) - grown_center, False, False)
    rack = eas.spawn_actor_from_class(unreal.BenchWeaponRack, pos, first.get_actor_rotation())
    rack.set_actor_label("Armory_SiGMCXSpear")
    rack.set_folder_path("CombatBench/Armory/Racks")
    rack.set_editor_property("tags", ["CombatBenchArmory"])
    rack.set_editor_property("weapon_class", cls)
    rack.set_editor_property("title", "SiGMCXSpear")
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    print("SPEAR_ADDED", rack.get_actor_location(), cls.get_path_name())


def muzzles():
    """Replace the front ricochet pickup with the suppressor; remove the old stagger pair."""
    eas = guard()
    tag = "CombatBenchAttachments"
    actors = [a for a in eas.get_all_level_actors() if a.actor_has_tag(tag)]
    by_label = {a.get_actor_label(): a for a in actors}
    root = "/Game/Variant_Shooter/Blueprints/Pickups/Attachments/Muzzles/"
    suppressor = unreal.load_asset(root + "DA_Attach_Muzzle_Suppressor")
    assert suppressor, "Missing suppressor asset"

    pickup = by_label.get("Attach_Muzzle_Suppressor")
    source = "Suppressor"
    if not pickup:
        for suffix in ("Ricochet", "Stagger"):
            pickup = by_label.get("Attach_Muzzle_" + suffix)
            if pickup:
                source = suffix
                break
    assert pickup, "No existing muzzle pickup to reuse"
    label = by_label.get("Attach_Label_Muzzle_" + source)
    assert label, "Missing matching muzzle label"

    pickup.set_editor_property("attachment", suppressor)
    pickup.set_actor_label("Attach_Muzzle_Suppressor")
    label.get_component_by_class(unreal.TextRenderComponent).set_text("Muzzle\nSuppressor")
    label.set_actor_label("Attach_Label_Muzzle_Suppressor")
    for actor in actors:
        if actor.get_actor_label() in (
            "Attach_Muzzle_Ricochet", "Attach_Label_Muzzle_Ricochet",
            "Attach_Muzzle_Stagger", "Attach_Label_Muzzle_Stagger",
        ):
            print("REMOVED", actor.get_actor_label())
            assert eas.destroy_actor(actor)
    assert unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    print("MUZZLES_SAVED", pickup.get_actor_label(), pickup.get_actor_location())
