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
