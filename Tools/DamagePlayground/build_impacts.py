"""Сборка эффектов попадания из заготовок (Docs/VFX_Refs_2026-10-03). Бюджет: до ~30 спрайтов.

Запуск по шагам через Tools/mcp.sh (один шаг = один вызов, чтобы уложиться в таймаут тула):
    g = {"__name__": "bi"}; exec(compile(open(p, encoding="utf-8").read(), p, "exec"), g); g["armor_plate"]()
"""

import unreal

LIB = r"C:/Users/Professional/Documents/Unreal Projects/Polarity_Main5_8/Source/Tools/DamagePlayground/niagara_lib.py"
nl = {"__name__": "nl"}
exec(compile(open(LIB, encoding="utf-8").read(), LIB, "exec"), nl)

FX = "/Game/Effects/Impacts/"
MAT = "/Game/Effects/Impacts/Materials/"
BASE = FX + "NS_Impact_RobotBody"      # искры, основа для всех
EAL = unreal.EditorAssetLibrary


def _ensure(name, base=BASE):
    path = FX + name
    if not EAL.does_asset_exist(path):
        print("CREATED:", path, bool(EAL.duplicate_asset(base, path)))
        EAL.save_asset(path, only_if_is_dirty=False)
    return path


def armor_plate_debris():
    """Пластина: к искрам добавляются осколки. Эмиттер Debris уже заведён руками 2026-10-04."""
    p = _ensure("NS_Impact_ArmorPlate")
    nl["add_burst"](p, "Debris", 8, 0.35, 7.0, MAT + "M_Impact_ArmorFragments", grid=4, speed=300.0, cone=60.0, gravity=True)
    nl["finish"](p)


def armor_plate_fluff():
    p = _ensure("NS_Impact_ArmorPlate")
    nl["add_burst"](p, "Fluff", 2, 0.4, 28.0, MAT + "M_Impact_FluffPuffs", grid=2, speed=40.0, cone=80.0)
    nl["finish"](p)


def soft_armor_fluff():
    p = _ensure("NS_Impact_SoftArmor")
    nl["add_burst"](p, "Fluff", 2, 0.4, 24.0, MAT + "M_Impact_FluffPuffs", grid=2, speed=40.0, cone=80.0)
    nl["finish"](p)


def soft_armor_scraps():
    p = _ensure("NS_Impact_SoftArmor")
    nl["add_burst"](p, "Scraps", 3, 0.45, 9.0, MAT + "M_Impact_ClothScraps", grid=2, speed=180.0, cone=65.0, gravity=True)
    nl["finish"](p)


def shield_break_shards():
    p = _ensure("NS_ShieldBreak", FX + "NS_Impact_EnergyShield")   # синие искры как основа
    nl["add_burst"](p, "Shards", 20, 0.35, 16.0, MAT + "M_Shield_Shards", grid=4, speed=420.0, cone=180.0)
    nl["finish"](p)


def shield_break_ring():
    p = _ensure("NS_ShieldBreak", FX + "NS_Impact_EnergyShield")   # синие искры как основа
    nl["add_burst"](p, "Ring", 1, 0.25, 160.0, MAT + "M_Shield_Ring")
    nl["finish"](p)


FLESH = FX + "NS_Impact_Flesh"


def flesh_create():
    """Кровь: своя система. Берём копию пластины и убираем из неё всё, остаётся пустая оболочка."""
    if not EAL.does_asset_exist(FLESH):
        print("CREATED:", FLESH, bool(EAL.duplicate_asset(FX + "NS_Impact_ArmorPlate", FLESH)))
        EAL.save_asset(FLESH, only_if_is_dirty=False)
    names = nl["emitter_names"](FLESH)
    if "Sparks" in names:
        nl["remove_emitter"](FLESH, "Sparks")
    print("EMITTERS:", nl["emitter_names"](FLESH))


def flesh_strip():
    for n in nl["emitter_names"](FLESH):
        if n in ("Debris", "Fluff", "Sparks"):
            nl["remove_emitter"](FLESH, n)
    print("EMITTERS:", nl["emitter_names"](FLESH))
    nl["finish"](FLESH)


def flesh_user():
    nl["add_user_float"](FLESH, "ImpactScale", 1.0, "Size from damage, written by AShooterWeapon")
    nl["finish"](FLESH)


def flesh_burst():
    nl["add_burst"](FLESH, "Burst", 1, 0.09, 34.0, MAT + "M_Blood_Burst", grid=2)
    nl["finish"](FLESH)


def flesh_drops():
    nl["add_burst"](FLESH, "Drops", 6, 0.42, 9.0, MAT + "M_Blood_Drops", grid=4, speed=220.0, cone=75.0, gravity=True)
    nl["set_random"](FLESH, "Drops", "ParticleSpawnScript", "AddVelocityInCone", "Velocity Strength", 140.0, 330.0)
    nl["finish"](FLESH)


def flesh_mist():
    nl["add_burst"](FLESH, "Mist", 2, 0.25, 30.0, MAT + "M_Blood_Mist", grid=2, speed=30.0, cone=80.0)
    nl["finish"](FLESH)


def flesh_scale_one():
    nl["scale_by_user"](FLESH, "Burst", "ParticleSpawnScript", "InitializeParticle", "Uniform Sprite Size", 30.0, 38.0)
    nl["finish"](FLESH)


def flesh_scale_rest():
    nl["scale_by_user"](FLESH, "Drops", "ParticleSpawnScript", "InitializeParticle", "Uniform Sprite Size", 6.0, 12.0)
    nl["scale_by_user"](FLESH, "Mist", "ParticleSpawnScript", "InitializeParticle", "Uniform Sprite Size", 24.0, 34.0)
    nl["finish"](FLESH)
