# build_assets.py
# Editor setup for the grenades (Docs/Grenades_Plan_2026-10-14.md). Idempotent: every step checks
# before it creates. Run one step at a time:
#   Tools/ue_step.sh Grenades build_assets <step>
# Steps: root_motion, knockdown_montages, stun_montages, inputs, projectiles, definitions,
#        player, enemy, microwave
import unreal

ANIM_KD = "/Game/Variant_Shooter/Anims/Knockdown"
ANIM_STUN = "/Game/Variant_Shooter/Anims/Stuns"
OUT_ANIM = "/Game/Variant_Shooter/Anims/Grenade"
OUT_BP = "/Game/Variant_Shooter/Blueprints/Grenades"
SKELETON = "/Game/Frank_Slash_Pack/Frank_Damages/Skeletal_Meshes/SK_Mannequin_Skeleton"
INPUT_DIR = "/Game/Variant_Shooter/Input/Actions"
IMC = "/Game/Variant_Shooter/Input/IMC_Weapons"
GRENADE_KEY = "G"
CYCLE_KEY = "H"

EAL = unreal.EditorAssetLibrary
MS = unreal.AnimMontageService

# Fall -> the get-up that starts in the pose it ends in (inspect_anims: 01 and 05 end on the back,
# 02 and 04 face down; 03 ends turned 36 degrees off its get-up and is left out).
KNOCKDOWNS = {
    "01": "Frank_Damage_Getup02_L",
    "05": "Frank_Damage_Getup02_L",
    "02": "Frank_Damage_Getup01_P",
    "04": "Frank_Damage_Getup01_P",
}
# 05 is left out: in it the body goes down on its back and lies there, which is a fall, not a stun
# [author, 2026-10-14]; the author's own head knockdown is made of it (AM_Knockdown_Front).
STUNS = ["01", "02", "04"]


def _save(path):
    a = unreal.load_asset(path)
    if a:
        a.modify()
    print("SAVED:", path, EAL.save_asset(path, only_if_is_dirty=False))


def root_motion():
    """The travel of the falls and stuns is in the root bone: turn root motion on, as the author's
    own _RM copies of Stun05 have it. Without it the body pops back to the capsule at the get-up."""
    reg = unreal.AssetRegistryHelpers.get_asset_registry()
    for folder in (ANIM_KD, ANIM_STUN):
        for ad in reg.get_assets_by_path(folder, recursive=False):
            seq = ad.get_asset()
            if not isinstance(seq, unreal.AnimSequence):
                continue
            if not seq.get_editor_property("enable_root_motion"):
                seq.set_editor_property("enable_root_motion", True)
                seq.set_editor_property("root_motion_root_lock", unreal.RootMotionRootLock.REF_POSE)
                print("MODIFIED: root motion on", seq.get_path_name())
                _save(seq.get_path_name().split(".")[0])


def _new_montage(name):
    path = OUT_ANIM + "/" + name
    if EAL.does_asset_exist(path):
        tracks = MS.list_slot_tracks(path)
        if len(tracks) > 0 and len(MS.list_anim_segments(path, 0)) > 0:
            print("EXISTS:", path)
            _save(path)
            return None
        print("EXISTS EMPTY, filling:", path)
        return path
    created = MS.create_empty_montage(SKELETON, OUT_ANIM, name)
    print("CREATED:", created)
    tracks = MS.list_slot_tracks(path)
    if len(tracks) == 0:
        MS.add_slot_track(path, "DefaultSlot")
    else:
        MS.set_slot_name(path, 0, "DefaultSlot")
    return path


def knockdown_montages():
    for num, getup in KNOCKDOWNS.items():
        path = _new_montage("AM_BlastKnockdown_" + num)
        if not path:
            continue
        fall = ANIM_KD + "/Frank_Damage_KnockDown_" + num
        up = ANIM_KD + "/" + getup
        fall_len = unreal.load_asset(fall).get_play_length()
        MS.add_anim_segment(path, 0, fall, 0.0)
        MS.add_anim_segment(path, 0, up, fall_len)
        MS.set_blend_in(path, 0.1)
        MS.set_blend_out(path, 0.25)
        MS.set_enable_root_motion_translation(path, True)
        print("  segments:", [x.export_text() for x in MS.list_anim_segments(path, 0)])
        _save(path)


