"""Puts the class tracers on the bullet blueprints. RPG is not touched (own trail)."""
import json
import unreal

BASE = "/Game/Variant_Shooter/Blueprints/Pickups/Projectiles/"
FX = "/Game/Effects/Tracers/"
MAP = {"BP_Bullet_AR": "NS_Tracer_Bullet", "BP_Bullet_MG": "NS_Tracer_Bullet", "BP_Bullet_Pistol": "NS_Tracer_Bullet",
       "BP_Bullet_SMG": "NS_Tracer_Bullet", "BP_Bullet_Marksman": "NS_Tracer_Sniper", "BP_Bullet_Shotgun": "NS_Tracer_Pellet"}
eal = unreal.EditorAssetLibrary


def compile_bp(path):
    r = unreal.ToolsetRegistry.execute_tool("editor_toolset.toolsets.blueprint.BlueprintTools", "compile_blueprint",
                                            json.dumps({"blueprint": {"refPath": path + "." + path.split("/")[-1]}}))
    return r.error or r.get_value_as_json_string()[:120]


for bp, ns in MAP.items():
    path = BASE + bp
    cls = eal.load_blueprint_class(path)
    cdo = unreal.get_default_object(cls)
    old = cdo.get_editor_property("trail_fx")
    cdo.set_editor_property("trail_fx", unreal.load_asset(FX + ns))
    cdo.set_editor_property("free_tracer", True)  # drawn by UTracerSubsystem (lag, barrel origin, runs to impact)
    status = compile_bp(path)
    eal.save_asset(path, False)
    cdo = unreal.get_default_object(eal.load_blueprint_class(path))
    print("MODIFIED:", bp, (old.get_name() if old else None), "->", cdo.get_editor_property("trail_fx").get_name(),
          "free:", cdo.get_editor_property("free_tracer"), "|", status)
