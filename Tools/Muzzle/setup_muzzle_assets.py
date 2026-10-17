import unreal

# Muzzle attachments: Docs/Muzzle_Attachment_Plan_2026-10-01.md.
# Run a step through run_step.py (execute_python_code cuts long code). Idempotent: an existing asset
# is only filled where empty, numbers are written only on creation.

ROOT = "/Game/Variant_Shooter/Blueprints/Pickups/Attachments/Muzzles"
PART_DIR = "/Game/InfimaGames/AnimatedLowPolyWeapons/Art/Weapons/_Common/Attachments/Models/"
PARTS = {
    "Simple": PART_DIR + "SM_ATT_Muzzle_Silencer_01",
    "Stagger": PART_DIR + "SM_ATT_Muzzle_Silencer_02",
    "Ricochet": PART_DIR + "SM_ATT_Muzzle_Silencer_03",
}
SLOW_OVERLAY = "/Game/Variant_Shooter/Tactical/Materials/MI_TacticalProto_Frozen"
WEAPON_DIRS = ["/Game/Variant_Shooter/Blueprints/Pickups/Weapons/Kinemation"]
SKIP = ("BP_RPG7",)
EAL = unreal.EditorAssetLibrary
BT = "editor_toolset.toolsets.blueprint.BlueprintTools"


def tools():
    return unreal.AssetToolsHelpers.get_asset_tools()


def step_measure():
    # The tactical lesson: a mesh picked by its name was a 0.6 cm effect point. Measure first.
    for key, path in PARTS.items():
        m = unreal.load_asset(path)
        if not m:
            print("MISSING:", path)
            continue
        b = m.get_bounds()
        print("PART", key, path, "extent", b.box_extent, "materials", [s.material_interface.get_name() if s.material_interface else None for s in m.static_materials])


def get_or_create_da(name):
    path = ROOT + "/" + name
    if EAL.does_asset_exist(path):
        return unreal.load_asset(path), False
    fac = unreal.DataAssetFactory()
    fac.set_editor_property("data_asset_class", unreal.WeaponAttachmentDefinition)
    a = tools().create_asset(name, ROOT, None, fac)
    print("CREATED:", path)
    return a, True


def step_assets():
    overlay = unreal.load_asset(SLOW_OVERLAY)
    specs = (
        # name, label, effect, horizontal, vertical
        # Labels are the author's own words for them, not invented names.
        ("Simple", "Muzzle Simple", unreal.MuzzleEffect.NONE, 0.5, 0.5),
        ("Stagger", "Muzzle Mozambique", unreal.MuzzleEffect.STAGGER, 0.8, 0.8),
        ("Ricochet", "Muzzle Smart Ricochet", unreal.MuzzleEffect.RICOCHET, 0.8, 0.8),
    )
    for key, label, effect, h, v in specs:
        att, new = get_or_create_da("DA_Attach_Muzzle_" + key)
        if new:
            att.set_editor_property("type", unreal.WeaponAttachmentType.MUZZLE)
            att.set_editor_property("display_name", label)
            att.set_editor_property("compatible_weapons", [unreal.ShooterWeapon])
            att.set_editor_property("fx_socket_name", "SOCKET_Emitter")
            att.set_editor_property("muzzle_effect", effect)
            att.set_editor_property("horizontal_recoil_multiplier", h)
            att.set_editor_property("vertical_recoil_multiplier", v)
            if effect == unreal.MuzzleEffect.STAGGER and overlay:
                att.set_editor_property("center_slow_overlay", overlay)
        if att.get_editor_property("mesh") is None:
            att.set_editor_property("mesh", unreal.load_asset(PARTS[key]))
            print("MODIFIED:", att.get_name(), "mesh")
        EAL.save_loaded_asset(att)
        print("SAVED:", att.get_name(), "effect", att.get_editor_property("muzzle_effect"),
              "recoil", att.get_editor_property("horizontal_recoil_multiplier"),
              att.get_editor_property("vertical_recoil_multiplier"))


def compile_bp(path):
    # Same call as setup_tactical_assets.py, which worked on these 21 guns.
    import json
    res = unreal.ToolsetRegistry.execute_tool(BT, "compile_blueprint",
        json.dumps({"blueprint": {"refPath": path + "." + path.split("/")[-1]}}))
    return res.error if res.error else "ok"