def stun_montages():
    for num in STUNS:
        path = _new_montage("AM_FlashStun_" + num)
        if not path:
            continue
        parts = [ANIM_STUN + "/Frank_Damage_Stun%s_%s" % (num, p) for p in ("start", "loop", "end")]
        t = 0.0
        starts = []
        for part in parts:
            starts.append(t)
            MS.add_anim_segment(path, 0, part, t)
            t += unreal.load_asset(part).get_play_length()
        sections = [s.get_editor_property("section_name") for s in MS.list_sections(path)]
        print("  sections before:", sections)
        if "Default" in [str(s) for s in sections]:
            MS.rename_section(path, "Default", "Start")
        elif "Start" not in [str(s) for s in sections]:
            MS.add_section(path, "Start", 0.0)
        MS.add_section(path, "Loop", starts[1])
        MS.add_section(path, "End", starts[2])
        MS.set_next_section(path, "Start", "Loop")
        MS.set_next_section(path, "Loop", "End")
        MS.set_blend_in(path, 0.1)
        MS.set_blend_out(path, 0.25)
        print("  sections:", [(str(s.get_editor_property("section_name")), s.get_editor_property("start_time")) for s in MS.list_sections(path)])
        _save(path)


def inputs():
    for name in ("IA_Grenade", "IA_GrenadeCycle"):
        path = INPUT_DIR + "/" + name
        if not EAL.does_asset_exist(path):
            print("CREATED:", unreal.InputService.create_action(name, INPUT_DIR, "Boolean"))
    imc = unreal.load_asset(IMC)
    have = [(str(m.get_editor_property("key").get_editor_property("key_name")), m.get_editor_property("action").get_name())
            for m in imc.get_editor_property("default_key_mappings").get_editor_property("mappings")]
    for key, action in ((GRENADE_KEY, "IA_Grenade"), (CYCLE_KEY, "IA_GrenadeCycle")):
        if (key, action) in have:
            print("EXISTS: mapping", key, action)
            continue
        print("ADDED: mapping", key, action, unreal.InputService.add_key_mapping(IMC, INPUT_DIR + "/" + action, key))
    _save(IMC)


def _make_bp(name, parent_path):
    path = OUT_BP + "/" + name
    if EAL.does_asset_exist(path):
        print("EXISTS:", path)
        return unreal.load_asset(path)
    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", unreal.load_class(None, parent_path))
    bp = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, OUT_BP, unreal.Blueprint, factory)
    print("CREATED:", path)
    return bp


def projectiles():
    mesh = unreal.load_asset("/Game/InfimaGames/ArtCore/Weapons/Grenades/Meshes/SM_IG_Grenade_Frag")
    for name in ("BP_Grenade_Frag", "BP_Grenade_Flash"):
        bp = _make_bp(name, "/Script/Polarity.GrenadeProjectile")
        unreal.BlueprintEditorLibrary.compile_blueprint(bp)
        cdo = unreal.get_default_object(bp.generated_class())
        comp = cdo.get_editor_property("mesh")
        comp.set_editor_property("static_mesh", mesh)
        print("MODIFIED:", name, "mesh", comp.get_editor_property("static_mesh"))
        unreal.BlueprintEditorLibrary.compile_blueprint(bp)
        _save(OUT_BP + "/" + name)


def _make_da(name, cls):
    path = OUT_BP + "/" + name
    if EAL.does_asset_exist(path):
        return unreal.load_asset(path)
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", cls)
    da = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, OUT_BP, cls, factory)
    print("CREATED:", path)
    return da


