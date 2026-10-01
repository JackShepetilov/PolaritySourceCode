import unreal
for p in ("/Game/InfimaGames/LowPolyShooterPack/Art/Effects/Models/SM_LaserSight",
          "/Game/InfimaGames/AnimatedLowPolyWeapons/Art/Weapons/_Common/Attachments/Models/SM_ATT_Laser_Flashlight_01"):
    sm = unreal.load_asset(p)
    b = sm.get_bounds()
    mats = [m.material_interface for m in sm.static_materials]
    print("MESH", sm.get_name(), "origin", b.origin, "ext", b.box_extent,
          "mats", [(m.get_name(), m.get_base_material().get_editor_property("blend_mode")) for m in mats if m])
cls = unreal.EditorAssetLibrary.load_blueprint_class("/Game/Variant_Shooter/Blueprints/Pickups/Weapons/Kinemation/Pistols/BP_M1911")
cdo = unreal.get_default_object(cls) if cls else None
print("M1911 cdo", cdo, cdo.get_editor_property("attachment_sockets") if cdo else None)
if cdo:
    fp = cdo.get_first_person_mesh(); tp = cdo.get_third_person_mesh()
    for m in (fp, tp):
        sk = m.skeletal_mesh_asset
        print("MESHCOMP", m.get_name(), sk.get_path_name() if sk else None, "vis", m.is_visible())
        if sk:
            s = sk.find_socket("SOCKET_Tactical")
            print("  socket", s, s.get_editor_property("bone_name") if s else None, s.get_editor_property("relative_location") if s else None)
