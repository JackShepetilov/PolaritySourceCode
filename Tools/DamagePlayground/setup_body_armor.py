"""Броня зоной, каска, лут от целости, тело с подсветкой (2026-10-12).
План: Docs/Damage_Model_Rework_Plan_2026-10-03.md, раздел 8. Код: UHelmetComponent,
UShieldFieldComponent (CoveredBones, UncoveredDamageMultiplier), UDamageType_RangedLight / Piercing,
UStrippableArmorComponent (BrokenArmorFraction, BodyGlowMaterial), AShooterNPC::CorpseSeconds.

    probe()          что сейчас стоит: кости физассета, броня, стволы и их типы урона, монтажи
    step_material()  M_ArmorLootGlow: оверлей тела, светится торс, пока броня не снята
    step_enemy()     BP_ShooterNPC: кости брони, каска, тело на 180 с, подсветка
    step_weapons()   типы урона стволам: пистолеты и ПП слабо по броне, снайперки и пулемёты сильно

Запуск: Tools/dp_step.sh setup_body_armor <функция>. Идемпотентно.
"""

import json
import unreal

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
BT = "editor_toolset.toolsets.blueprint.BlueprintTools"

NPC = "/Game/Variant_Shooter/Blueprints/AI/BPs/BP_ShooterNPC"
GLOW_DIR = "/Game/Variant_Shooter/Materials"
GLOW = GLOW_DIR + "/M_ArmorLootGlow"

# Что закрыто бронёй [автор, 2026-10-12]: всё, кроме головы, ног ниже бедра и рук ниже локтя.
COVERED = ("pelvis", "spine_01", "spine_02", "spine_03", "spine_04", "spine_05", "neck_01", "neck_02",
           "clavicle_l", "clavicle_r", "upperarm_l", "upperarm_r", "thigh_l", "thigh_r")
HEAD = ("head",)
CORPSE_SECONDS = 180.0


def log(msg):
    print("[ARMOR_SETUP] " + msg)


def _compile(path):
    ref = "{}.{}".format(path, path.split("/")[-1])
    res = unreal.ToolsetRegistry.execute_tool(BT, "compile_blueprint", json.dumps({"blueprint": {"refPath": ref}}))
    status = str(unreal.load_asset(path).get_editor_property("status"))
    return res.is_complete and not res.error, status


def _components(path):
    """(имя, объект-шаблон, handle) для компонентов, добавленных в блюпринте и унаследованных."""
    bp = unreal.load_asset(path)
    sds = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    lib = unreal.SubobjectDataBlueprintFunctionLibrary
    out = []
    handles = sds.k2_gather_subobject_data_for_blueprint(bp)
    for h in handles:
        obj = lib.get_object(sds.k2_find_subobject_data_from_handle(h))
        if obj:
            out.append((obj.get_name(), obj, h))
    return bp, sds, handles, out


def _find(comps, cls):
    for _name, obj, _h in comps:
        if isinstance(obj, cls):
            return obj
    return None


def _weapon_blueprints():
    """Пути блюпринтов стволов, по тегу реестра, без загрузки ассетов."""
    reg = unreal.AssetRegistryHelpers.get_asset_registry()
    flt = unreal.ARFilter(class_paths=[unreal.TopLevelAssetPath("/Script/Engine", "Blueprint")],
                          package_paths=["/Game/Variant_Shooter/Blueprints"], recursive_paths=True)
    out = []
    for a in reg.get_assets(flt):
        parent = str(a.get_tag_value("NativeParentClass") or "")
        if "ShooterWeapon" in parent and "Melee" not in parent:
            out.append((str(a.package_name), parent.split(".")[-1].rstrip("'")))
    return sorted(out)