def definitions():
    smoke = unreal.load_asset("/Game/Variant_Shooter/Blueprints/Abilities/DA_Ability_SmokeScreen")
    copied = ("aim_start_montage", "aim_loop_montage", "throw_montage", "throw_release_time", "quick_start_play_rate",
              "throw_play_rate", "montage_overlap", "aim_preview_delay", "throw_origin_offset", "aim_converge_distance")
    icon = unreal.load_asset("/Game/InfimaGames/ArtCore/Weapons/Grenades/Icons/T_IG_HUD_Grenade_Frag_D")
    bounce = unreal.load_asset("/Game/Audio/MetaSounds/sfx_Weapon_GrenadeImpact_nl_meta")
    att = unreal.load_asset("/Game/Audio/AttenuationPresets/ATT_Grenade")
    specs = (
        ("DA_Grenade_Frag", unreal.GrenadeDefinition_Frag, "BP_Grenade_Frag", "FRAG",
         "/Game/Effects/Particles/Explosion/NS_Grenade_Explosion", "/Game/Audio/MetaSounds/sfx_Weapon_GrenadeExplosion_nl_meta"),
        ("DA_Grenade_Flash", unreal.GrenadeDefinition_Flash, "BP_Grenade_Flash", "FLASH",
         None, "/Game/Audio/Sounds/Grenade/grenade_explode_whack_01"),
    )
    for name, cls, proj, label, fx, sound in specs:
        da = _make_da(name, cls)
        for p in copied:
            da.set_editor_property(p, smoke.get_editor_property(p))
        da.set_editor_property("projectile_class", unreal.load_class(None, OUT_BP + "/" + proj + "." + proj + "_C"))
        da.set_editor_property("display_name", label)
        da.set_editor_property("icon", icon)
        da.set_editor_property("bounce_sound", bounce)
        da.set_editor_property("detonation_attenuation", att)
        da.set_editor_property("detonation_sound", unreal.load_asset(sound))
        if fx:
            da.set_editor_property("detonation_fx", unreal.load_asset(fx))
        print("MODIFIED:", name, "projectile", da.get_editor_property("projectile_class"), "fuse", da.get_editor_property("fuse_time"))
        _save(OUT_BP + "/" + name)


def _grenade_component(bp_path):
    bp = unreal.load_asset(bp_path)
    cdo = unreal.get_default_object(bp.generated_class())
    return bp, cdo.get_editor_property("grenade_component")


def player():
    frag = unreal.load_asset(OUT_BP + "/DA_Grenade_Frag")
    flash = unreal.load_asset(OUT_BP + "/DA_Grenade_Flash")
    for bp_path in ("/Game/Variant_Shooter/Blueprints/BP_ShooterCharacter",
                    "/Game/Variant_Shooter/Tests/LPSP_AR02/BP_ShooterCharacter_AR02_Test"):
        bp, comp = _grenade_component(bp_path)
        comp.set_editor_property("grenade_action", unreal.load_asset(INPUT_DIR + "/IA_Grenade"))
        comp.set_editor_property("grenade_cycle_action", unreal.load_asset(INPUT_DIR + "/IA_GrenadeCycle"))
        stock = []
        for t in (frag, flash):
            s = unreal.GrenadeStock()
            s.set_editor_property("type", t)
            s.set_editor_property("count", 2)
            stock.append(s)
        comp.set_editor_property("starting_grenades", stock)
        unreal.BlueprintEditorLibrary.compile_blueprint(bp)
        _, comp = _grenade_component(bp_path)
        print("MODIFIED:", bp_path, comp.get_editor_property("grenade_action"), len(comp.get_editor_property("starting_grenades")))
        _save(bp_path)


def enemy():
    if unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world():
        print("PIE is running, stop it first"); return
    path = "/Game/Variant_Shooter/Blueprints/AI/BPs/BP_ShooterNPC"
    bp = unreal.load_asset(path)
    cdo = unreal.get_default_object(bp.generated_class())
    kd = [unreal.load_asset(OUT_ANIM + "/AM_BlastKnockdown_" + n) for n in KNOCKDOWNS]
    st = [unreal.load_asset(OUT_ANIM + "/AM_FlashStun_" + n) for n in STUNS]
    cdo.set_editor_property("blast_knockdown_montages_front", kd)
    cdo.set_editor_property("flash_stun_montages", st)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    cdo = unreal.get_default_object(bp.generated_class())
    print("MODIFIED:", path, len(cdo.get_editor_property("blast_knockdown_montages_front")), len(cdo.get_editor_property("flash_stun_montages")))
    _save(path)


# ==================== Flash: the burn on the eye, the deafness ====================

