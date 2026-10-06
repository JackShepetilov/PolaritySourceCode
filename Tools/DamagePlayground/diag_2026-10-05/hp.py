import unreal
w = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
for a in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.Pawn):
    l = a.get_actor_label()
    if l in ("DP_GRUNT_NORMAL", "DP_TANK_NORMAL"):
        sh = a.get_component_by_class(unreal.ShieldFieldComponent)
        z = a.get_component_by_class(unreal.HitZoneComponent)
        print(l, "hp", a.get_editor_property("current_hp"), "armor", sh.get_current_shield() if sh else None,
              "zones", [(str(n), round(z.get_zone_pool_fraction(n),2)) for n in ("TrackLeft","TrackRight","Optics")] if z else None)