def probe():
    cls = EAL.load_blueprint_class(NPC)
    cdo = unreal.get_default_object(cls)
    mesh = cdo.mesh.get_editor_property("skeletal_mesh_asset")
    phys = mesh.get_editor_property("physics_asset") if mesh else None
    try:
        bodies = [str(b.get_editor_property("bone_name")) for b in phys.get_editor_property("skeletal_body_setups")]
    except Exception as e:
        # В 5.8 массив тел физассета из питона не читается: имена сверять попаданием в PIE ([HITPART]).
        bodies = []
        log("bodies not readable: {}".format(str(e)[:80]))
    log("mesh {} physics {}".format(mesh.get_name() if mesh else None, phys.get_name() if phys else None))
    log("bodies: " + ", ".join(bodies))
    log("missing covered: {}".format([b for b in COVERED if b not in bodies]))
    log("corpse_seconds {} knockback_montage {}".format(cdo.get_editor_property("corpse_seconds"),
                                                        cdo.get_editor_property("knockback_montage")))
    log("default death mode {}".format(cdo.get_editor_property("default_death_config").get_editor_property("mode")))
    log("hard_armor_bones {}".format([str(b) for b in cdo.get_editor_property("hard_armor_bones")]))
    _bp, _sds, _handles, comps = _components(NPC)
    for name, obj, _h in comps:
        if isinstance(obj, (unreal.ShieldFieldComponent, unreal.StrippableArmorComponent, unreal.HelmetComponent)):
            log("component {} ({})".format(name, obj.get_class().get_name()))
    shield = _find(comps, unreal.ShieldFieldComponent)
    if shield:
        log("shield max {} covered {}".format(shield.get_editor_property("max_shield"),
                                              [str(b) for b in shield.get_editor_property("covered_bones")]))
    for path, parent in _weapon_blueprints():
        wcls = EAL.load_blueprint_class(path)
        wcdo = unreal.get_default_object(wcls)
        dt = wcdo.get_editor_property("hitscan_damage_type")
        log("weapon {} [{}] dmg {} type {}".format(path.split("/")[-1], parent, wcdo.get_editor_property("hitscan_damage"),
                                                   dt.get_name() if dt else None))
    reg = unreal.AssetRegistryHelpers.get_asset_registry()
    flt = unreal.ARFilter(class_paths=[unreal.TopLevelAssetPath("/Script/Engine", "AnimMontage"),
                                       unreal.TopLevelAssetPath("/Script/Engine", "AnimSequence")],
                          package_paths=["/Game"], recursive_paths=True)
    words = ("knock", "getup", "get_up", "falldown", "fall_down", "stun", "stagger", "prone")
    for a in reg.get_assets(flt):
        n = str(a.asset_name).lower()
        if any(w in n for w in words):
            log("anim {}".format(a.package_name))