BURN_HLSL = r"""
// Afterimage of a flash looked at, as after looking at the sun: a bright warm spot with a glow that
// washes the picture, turning within a fraction of the burn into a dim, tinted negative spot that
// fades. Stays where the flash was on the screen. Center in viewport UV, Size in screen heights.
float2 d = UV - Center.xy;
d.x *= Aspect;
float r = length(d) / max(Size, 0.001);
float core = saturate(1.0 - r);
core = core * core * (3.0 - 2.0 * core);
float halo = exp(-r * r * 2.5);
float t = saturate(Age);
float s = saturate(Strength);
float bright = saturate(1.0 - t * 4.0);
float ghost = saturate((t - 0.08) * 3.0) * pow(1.0 - t, 0.7);
float3 warm = float3(1.0, 0.96, 0.85);
float3 col = Scene;
float wash = s * s * saturate(1.0 - t * 5.0) * 0.35;
col = lerp(col, warm, wash);
col += warm * (core * 2.0 + halo * 0.5) * s * bright;
float3 tint = float3(0.55, 0.38, 0.62);
col = lerp(col, col * tint, saturate(core * 1.4 + halo * 0.3) * ghost * sqrt(s) * 1.4);
float lum = dot(col, float3(0.3, 0.59, 0.11));
col = lerp(col, lum.xxx, saturate(halo) * s * (1.0 - t) * 0.5);
return min(col, 1.0);
"""

BURN_MAT = OUT_BP + "/M_FlashBurn"


def burn_material():
    mel = unreal.MaterialEditingLibrary
    if EAL.does_asset_exist(BURN_MAT):
        mat = unreal.load_asset(BURN_MAT)
        mel.delete_all_material_expressions(mat)
        print("REBUILT:", BURN_MAT)
    else:
        mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset("M_FlashBurn", OUT_BP, unreal.Material, unreal.MaterialFactoryNew())
        print("CREATED:", BURN_MAT)
    mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_POST_PROCESS)
    mat.set_editor_property("blendable_location", unreal.BlendableLocation.BL_SCENE_COLOR_AFTER_TONEMAPPING)

    custom = mel.create_material_expression(mat, unreal.MaterialExpressionCustom, -300, 0)
    custom.set_editor_property("code", BURN_HLSL)
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    names = ["UV", "Scene", "Center", "Size", "Strength", "Age", "Aspect"]
    inputs = []
    for n in names:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", n)
        inputs.append(ci)
    custom.set_editor_property("inputs", inputs)

    uv = mel.create_material_expression(mat, unreal.MaterialExpressionScreenPosition, -800, -200)
    scene = mel.create_material_expression(mat, unreal.MaterialExpressionSceneTexture, -800, -60)
    scene.set_editor_property("scene_texture_id", unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0)
    center = mel.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -800, 80)
    center.set_editor_property("parameter_name", "Center")
    center.set_editor_property("default_value", unreal.LinearColor(0.5, 0.5, 0.0, 0.0))
    params = {}
    for i, (n, v) in enumerate((("Size", 0.3), ("Strength", 0.0), ("Age", 0.0))):
        p = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -800, 220 + i * 90)
        p.set_editor_property("parameter_name", n)
        p.set_editor_property("default_value", v)
        params[n] = p
    view = mel.create_material_expression(mat, unreal.MaterialExpressionViewSize, -1100, 500)
    mx = mel.create_material_expression(mat, unreal.MaterialExpressionComponentMask, -950, 480)
    mx.set_editor_property("r", True); mx.set_editor_property("g", False); mx.set_editor_property("b", False); mx.set_editor_property("a", False)
    my = mel.create_material_expression(mat, unreal.MaterialExpressionComponentMask, -950, 560)
    my.set_editor_property("r", False); my.set_editor_property("g", True); my.set_editor_property("b", False); my.set_editor_property("a", False)
    div = mel.create_material_expression(mat, unreal.MaterialExpressionDivide, -800, 520)
    mel.connect_material_expressions(view, "", mx, "")
    mel.connect_material_expressions(view, "", my, "")
    mel.connect_material_expressions(mx, "", div, "A")
    mel.connect_material_expressions(my, "", div, "B")

    ok = [
        mel.connect_material_expressions(uv, "ViewportUV", custom, "UV"),
        mel.connect_material_expressions(scene, "Color", custom, "Scene"),
        mel.connect_material_expressions(center, "RGBA", custom, "Center"),
        mel.connect_material_expressions(params["Size"], "", custom, "Size"),
        mel.connect_material_expressions(params["Strength"], "", custom, "Strength"),
        mel.connect_material_expressions(params["Age"], "", custom, "Age"),
        mel.connect_material_expressions(div, "", custom, "Aspect"),
        mel.connect_material_property(custom, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR),
    ]
    print("connections:", ok)
    mel.recompile_material(mat)
    _save(BURN_MAT)


