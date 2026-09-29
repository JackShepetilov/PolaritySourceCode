import unreal, json

# One-time fill of AShooterWeapon::AttachmentSlots (2026-09-26): a gun gets a slot of every type
# some attachment already whitelists it for (CompatibleWeapons, or MagazineSizeByWeapon for a mag).
# Only guns whose list is still EMPTY are touched, so the author's edits are never overwritten.
# Run after the C++ build:  Tools/mcp.sh py <launcher that execs this file>

BT = "editor_toolset.toolsets.blueprint.BlueprintTools"
ORDER = ["MUZZLE", "MAGAZINE", "OPTIC", "STOCK"]


def main():
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    f = unreal.ARFilter(class_paths=[unreal.TopLevelAssetPath("/Script/Polarity", "WeaponAttachmentDefinition")],
                        package_paths=["/Game"], recursive_paths=True)

    wanted = {}  # class path -> (class, set of type names)
    for a in ar.get_assets(f):
        d = unreal.load_asset(str(a.package_name))
        t = d.get_editor_property("type")
        if t == unreal.WeaponAttachmentType.MAGAZINE:
            classes = list(d.get_editor_property("magazine_size_by_weapon").keys())
        else:
            classes = list(d.get_editor_property("compatible_weapons"))
        for cls in classes:
            if not cls:
                continue
            key = cls.get_path_name()
            wanted.setdefault(key, (cls, set()))[1].add(t.name)

    for key, (cls, types) in sorted(wanted.items()):
        if not key.startswith("/Game/"):
            print("SKIP native class (set it on a Blueprint child):", key, sorted(types))
            continue
        cdo = unreal.get_default_object(cls)
        current = list(cdo.get_editor_property("attachment_slots"))
        if current:
            print("KEEP", key, [c.name for c in current])
            continue
        slots = [getattr(unreal.WeaponAttachmentType, n) for n in ORDER if n in types]
        cdo.set_editor_property("attachment_slots", slots)
        bp_path = key.split(".")[0]
        res = unreal.ToolsetRegistry.execute_tool(BT, "compile_blueprint",
            json.dumps({"blueprint": {"refPath": bp_path + "." + bp_path.split("/")[-1]}}))
        unreal.EditorAssetLibrary.save_asset(bp_path)
        print("MODIFIED:", bp_path, [s.name for s in slots], res.error or "")


main()
