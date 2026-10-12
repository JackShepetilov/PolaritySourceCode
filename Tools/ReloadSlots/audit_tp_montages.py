# Every third person body montage the weapons point at: its slot and whether its sequence is
# additive. The Action slots of the shared TP graph are additive; a full pose there replaces the legs.
import unreal

WEAPON_DIR = "/Game/Variant_Shooter/Blueprints/Pickups/Weapons/Kinemation"
FIELDS = ["ReloadMontageTP", "SecondaryReloadMontageTP", "ReloadEndMontageTP", "FiringMontageTP",
          "CycleActionMontageTP", "DrawMontageTP", "HolsterMontageTP"]
OUT = unreal.Paths.project_saved_dir() + "ReloadSlots/audit_tp_montages.txt"


def audit():
    reg = unreal.AssetRegistryHelpers.get_asset_registry()
    flt = unreal.ARFilter(package_paths=[WEAPON_DIR], recursive_paths=True,
                          class_paths=[unreal.TopLevelAssetPath("/Script/Engine", "Blueprint")])
    seen, lines = {}, []
    for ad in reg.get_assets(flt):
        path = "%s.%s_C" % (ad.package_name, ad.asset_name)
        cls = unreal.find_object(None, path) or unreal.load_object(None, path)
        if not cls or not unreal.MathLibrary.class_is_child_of(cls, unreal.ShooterWeapon):
            continue
        cdo = unreal.get_default_object(cls)
        for f in FIELDS:
            try:
                m = cdo.get_editor_property(f)
            except Exception:
                continue
            if m:
                seen.setdefault(m.get_name(), {"m": m, "users": []})["users"].append("%s.%s" % (ad.asset_name, f))
    for name, e in sorted(seen.items()):
        slots, kinds = [], set()
        for tr in e["m"].get_editor_property("slot_anim_tracks"):
            slots.append(str(tr.get_editor_property("slot_name")))
            for s in tr.get_editor_property("anim_track").get_editor_property("anim_segments"):
                a = s.get_editor_property("anim_reference")
                kinds.add("additive" if a.get_editor_property("additive_anim_type") != unreal.AdditiveAnimationType.AAT_NONE else "FULLPOSE")
        action = any(s.startswith("Action") for s in slots)
        verdict = "BAD full pose in additive slot" if action and "FULLPOSE" in kinds else (
            "BAD additive in DefaultSlot" if not action and "additive" in kinds else "ok")
        lines.append("%-40s | %-32s | %-9s | %-30s | %s" % (name, ",".join(slots), ",".join(sorted(kinds)), verdict, " ".join(e["users"])))
    with open(OUT, "w", encoding="utf-8") as fh:
        fh.write("\n".join(lines) + "\n")
    print("AUDIT WRITTEN", len(lines))
