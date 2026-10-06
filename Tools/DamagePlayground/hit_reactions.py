"""Рывки от попаданий: монтажи из аддитивных MM_HitReact_* и раскладка по грунту.

Анимации аддитивные (local space). Отдельный узел Apply Additive им не нужен: узел Slot сам
накладывает аддитивный монтаж поверх входной позы. В ABP_AR02_LPSP_Test слот DefaultSlot стоит в
середине графа, до IK рук, поэтому хват после рывка пересчитывается.

Монтаж: быстрый вход, короткий выход, скорость подобрана так, чтобы рывок укладывался в ~0.7 с.
"""

import json

import unreal

EAL = unreal.EditorAssetLibrary
DIR = "/Game/Characters/Mannequins/Anims/Rifle/HitReact"
BLEND_IN, BLEND_OUT, TARGET_LEN = 0.03, 0.15, 0.7

# монтаж: (секвенция, куда кладём)
ROWS = (
    ("AM_HitReact_Front_Lgt_01", "MM_HitReact_Front_Lgt_01", "light"),
    ("AM_HitReact_Front_Lgt_03", "MM_HitReact_Front_Lgt_03", "light"),
    ("AM_HitReact_Left_Lgt_02", "MM_HitReact_Left_Lgt_02", "left"),
    ("AM_HitReact_Right_Lgt_04", "MM_HitReact_Right_Lgt_04", "right"),
    ("AM_HitReact_Front_Med_01", "MM_HitReact_Front_Med_01", "medium"),
    ("AM_HitReact_Front_Med_02", "MM_HitReact_Front_Med_02", "medium"),
    ("AM_HitReact_Front_Hvy_01", "MM_HitReact_Front_Hvy_01", "heavy"),
    ("AM_HitReact_Back_Med_01", "MM_HitReact_Back_Med_01", "back"),
)
ARRAYS = {"light": "hit_reactions_light", "medium": "hit_reactions_medium", "heavy": "hit_reactions_heavy",
          "back": "hit_reactions_back", "left": "hit_reactions_left", "right": "hit_reactions_right"}
GRUNT = "/Game/Variant_Shooter/Blueprints/AI/BPs/BP_ShooterNPC"


def montages():
    at = unreal.AssetToolsHelpers.get_asset_tools()
    for name, seq_name, _ in ROWS:
        path = DIR + "/" + name
        seq = unreal.load_asset(DIR + "/" + seq_name)
        if not seq:
            print("MISSING:", seq_name)
            continue
        if EAL.does_asset_exist(path):
            m = unreal.load_asset(path)
        else:
            fac = unreal.AnimMontageFactory()
            fac.set_editor_property("source_animation", seq)
            fac.set_editor_property("target_skeleton", seq.get_skeleton())
            m = at.create_asset(name, DIR, unreal.AnimMontage, fac)
            print("CREATED:", path)
        for prop, t in (("blend_in", BLEND_IN), ("blend_out", BLEND_OUT)):
            b = m.get_editor_property(prop)
            b.set_editor_property("blend_time", t)
            m.set_editor_property(prop, b)
        m.set_editor_property("rate_scale", max(1.0, seq.get_play_length() / TARGET_LEN))
        slots = [str(t.get_editor_property("slot_name")) for t in m.get_editor_property("slot_anim_tracks")]
        print("MODIFIED:", name, "len %.2f" % m.get_play_length(), "rate %.2f" % m.get_editor_property("rate_scale"),
              "slots", slots, "| SAVED:", EAL.save_asset(path, only_if_is_dirty=False))


def assign():
    """Раскладка по массивам грунта. Нужна сборка с полями HitReactions*."""
    if unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world():
        print("PIE_RUNNING: nothing changed")
        return
    cdo = unreal.get_default_object(EAL.load_blueprint_class(GRUNT))
    pools = {k: [] for k in ARRAYS}
    for name, _, where in ROWS:
        pools[where].append(unreal.load_asset(DIR + "/" + name))
    for where, prop in ARRAYS.items():
        cdo.set_editor_property(prop, pools[where])
        print("MODIFIED: BP_ShooterNPC", prop, [m.get_name() for m in cdo.get_editor_property(prop)])
    unreal.ToolsetRegistry.execute_tool("editor_toolset.toolsets.blueprint.BlueprintTools", "compile_blueprint",
                                        json.dumps({"blueprint": {"refPath": GRUNT + ".BP_ShooterNPC"}}))
    print("SAVED:", EAL.save_asset(GRUNT, only_if_is_dirty=False))
