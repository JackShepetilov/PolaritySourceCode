# bench.py: put the grenade stand on L_CombatBench (Tools/ue_step.sh Grenades bench <step>).
import unreal

ARM = r"C:/Users/Professional/Documents/Unreal Projects/Polarity_Main5_8/Source/Tools/CombatBench/build_bench_armory.py"
CB = r"C:/Users/Professional/Documents/Unreal Projects/Polarity_Main5_8/Source/Tools/CombatBench/build_combat_bench.py"


def where():
    w = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    print("open:", w.get_package().get_name() if w else None)
    print("pie:", unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is not None)
    print("dirty maps:", [p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()])


def open_level():
    g = {"__name__": "cb"}
    exec(compile(open(CB, encoding="utf-8").read(), CB, "exec"), g)
    g["step_level"]()


def grenades():
    g = {"__name__": "arm"}
    exec(compile(open(ARM, encoding="utf-8").read(), ARM, "exec"), g)
    g["step_grenades"]()


def check():
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for a in eas.get_all_level_actors():
        if isinstance(a, unreal.GrenadePickup):
            m = a.get_component_by_class(unreal.StaticMeshComponent)
            print(a.get_actor_label(), a.get_actor_location(), a.get_editor_property("type").get_name(), a.get_editor_property("count"),
                  m.get_editor_property("static_mesh").get_name() if m and m.get_editor_property("static_mesh") else None,
                  m.is_visible() if m else None)
        elif a.get_actor_label().startswith("Medkit_Stand") or a.get_actor_label().startswith("Grenade_Stand"):
            o, e = a.get_actor_bounds(False)
            print(a.get_actor_label(), "center", o, "extent", e)


def shot():
    import json, base64
    cam = unreal.Transform(location=unreal.Vector(-150.0, -250.0, 260.0), rotation=unreal.Rotator(roll=0.0, pitch=-25.0, yaw=0.0))
    args = {"captureTransform": json.loads(json.dumps({"Translation": {"X": -150, "Y": -250, "Z": 260},
                                                       "Rotation": {"Pitch": -25, "Yaw": 0, "Roll": 0}})),
            "annotations": {"bShowGrid": False, "bShowActorLabels": False, "GridSpacing": 100, "LabelMaxActors": 0,
                            "LabelMaxDistance": 0, "bShowAxes": False}}
    res = unreal.ToolsetRegistry.execute_tool("EditorToolset.EditorAppToolset", "CaptureViewport", json.dumps(args))
    print("done", res.is_complete, res.error)
    if res.is_complete and not res.error:
        v = res.get_value_as_json_string()
        while isinstance(v, str):
            v = json.loads(v)
        data = v["returnValue"]["image"]["data"]
        p = "C:/Users/Professional/Documents/Unreal Projects/Polarity_Main5_8/Saved/grenade_stand.png"
        open(p, "wb").write(base64.b64decode(data))
        print("saved", p)
