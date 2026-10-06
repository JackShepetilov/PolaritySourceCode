"""Подключить NS_Impact_Flesh: общая таблица + снять личную кровь со стволов.

У части стволов в самом блюпринте есть ImpactFXBySurface[Flesh], и она главнее общей таблицы
(AShooterWeapon::SpawnImpactEffectLocally). Пока запись там, новая кровь на таком стволе не видна.
"""

import json

import unreal

EAL = unreal.EditorAssetLibrary
FLESH = unreal.PhysicalSurface.SURFACE_TYPE4
SETS = "/Game/Variant_Shooter/Blueprints/"
WEAPONS = "/Game/Variant_Shooter/Blueprints/Pickups/Weapons"


def tables():
    fx = unreal.load_asset("/Game/Effects/Impacts/NS_Impact_Flesh")
    for n in ("DA_HitFeedbackSet", "DA_HitFeedbackSetNoShield"):
        a = unreal.load_asset(SETS + n)
        m = dict(a.get_editor_property("impacts"))
        e = m.get(FLESH, unreal.ImpactFeedback())
        e.set_editor_property("vfx", fx)
        m[FLESH] = e
        a.set_editor_property("impacts", m)
        print("MODIFIED:", n, "Flesh ->", a.get_editor_property("impacts")[FLESH].vfx.get_name(),
              "| SAVED:", EAL.save_asset(SETS + n, only_if_is_dirty=False))


def weapons():
    if unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world():
        print("PIE_RUNNING: nothing changed")
        return
    reg = unreal.AssetRegistryHelpers.get_asset_registry()
    n_changed = 0
    for a in reg.get_assets(unreal.ARFilter(package_paths=[WEAPONS], recursive_paths=True)):
        if str(a.asset_class_path.asset_name) != "Blueprint":
            continue
        path = str(a.package_name)
        name = str(a.asset_name)
        cls = unreal.load_object(None, path + "." + name + "_C")
        if not cls or not unreal.MathLibrary.class_is_child_of(cls, unreal.ShooterWeapon):
            continue
        cdo = unreal.get_default_object(cls)
        m = dict(cdo.get_editor_property("impact_fx_by_surface"))
        if FLESH not in m:
            continue
        old = m[FLESH].get_name() if m[FLESH] else None
        del m[FLESH]
        cdo.set_editor_property("impact_fx_by_surface", m)
        unreal.ToolsetRegistry.execute_tool("editor_toolset.toolsets.blueprint.BlueprintTools", "compile_blueprint",
                                            json.dumps({"blueprint": {"refPath": path + "." + name}}))
        n_changed += 1
        print("MODIFIED:", name, "removed own Flesh effect", old, "| SAVED:", EAL.save_asset(path, only_if_is_dirty=False))
    print("WEAPONS_CHANGED:", n_changed)