def burn_check():
    mat = unreal.load_asset(BURN_MAT)
    st = unreal.MaterialEditingLibrary.get_statistics(mat)
    print("pixel instructions:", st.get_editor_property("num_pixel_shader_instructions"))


DEAF_MIX = OUT_BP + "/SMix_FlashDeaf"


def deaf_mix():
    if EAL.does_asset_exist(DEAF_MIX):
        mix = unreal.load_asset(DEAF_MIX)
    else:
        mix = unreal.AssetToolsHelpers.get_asset_tools().create_asset("SMix_FlashDeaf", OUT_BP, unreal.SoundMix, unreal.SoundMixFactory())
        print("CREATED:", DEAF_MIX)
    effects = []
    # The engine's Master is where every sound with no class of its own goes (most of the project's);
    # SFX for the ones that name it. The ringing plays in UI, which is under neither.
    for cls_path in ("/Engine/EngineSounds/Master", "/Game/Audio/Classes/SFX", "/Game/Audio/Classes/Voice"):
        c = unreal.load_asset(cls_path)
        if not c:
            print("no class", cls_path); continue
        a = unreal.SoundClassAdjuster()
        a.set_editor_property("sound_class_object", c)
        a.set_editor_property("volume_adjuster", 0.3)
        a.set_editor_property("low_pass_filter_frequency", 700.0)
        a.set_editor_property("apply_to_children", True)
        effects.append(a)
    mix.set_editor_property("sound_class_effects", effects)
    mix.set_editor_property("fade_in_time", 0.05)
    mix.set_editor_property("fade_out_time", 2.0)
    mix.set_editor_property("duration", -1.0)
    print("MODIFIED:", DEAF_MIX, len(effects), "classes")
    _save(DEAF_MIX)


def flash_assets():
    """After the rebuild that brought the new fields: the burn, the deaf mix and the ringing on the flash."""
    da = unreal.load_asset(OUT_BP + "/DA_Grenade_Flash")
    da.set_editor_property("player_burn_material", unreal.load_asset(BURN_MAT))
    da.set_editor_property("player_deaf_mix", unreal.load_asset(DEAF_MIX))
    ring = unreal.load_asset(OUT_BP + "/Sounds/S_FlashRing")
    if ring:
        da.set_editor_property("player_ring_sound", ring)
    print("MODIFIED: DA_Grenade_Flash burn", da.get_editor_property("player_burn_material"),
          "mix", da.get_editor_property("player_deaf_mix"), "ring", da.get_editor_property("player_ring_sound"))
    _save(OUT_BP + "/DA_Grenade_Flash")


SOUND_SRC = "C:/Users/Professional/Documents/Unreal Projects/Polarity_Main5_8/Art/Sounds/Flashbang"
SOUND_DST = OUT_BP + "/Sounds"


def sounds():
    """Import every candidate (Art/Sounds/Flashbang/SOURCES.md); the ringing ones go into the UI class,
    which the deaf mix leaves alone. Then the first bang and the first ring go onto the flash."""
    import os
    tasks = []
    for f in sorted(os.listdir(SOUND_SRC)):
        if not f.lower().endswith((".mp3", ".wav")):
            continue
        name = "S_Flash_" + os.path.splitext(f)[0]
        if EAL.does_asset_exist(SOUND_DST + "/" + name):
            print("EXISTS:", name)
            continue
        t = unreal.AssetImportTask()
        t.set_editor_property("filename", SOUND_SRC + "/" + f)
        t.set_editor_property("destination_path", SOUND_DST)
        t.set_editor_property("destination_name", name)
        t.set_editor_property("automated", True)
        t.set_editor_property("save", True)
        tasks.append(t)
    if tasks:
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
        for t in tasks:
            print("IMPORTED:", t.get_editor_property("destination_name"), list(t.get_editor_property("imported_object_paths")))
    ui = unreal.load_asset("/Game/Audio/Classes/UI")
    reg = unreal.AssetRegistryHelpers.get_asset_registry()
    for ad in reg.get_assets_by_path(SOUND_DST, recursive=False):
        s = ad.get_asset()
        if isinstance(s, unreal.SoundWave) and "_ring" in s.get_name():
            s.set_editor_property("sound_class_object", ui)
            print("MODIFIED:", s.get_name(), "class UI, length", s.get_editor_property("duration"))
            _save(s.get_path_name().split(".")[0])
        elif isinstance(s, unreal.SoundWave):
            print("bang:", s.get_name(), "length", s.get_editor_property("duration"))
    da = unreal.load_asset(OUT_BP + "/DA_Grenade_Flash")
    bang = unreal.load_asset(SOUND_DST + "/S_Flash_bang1_modusmogulus_Flashbang")
    ring = unreal.load_asset(SOUND_DST + "/S_Flash_ring1_Klangkobold_Tinnitus")
    if bang:
        da.set_editor_property("detonation_sound", bang)
    if ring:
        da.set_editor_property("player_ring_sound", ring)
    da.set_editor_property("player_burn_material", unreal.load_asset(BURN_MAT))
    da.set_editor_property("player_deaf_mix", unreal.load_asset(DEAF_MIX))
    print("MODIFIED: DA_Grenade_Flash bang", bang, "ring", ring, "burn", da.get_editor_property("player_burn_material"),
          "mix", da.get_editor_property("player_deaf_mix"))
    _save(OUT_BP + "/DA_Grenade_Flash")


