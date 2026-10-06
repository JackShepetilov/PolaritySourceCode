"""Saved setup for the tank's surfaces/rows and the three isolated infantry tiers."""
import json
import unreal

EAL = unreal.EditorAssetLibrary
BP_DIR = "/Game/Variant_Shooter/Blueprints/AI/BPs/"
PROFILE_DIR = "/Game/Variant_Shooter/Blueprints/AI/CombatProfiles/"
TANK = BP_DIR + "BP_TrackedTank"
RICOCHETS = ["/Game/Audio/Sounds/Explosions/Explosions_Grenade_Ricochet_%02d" % i for i in range(1, 9)] + [
    "/Game/Variant_Shooter/Blueprints/HitSounds/ShieldHit/bullet_impact_metal_heavy_08"]
THUDS = ["/Game/InfimaGames/LowPolyShooterPack/Audio/Cues/SC_Thud",
         "/Game/SFX/virtual_vibes-thud-impact-sound-sfx-379990"]


def compile_bp(path):
    result = unreal.ToolsetRegistry.execute_tool(
        "editor_toolset.toolsets.blueprint.BlueprintTools", "compile_blueprint",
        json.dumps({"blueprint": {"refPath": path + "." + path.rsplit("/", 1)[-1]}}))
    if not result.is_complete or result.error:
        raise RuntimeError(str(result.error))
    EAL.save_asset(path, only_if_is_dirty=False)


def tank():
    cdo = unreal.get_default_object(EAL.load_blueprint_class(TANK))
    cdo.set_editor_property("one_track_speed_multiplier", 0.5)
    zones_comp = cdo.get_components_by_class(unreal.HitZoneComponent)[0]
    zones_comp.set_editor_property("use_zone_collision", True)
    zones = list(zones_comp.get_editor_property("zones"))
    for z in zones:
        name = str(z.get_editor_property("name"))
        surface = 11 if name == "Hull" else 13 if name == "Optics" else 12
        if name == "Optics":
            # The imported turret bone has scale 100; these are native bone coordinates.
            z.set_editor_property("center", unreal.Vector(0, -.23, .70))
            z.set_editor_property("extent", unreal.Vector(.30, .13, .10))
            z.set_editor_property("broken_effect_offset", unreal.Vector(0, -.16, 0))
        elif name in ("TrackLeft", "TrackRight"):
            # The mesh is 208 cm wide: mirrored tracks extend to +/-104, outside the hull.
            z.set_editor_property("extent", unreal.Vector(182, 21, 26))
            z.set_editor_property("broken_effect_offset", unreal.Vector(0, -25 if name == "TrackLeft" else 25, 0))
        z.set_editor_property("surface", getattr(unreal.PhysicalSurface, "SURFACE_TYPE" + str(surface)))
        if name != "Hull":
            z.set_editor_property("broken_surface", unreal.PhysicalSurface.SURFACE_TYPE14)
    zones_comp.set_editor_property("zones", zones)
    rows = []
    for icon, names in [(unreal.HitZoneBarIcon.TURRET, ["Optics"]),
                        (unreal.HitZoneBarIcon.TRACKS, ["TrackLeft", "TrackRight"])]:
        row = unreal.HitZoneBarRow()
        row.set_editor_property("icon", icon)
        row.set_editor_property("zones", [unreal.Name(n) for n in names])
        rows.append(row)
    zones_comp.set_editor_property("bar_rows", rows)
    compile_bp(TANK)
    unreal.GameplayTagService.add_tags(["Palette.HUD.Enemy.Part"], "Tank part durability")
    tag = unreal.GameplayTag()
    tag.import_text('(TagName="Palette.HUD.Enemy.Part")')
    if not unreal.PolarityPalette.has_color(tag):
        unreal.PolarityPalette.set_color(tag, unreal.LinearColor(0.871367, 0.254152, 0.045186, 1))
    print("[TANK_SETUP] surfaces, rows and one-track speed saved")


def feedback():
    ricochet = unreal.load_asset(RICOCHETS[0])
    thud = unreal.load_asset(THUDS[0])
    metal = unreal.load_asset("/Game/Sounds/Impacts/SCue_Impt_Bullet_Metal")
    assert ricochet and thud and metal
    for name in ("DA_HitFeedbackSet", "DA_HitFeedbackSetNoShield"):
        path = "/Game/Variant_Shooter/Blueprints/" + name
        asset = unreal.load_asset(path)
        for prop, sound in (("deflected", ricochet), ("dead_part", thud)):
            cue = asset.get_editor_property(prop)
            cue.set_editor_property("sound", sound)
            asset.set_editor_property(prop, cue)
        impacts = dict(asset.get_editor_property("impacts"))
        for surface, sound in ((11, ricochet), (12, metal), (13, metal), (14, thud)):
            key = getattr(unreal.PhysicalSurface, "SURFACE_TYPE" + str(surface))
            item = impacts.get(key, unreal.ImpactFeedback())
            item.set_editor_property("sound", sound)
            impacts[key] = item
        asset.set_editor_property("impacts", impacts)
        EAL.save_asset(path, only_if_is_dirty=False)
        print("[TANK_SETUP] feedback saved", path)
    print("[TANK_SETUP] sound candidates", RICOCHETS, THUDS)


def tiers():
    parent = EAL.load_blueprint_class(BP_DIR + "BP_ShooterNPC")
    fields = ("shield_charge", "hide_duration", "peek_duration", "crouch_peek_chance",
              "slide_peek_chance", "peek_aim_hold_seconds", "relocate_after_firing", "min_corner_seconds")
    settings = {"White": (50, 2.5, 2.5, .3, 0, .6, False, 6),
                "Blue": (75, 1.5, 2, .7, .2, .35, True, 4),
                "Purple": (100, 1, 1.2, 1, .5, .2, True, 2.5)}
    for tier, values in settings.items():
        profile_path = PROFILE_DIR + "DA_ECP_Shooter_" + tier
        if not EAL.does_asset_exist(profile_path):
            assert EAL.duplicate_asset(PROFILE_DIR + "DA_ECP_Shooter", profile_path)
        profile = unreal.load_asset(profile_path)
        profile.set_editor_property("override_peek_tuning", True)
        profile.set_editor_property("profile_name", "Shooter_" + tier)
        for field, value in zip(fields, values):
            profile.set_editor_property(field, value)
        EAL.save_asset(profile_path, only_if_is_dirty=False)
        bp_path = BP_DIR + "BP_ShooterNPC_Tier" + tier
        if not EAL.does_asset_exist(bp_path):
            factory = unreal.BlueprintFactory()
            factory.set_editor_property("parent_class", parent)
            assert unreal.AssetToolsHelpers.get_asset_tools().create_asset(
                bp_path.rsplit("/", 1)[-1], BP_DIR.rstrip("/"), unreal.Blueprint, factory)
        cdo = unreal.get_default_object(EAL.load_blueprint_class(bp_path))
        cdo.set_editor_property("combat_profile", profile)
        compile_bp(bp_path)
        # Read a spawned editor instance to catch stale Blueprint defaults before PIE.
        eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
        probe = eas.spawn_actor_from_class(EAL.load_blueprint_class(bp_path), unreal.Vector(0, 0, -10000))
        try:
            assert probe.get_editor_property("combat_profile") == profile
        finally:
            eas.destroy_actor(probe)
        print("[TIER_SETUP] saved and spawned", tier, values)
