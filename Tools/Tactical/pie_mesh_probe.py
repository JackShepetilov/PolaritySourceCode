import unreal
w = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
if not w:
    print("NO PIE")
else:
    pawn = unreal.GameplayStatics.get_player_pawn(w, 0)
    wpn = pawn.get_current_weapon()
    print("WEAPON", wpn.get_name(), [a.get_name() for a in wpn.get_installed_attachments()] if hasattr(wpn, "get_installed_attachments") else "")
    fp = wpn.get_first_person_mesh()
    print("FP", fp.get_name(), fp.skeletal_mesh_asset.get_name() if fp.skeletal_mesh_asset else None, "vis", fp.is_visible(),
          "sock", fp.does_socket_exist("SOCKET_Tactical"), fp.get_socket_location("SOCKET_Tactical"))
    for c in wpn.get_components_by_class(unreal.StaticMeshComponent):
        sm = c.static_mesh
        b = sm.get_bounds() if sm else None
        print("SMC", c.get_name(), sm.get_name() if sm else None, "parent", c.get_attach_parent().get_name() if c.get_attach_parent() else None,
              "sock", c.get_attach_socket_name(), "vis", c.is_visible(), "hidden", c.hidden_in_game,
              "loc", c.get_world_location(), "scale", c.get_world_scale(),
              "ext", b.box_extent if b else None, "mat", [m.get_name() if m else None for m in c.get_materials()],
              "onlyOwner", c.get_editor_property("only_owner_see"), "fpType", c.get_editor_property("first_person_primitive_type"))