VITALS = "/Game/Variant_Shooter/UI/Widgets/HUD/Registry/WBP_Vitals"


def _vw(name):
    return unreal.load_object(None, "{}.{}:WidgetTree.{}".format(VITALS, VITALS.split("/")[-1], name))


def hud_survey():
    for h in unreal.WidgetService.get_hierarchy(VITALS):
        print(h.export_text()[:220])


def hud():
    """A debug line under the vitals: which grenade G throws and how many, the others on H.
    Filled by UHudVitalsWidget (GrenadeText, found by name)."""
    names = [h.get_editor_property("widget_name") for h in unreal.WidgetService.get_hierarchy(VITALS)]
    if "GrenadeText" not in names:
        unreal.WidgetService.add_component(VITALS, "TextBlock", "GrenadeText", "BarRow", True)
        print("ADDED: GrenadeText")
    text = _vw("GrenadeText")
    if not text:
        print("no GrenadeText after adding"); return
    text.set_editor_property("text", unreal.Text("[G] FRAG x2   [H] FLASH x2"))
    text.slot.set_horizontal_alignment(unreal.HorizontalAlignment.H_ALIGN_LEFT)
    text.slot.set_vertical_alignment(unreal.VerticalAlignment.V_ALIGN_BOTTOM)
    font = _vw("Caption").get_editor_property("font")
    font.set_editor_property("size", 13)
    text.set_editor_property("font", font)
    text.set_editor_property("render_transform", unreal.WidgetTransform(translation=unreal.Vector2D(0.0, 34.0)))
    res = unreal.ToolsetRegistry.execute_tool("editor_toolset.toolsets.blueprint.BlueprintTools", "compile_blueprint",
                                             '{"blueprint": {"refPath": "%s.%s"}}' % (VITALS, VITALS.split("/")[-1]))
    print("compile", res.is_complete, res.error)
    print("SAVED:", EAL.save_asset(VITALS, False))


# ==================== Microwave ====================