def step_weapons():
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    done = 0
    for d in WEAPON_DIRS:
        for ad in ar.get_assets_by_path(d, recursive=True):
            if str(ad.asset_class_path.asset_name) != "Blueprint":
                continue
            name = str(ad.asset_name)
            if name in SKIP:
                continue
            path = str(ad.package_name)
            cls = EAL.load_blueprint_class(path)
            if not cls:
                continue
            cdo = unreal.get_default_object(cls)
            if not isinstance(cdo, unreal.ShooterWeapon) or isinstance(cdo, unreal.ShooterWeapon_Melee):
                continue
            slots = list(cdo.get_editor_property("attachment_slots"))
            if unreal.WeaponAttachmentType.MUZZLE in slots:
                print("HAS:", name)
                continue
            slots.append(unreal.WeaponAttachmentType.MUZZLE)
            cdo.set_editor_property("attachment_slots", slots)
            r = compile_bp(path)
            EAL.save_asset(path)
            print("MODIFIED:", name, "slots", len(slots), r)
            done += 1
    print("WEAPONS done", done)


def step_sockets():
    # Static mesh sockets are protected in Python: read them off a spawned component.
    sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for key, path in PARTS.items():
        m = unreal.load_asset(path)
        a = sub.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0, 0, -100000))
        c = a.static_mesh_component
        c.set_static_mesh(m)
        names = c.get_all_socket_names()
        print("SOCKETS", key, [str(n) for n in names],
              [str(c.get_socket_transform(n, unreal.RelativeTransformSpace.RTS_COMPONENT).translation) for n in names])
        sub.destroy_actor(a)


def step_brake_suppressor(sound_path=""):
    """One-time migration of Simple into the brake; add a separate suppressor and shop entry.

    Existing Simple references remain valid. Re-running preserves designer tuning. Passing a
    Sound and flash assets belong to each weapon. The optional sound_path initializes empty
    per-weapon fields through step_weapon_suppressor_effects, never a shared attachment field.
    """
    assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world(), "Stop PIE first"
    brake = unreal.load_asset(ROOT + "/DA_Attach_Muzzle_Simple")
    assert brake, "Run step_assets first"
    suppressor_path = ROOT + "/DA_Attach_Muzzle_Suppressor"
    suppressor = unreal.load_asset(suppressor_path) if EAL.does_asset_exist(suppressor_path) else None
    if not suppressor:
        suppressor = EAL.duplicate_asset(brake.get_path_name().split(".")[0], suppressor_path)
        assert suppressor, "Could not create suppressor"
        # A duplicate inherits the brake's migration marker, so this new item needs its own setup.
        EAL.remove_metadata_tag(suppressor, "MuzzleHandlingVersion")

    for asset, label, recoil, noise, penalties in (
        (brake, "ДТК", 0.5, 1.0, (1.05, 1.03, 1.05, 1.05, 1.05)),
        (suppressor, "Глушитель", 0.85, 0.2, (1.15, 1.10, 1.10, 1.10, 1.15)),
    ):
        if EAL.get_metadata_tag(asset, "MuzzleHandlingVersion") != "1":
            asset.set_editor_property("display_name", label)
            asset.set_editor_property("muzzle_effect", unreal.MuzzleEffect.NONE)
            asset.set_editor_property("horizontal_recoil_multiplier", recoil)
            asset.set_editor_property("vertical_recoil_multiplier", recoil)
            asset.set_editor_property("shot_noise_range_multiplier", noise)
            for prop, value in zip(("ads_blend_in_time_multiplier", "ads_blend_out_time_multiplier",
                                    "draw_duration_multiplier", "holster_duration_multiplier",
                                    "sprint_to_fire_time_multiplier"), penalties):
                asset.set_editor_property(prop, value)
            EAL.set_metadata_tag(asset, "MuzzleHandlingVersion", "1")
        asset.set_editor_property("is_suppressor", asset == suppressor)
        assert EAL.save_loaded_asset(asset, only_if_is_dirty=False), asset.get_name()
        print("SAVED", asset.get_path_name(), str(asset.display_name), "noise", asset.shot_noise_range_multiplier,
              "is_suppressor", asset.is_suppressor)

    pool = unreal.load_asset("/Game/Variant_Shooter/Blueprints/Upgrades/DA_DispenserUpgradePool")
    assert pool, "Missing dispenser pool"
    entries = list(pool.get_editor_property("attachments"))
    if not any(entry.attachment == suppressor for entry in entries):
        # Preserve the existing entry's weights and all unrelated contents of the shared pool.
        template = next(entry for entry in entries if entry.attachment == brake)
        entry = unreal.DispenserAttachmentEntry()
        entry.import_text(template.export_text().replace(brake.get_path_name(), suppressor.get_path_name()))
        assert entry.attachment == suppressor, entry.export_text()
        entries.append(entry)
        pool.set_editor_property("attachments", entries)
        assert EAL.save_loaded_asset(pool), "Could not save dispenser pool"
    print("POOL", [entry.attachment.get_name() for entry in entries
                   if entry.attachment and entry.attachment.type == unreal.WeaponAttachmentType.MUZZLE])
    if sound_path:
        step_weapon_suppressor_effects(sound_path=sound_path)


