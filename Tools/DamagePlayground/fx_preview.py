"""Снимок эффекта попадания в редакторе, чтобы агент видел то, что собрал.

Ставит временный NiagaraActor (тег FX_PREVIEW_TMP) над открытым уровнем, прогоняет симуляцию до
нужного момента и снимает вьюпорт в Saved/FxPreview/<имя>.png. Три вызова: spawn_*, shot_*, cleanup.
Правка ассета и снимок в ОДНОМ вызове дают пустой кадр (Docs/Gotchas/Niagara.md), поэтому раздельно.
PIE должен быть выключен.
"""

import base64
import io
import json
import os

import unreal

TAG = "FX_PREVIEW_TMP"
AT = unreal.Vector(-600.0, 0.0, 400.0)
OUT_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()) + "FxPreview/"


def _eas():
    return unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def _actor():
    found = [a for a in _eas().get_all_level_actors() if a.actor_has_tag(TAG)]
    return found[0] if found else None


def cleanup():
    n = 0
    for a in _eas().get_all_level_actors():
        if a.actor_has_tag(TAG):
            a.destroy_actor()
            n += 1
    print("DELETED: preview actors", n)


def _spawn(system):
    if unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world():
        print("PIE_RUNNING: no preview")
        return
    cleanup()
    act = _eas().spawn_actor_from_class(unreal.NiagaraActor, AT, unreal.Rotator(roll=0.0, pitch=0.0, yaw=135.0))
    act.set_editor_property("tags", [unreal.Name(TAG)])
    act.set_actor_label(TAG)
    act.niagara_component.set_asset(unreal.load_asset("/Game/Effects/Impacts/" + system))
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_set_viewport_realtime(True)
    print("SPAWNED:", system)


def _shot(name, t, scale=1.0, dist=150.0):
    act = _actor()
    if not act:
        print("NO_PREVIEW_ACTOR")
        return
    nc = act.niagara_component
    nc.deactivate()
    nc.set_variable_float("ImpactScale", scale)
    nc.activate(True)
    nc.advance_simulation_by_time(t, 1.0 / 120.0)
    args = {"captureTransform": {"location": {"x": AT.x - dist * 0.707, "y": AT.y - dist * 0.707, "z": AT.z},
                                 "rotation": {"pitch": 0.0, "yaw": 45.0, "roll": 0.0},
                                 "scale": {"x": 1.0, "y": 1.0, "z": 1.0}},
            "annotations": {"gridSpacing": 0, "gridExtent": 0, "gridHeight": 0, "maxLabelDistance": 0,
                            "classFilter": {"refPath": "/Script/Engine.Actor"}, "maxLabels": 0}}
    res = unreal.ToolsetRegistry.execute_tool("EditorToolset.EditorAppToolset", "CaptureViewport", json.dumps(args))
    txt = res.get_value_as_json_string()
    d = json.loads(txt) if txt else None
    while isinstance(d, str):
        d = json.loads(d)
    if not d:
        print("NO_IMAGE err", res.error)
        return
    os.makedirs(OUT_DIR, exist_ok=True)
    io.open(OUT_DIR + name + ".png", "wb").write(base64.b64decode(d["returnValue"]["image"]["data"]))
    print("SHOT:", OUT_DIR + name + ".png")


def spawn_flesh():
    _spawn("NS_Impact_Flesh")


def shot_flesh_start():
    _shot("flesh_t004_s1", 0.04, 1.0)


def shot_flesh_mid():
    _shot("flesh_t015_s1", 0.15, 1.0)


def shot_flesh_big():
    _shot("flesh_t015_s2", 0.15, 2.0)