def microwave():
    """The third type (UGrenadeDefinition_Microwave): its flying Blueprint, its asset with the throw
    copied off the flash, and two of them in the players' starting stock. Leaves frag and flash alone."""
    flash_bp = unreal.load_asset(OUT_BP + "/BP_Grenade_Flash")
    bp = _make_bp("BP_Grenade_Microwave", "/Script/Polarity.GrenadeProjectile")
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    mesh = unreal.get_default_object(flash_bp.generated_class()).get_editor_property("mesh").get_editor_property("static_mesh")
    unreal.get_default_object(bp.generated_class()).get_editor_property("mesh").set_editor_property("static_mesh", mesh)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    _save(OUT_BP + "/BP_Grenade_Microwave")

    flash = unreal.load_asset(OUT_BP + "/DA_Grenade_Flash")
    da = _make_da("DA_Grenade_Microwave", unreal.GrenadeDefinition_Microwave)
    for p in ("aim_start_montage", "aim_loop_montage", "throw_montage", "throw_release_time", "quick_start_play_rate",
              "throw_play_rate", "montage_overlap", "aim_preview_delay", "throw_origin_offset", "aim_converge_distance",
              "icon", "bounce_sound", "detonation_attenuation", "throw_speed", "gravity_scale", "max_per_cell"):
        da.set_editor_property(p, flash.get_editor_property(p))
    da.set_editor_property("projectile_class", unreal.load_class(None, OUT_BP + "/BP_Grenade_Microwave.BP_Grenade_Microwave_C"))
    da.set_editor_property("display_name", "MICROWAVE")
    # Placeholder until it has a sound of its own: a dull thump, not the flash's bang.
    da.set_editor_property("detonation_sound", unreal.load_asset("/Game/Audio/Sounds/Grenade/grenade_explode_whack_01"))
    print("MODIFIED: DA_Grenade_Microwave projectile", da.get_editor_property("projectile_class"),
          "fuse", da.get_editor_property("fuse_time"), "radius", da.get_editor_property("max_radius"))
    _save(OUT_BP + "/DA_Grenade_Microwave")

    for bp_path in ("/Game/Variant_Shooter/Blueprints/BP_ShooterCharacter",
                    "/Game/Variant_Shooter/Tests/LPSP_AR02/BP_ShooterCharacter_AR02_Test"):
        char_bp, comp = _grenade_component(bp_path)
        stock = list(comp.get_editor_property("starting_grenades"))
        if any(s.get_editor_property("type") == da for s in stock):
            print("EXISTS: microwave in", bp_path)
            continue
        s = unreal.GrenadeStock()
        s.set_editor_property("type", da)
        s.set_editor_property("count", 2)
        stock.append(s)
        comp.set_editor_property("starting_grenades", stock)
        unreal.BlueprintEditorLibrary.compile_blueprint(char_bp)
        _, comp = _grenade_component(bp_path)
        print("MODIFIED:", bp_path, [(x.get_editor_property("type").get_name(), x.get_editor_property("count"))
                                     for x in comp.get_editor_property("starting_grenades")])
        _save(bp_path)


def fpv():
    """The FPV drone (UGrenadeDefinition_FPV): its asset, the throw copied off the frag, the project's
    kamikaze as the drone, and two in the players' starting stock. Leaves the other types alone."""
    frag = unreal.load_asset(OUT_BP + "/DA_Grenade_Frag")
    da = _make_da("DA_Grenade_FPV", unreal.GrenadeDefinition_FPV)
    for p in ("aim_start_montage", "aim_loop_montage", "throw_montage", "throw_release_time", "quick_start_play_rate",
              "throw_play_rate", "montage_overlap", "aim_preview_delay", "throw_origin_offset", "aim_converge_distance"):
        da.set_editor_property(p, frag.get_editor_property(p))
    da.set_editor_property("drone_class", unreal.load_class(None, "/Game/Variant_Shooter/Blueprints/AI/BPs/BP_KamikazeDrone.BP_KamikazeDrone_C"))
    da.set_editor_property("display_name", "FPV")
    print("MODIFIED: DA_Grenade_FPV drone", da.get_editor_property("drone_class"), "speed", da.get_editor_property("launch_speed"),
          "per cell", da.get_editor_property("max_per_cell"))
    _save(OUT_BP + "/DA_Grenade_FPV")

    bp_path = "/Game/Variant_Shooter/Blueprints/BP_ShooterCharacter"
    char_bp, comp = _grenade_component(bp_path)
    stock = list(comp.get_editor_property("starting_grenades"))
    if any(s.get_editor_property("type") == da for s in stock):
        print("EXISTS: FPV in", bp_path)
        return
    s = unreal.GrenadeStock()
    s.set_editor_property("type", da)
    s.set_editor_property("count", 2)
    stock.append(s)
    comp.set_editor_property("starting_grenades", stock)
    unreal.BlueprintEditorLibrary.compile_blueprint(char_bp)
    _, comp = _grenade_component(bp_path)
    print("MODIFIED:", bp_path, [(x.get_editor_property("type").get_name(), x.get_editor_property("count"))
                                 for x in comp.get_editor_property("starting_grenades")])
    _save(bp_path)


def stock_check():
    """What each player Blueprint actually starts with: the child (the one the game mode spawns) must
    inherit the parent's list, not keep an old copy of its own."""
    for bp_path in ("/Game/Variant_Shooter/Blueprints/BP_ShooterCharacter",
                    "/Game/Variant_Shooter/Tests/LPSP_AR02/BP_ShooterCharacter_AR02_Test"):
        _, comp = _grenade_component(bp_path)
        print(bp_path.split("/")[-1], [(x.get_editor_property("type").get_name(), x.get_editor_property("count"))
                                       for x in comp.get_editor_property("starting_grenades")])