def step_material():
    """Оверлей на тело: светится только торс. Маска по позиции вершины в реф-позе (PreSkinnedPosition),
    поэтому работает и на рэгдолле. Параметры TorsoZMin/Max и TorsoHalfWidth подгоняются под меш."""
    if EAL.does_asset_exist(GLOW):
        mat = unreal.load_asset(GLOW)
        MEL.delete_all_material_expressions(mat)
        log("MODIFIED: граф {} пересобирается".format(GLOW))
    else:
        mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset("M_ArmorLootGlow", GLOW_DIR, unreal.Material,
                                                                      unreal.MaterialFactoryNew())
        log("CREATED: " + GLOW)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("used_with_skeletal_mesh", True)

    def node(cls, x, y):
        return MEL.create_material_expression(mat, cls, x, y)

    def scalar(name, value, y):
        p = node(unreal.MaterialExpressionScalarParameter, -900, y)
        p.set_editor_property("parameter_name", name)
        p.set_editor_property("default_value", value)
        return p

    # PreSkinnedPosition живёт только в вершинном шейдере: в пиксельный его несёт интерполятор.
    raw_pos = node(unreal.MaterialExpressionPreSkinnedPosition, -1150, -200)
    pos = node(unreal.MaterialExpressionVertexInterpolator, -900, -200)
    if not MEL.connect_material_expressions(raw_pos, "", pos, ""):
        log("FAILED connect interpolator")
    zmin, zmax = scalar("TorsoZMin", 92.0, -100), scalar("TorsoZMax", 152.0, 0)
    half, feather = scalar("TorsoHalfWidth", 24.0, 100), scalar("Feather", 5.0, 200)
    amount, intensity = scalar("Amount", 1.0, 300), scalar("Intensity", 3.0, 400)
    colour = node(unreal.MaterialExpressionVectorParameter, -900, 500)
    colour.set_editor_property("parameter_name", "Colour")
    colour.set_editor_property("default_value", unreal.LinearColor(0.15, 0.55, 1.0, 1.0))
    fresnel = node(unreal.MaterialExpressionFresnel, -900, 700)
    fresnel.set_editor_property("exponent", 2.0)
    fresnel.set_editor_property("base_reflect_fraction", 0.25)
    time = node(unreal.MaterialExpressionTime, -900, 850)

    custom = node(unreal.MaterialExpressionCustom, -400, 100)
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    custom.set_editor_property("description", "ArmorGlow")
    names = ("P", "ZMin", "ZMax", "HalfW", "F", "Amount", "Intensity", "Colour", "Rim", "T")
    inputs = []
    for n in names:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", n)
        inputs.append(ci)
    custom.set_editor_property("inputs", inputs)
    custom.set_editor_property("code",
        "float z = smoothstep(ZMin - F, ZMin + F, P.z) * (1.0 - smoothstep(ZMax - F, ZMax + F, P.z));\n"
        "float x = 1.0 - smoothstep(HalfW - F, HalfW + F, abs(P.x));\n"
        "float pulse = 0.75 + 0.25 * sin(T * 4.0);\n"
        "float strength = lerp(0.25, 1.0, saturate(Amount));\n"
        "return Colour.rgb * (z * x * Rim * pulse * strength * Intensity);")
    for src, out_pin, name in ((pos, "", "P"), (zmin, "", "ZMin"), (zmax, "", "ZMax"), (half, "", "HalfW"),
                               (feather, "", "F"), (amount, "", "Amount"), (intensity, "", "Intensity"),
                               (colour, "", "Colour"), (fresnel, "", "Rim"), (time, "", "T")):
        ok = MEL.connect_material_expressions(src, out_pin, custom, name)
        if not ok:
            log("FAILED connect " + name)
    MEL.connect_material_property(custom, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.recompile_material(mat)
    EAL.save_asset(GLOW, False)
    log("SAVED: {}".format(GLOW))


def check_material():
    mat = unreal.load_asset(GLOW)
    stats = MEL.get_statistics(mat)
    log("material pixel instructions {}".format(stats.num_pixel_shader_instructions))


def step_enemy():
    bp, sds, handles, comps = _components(NPC)
    changed = []

    if not _find(comps, unreal.HelmetComponent):
        params = unreal.AddNewSubobjectParams()
        params.set_editor_property("parent_handle", handles[0])
        params.set_editor_property("blueprint_context", bp)
        params.set_editor_property("new_class", unreal.HelmetComponent)
        _handle, fail = sds.add_new_subobject(params)
        if fail and str(fail):
            log("FAILED to add Helmet: {}".format(fail))
        else:
            changed.append("+Helmet")
        bp, sds, handles, comps = _components(NPC)

    shield = _find(comps, unreal.ShieldFieldComponent)
    if shield is None:
        raise RuntimeError("у BP_ShooterNPC нет ShieldField")
    if [str(b) for b in shield.get_editor_property("covered_bones")] != list(COVERED):
        shield.set_editor_property("covered_bones", [unreal.Name(b) for b in COVERED])
        changed.append("covered_bones")

    helmet = _find(comps, unreal.HelmetComponent)
    if helmet and [str(b) for b in helmet.get_editor_property("head_bones")] != list(HEAD):
        helmet.set_editor_property("head_bones", [unreal.Name(b) for b in HEAD])
        changed.append("head_bones")

    strip = _find(comps, unreal.StrippableArmorComponent)
    glow = unreal.load_asset(GLOW)
    if strip and glow and strip.get_editor_property("body_glow_material") != glow:
        strip.set_editor_property("body_glow_material", glow)
        changed.append("body_glow_material")

    cls = EAL.load_blueprint_class(NPC)
    cdo = unreal.get_default_object(cls)
    if cdo.get_editor_property("corpse_seconds") != CORPSE_SECONDS:
        cdo.set_editor_property("corpse_seconds", CORPSE_SECONDS)
        changed.append("corpse_seconds")
    # Вид попадания: каска и торс читаются как пластина, остальное как мягкая броня.
    hard = list(COVERED) + list(HEAD)
    if [str(b) for b in cdo.get_editor_property("hard_armor_bones")] != hard:
        cdo.set_editor_property("hard_armor_bones", [unreal.Name(b) for b in hard])
        changed.append("hard_armor_bones")

    if changed:
        ok, status = _compile(NPC)
        saved = EAL.save_asset(NPC, False)
        log("MODIFIED: BP_ShooterNPC: {} | compile {} {} | saved {}".format(", ".join(changed), ok, status, saved))
    else:
        log("ok: BP_ShooterNPC")


# Слабо по броне: пистолеты и ПП. Сильно: снайперки и пулемёты. Остальное без правки (x1.0).
# Список заполняется по выводу probe(); ключ это имя блюпринта ствола.
# Выбор агента 2026-10-12, автор не подтверждал: только стволы, чей вид ясен из имени. Colibri,
# Striker-V, дробовики, Val, M14, Mk14EBR оставлены на x1.0.
LIGHT = ("BP_M1911", "BP_Viper-357", "BP_DGL50", "BP_X18", "BP_MPS5", "BP_PDW90",
         "BP_ShooterWeapon_Pistol", "BP_ShooterFunPistol", "BP_ShooterWavePistol")
PIERCING = ("BP_L96X", "BP_SVD", "BP_Kar98K", "BP_SniperKraber", "BP_Marksman", "BP_MGX5", "BP_LMG")


def step_weapons():
    light = unreal.load_class(None, "/Script/Polarity.DamageType_RangedLight")
    piercing = unreal.load_class(None, "/Script/Polarity.DamageType_RangedPiercing")
    for path, _parent in _weapon_blueprints():
        name = path.split("/")[-1]
        want = light if name in LIGHT else piercing if name in PIERCING else None
        if want is None:
            continue
        cls = EAL.load_blueprint_class(path)
        cdo = unreal.get_default_object(cls)
        if cdo.get_editor_property("hitscan_damage_type") == want:
            log("ok: {}".format(name))
            continue
        cdo.set_editor_property("hitscan_damage_type", want)
        ok, status = _compile(path)
        saved = EAL.save_asset(path, False)
        log("MODIFIED: {} -> {} | compile {} {} | saved {}".format(name, want.get_name(), ok, status, saved))