def step_snapshot_suppressor_effects():
    """Run before rebuilding out the old attachment-owned properties."""
    import json
    from pathlib import Path
    assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world(), "Stop PIE first"
    asset = unreal.load_asset(ROOT + "/DA_Attach_Muzzle_Suppressor")
    assert asset
    data = {}
    for key, prop in (("sound", "fire_sound_override"), ("flash", "muzzle_flash_fx_override")):
        obj = asset.get_editor_property(prop)
        data[key] = obj.get_path_name() if obj else ""
    path = Path(__file__).resolve().parents[3] / "Saved/MuzzleHandling/suppressor_effects_before_move.json"
    path.write_text(json.dumps(data, indent=2), encoding="utf-8")
    print("SNAPSHOT", data)


def step_weapon_suppressor_effects(sound_path="", flash_path=""):
    """Migrate old shared effects into EMPTY per-weapon fields; preserve later hand tuning."""
    import json
    from pathlib import Path
    assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world(), "Stop PIE first"
    path = Path(__file__).resolve().parents[3] / "Saved/MuzzleHandling/suppressor_effects_before_move.json"
    old = json.loads(path.read_text(encoding="utf-8")) if path.exists() else {}
    sound_path = sound_path or old.get("sound") or "/Game/InfimaGames/LowPolyShooterPack/Audio/Cues/Weapons/SC_WEP_Silencer_01_Fire"
    flash_path = flash_path or old.get("flash") or "/Game/NiagaraExamples/FX_Weapons/MuzzleFlashes/NS_MuzzleFlashSilenced"
    sound, flash = unreal.load_asset(sound_path), unreal.load_asset(flash_path)
    assert isinstance(sound, unreal.SoundBase), sound_path
    assert isinstance(flash, unreal.NiagaraSystem), flash_path
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    count = 0
    for folder in WEAPON_DIRS:
        entries = ar.get_assets_by_path(folder, recursive=True)
        for entry in entries:
            if str(entry.asset_class_path.asset_name) != "Blueprint" or str(entry.asset_name) in SKIP:
                continue
            asset_path = str(entry.package_name)
            cls = EAL.load_blueprint_class(asset_path)
            if not cls:
                continue
            cdo = unreal.get_default_object(cls)
            if not isinstance(cdo, unreal.ShooterWeapon) or isinstance(cdo, unreal.ShooterWeapon_Melee):
                continue
            if unreal.WeaponAttachmentType.MUZZLE not in cdo.get_editor_property("attachment_slots"):
                continue
            changed = False
            for prop, value in (("suppressed_fire_sound", sound), ("suppressed_muzzle_flash_fx", flash)):
                if cdo.get_editor_property(prop) is None:
                    cdo.set_editor_property(prop, value)
                    changed = True
            if changed:
                result = compile_bp(asset_path)
                assert result == "ok", (asset_path, result)
                bp = unreal.load_asset(asset_path)
                assert bp.get_editor_property("status") == unreal.BlueprintStatus.BS_UP_TO_DATE, asset_path
                assert EAL.save_asset(asset_path, only_if_is_dirty=False), asset_path
            # Compiling can replace the CDO; read the current one again.
            current = unreal.get_default_object(EAL.load_blueprint_class(asset_path))
            print("WEAPON_SUPPRESSOR", entry.asset_name, current.suppressed_fire_sound.get_path_name(),
                  current.suppressed_muzzle_flash_fx.get_path_name(), "changed", changed)
            count += 1
    print("WEAPON_SUPPRESSOR_DONE", count)
