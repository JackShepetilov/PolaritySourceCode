import unreal
print("PIE:", unreal.PerformanceService.frame_timing().get("pie_running") if isinstance(unreal.PerformanceService.frame_timing(), dict) else unreal.PerformanceService.frame_timing()[:200])
base = "/Game/Variant_Shooter/Blueprints/Pickups/Projectiles/"
for n in ["BP_Bullet_AR","BP_Bullet_Marksman","BP_Bullet_MG","BP_Bullet_Pistol","BP_Bullet_SMG","BP_Bullet_Shotgun"]:
    bp = unreal.load_asset(base + n)
    cls = bp.generated_class()
    cdo = unreal.get_default_object(cls)
    tfx = cdo.get_editor_property("trail_fx")
    pm = cdo.get_editor_property("projectile_movement")
    col = cdo.get_editor_property("collision_component")
    print(n, "| parent:", "-", "| trail:", tfx.get_path_name() if tfx else None,
          "| rotFollowsVel:", pm.get_editor_property("rotation_follows_velocity") if pm else None,
          "| speed:", pm.get_editor_property("initial_speed") if pm else None,
          "| colScale:", col.get_editor_property("relative_scale3d") if col else None)
print("ViewProperty enum:", [m for m in dir(unreal.MaterialExposedViewProperty) if m.isupper()][:30])
for p in ["/Niagara/DynamicInputs/Transforms/TransformVector.TransformVector", "/Niagara/Modules/Update/Material/DynamicMaterialParameters.DynamicMaterialParameters"]:
    print(p, unreal.load_asset(p) is not None)
print("folder exists:", unreal.EditorAssetLibrary.does_directory_exist("/Game/Effects/Tracers"))
