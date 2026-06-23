# Inspect the live editor to READ what the lead set up: the example terrace-edge actor
# (ART_A2_Courtyard_pl_s_e2) vs the terrace platform (pl_s_e), the texture picks currently on the
# MI_ArtPass_* materials, and any textures imported under /Game/ArenaArtPass/Textures.
# Read-only, no level load. Results: Polarity.log [ARTPASS_PROBE5].

import unreal

TAG = "[ARTPASS_PROBE5]"


def log(m):
    unreal.log("{} {}".format(TAG, m))


def warn(m):
    unreal.log_warning("{} {}".format(TAG, m))


def mic_textures(mic):
    out = []
    try:
        for tv in mic.get_editor_property("texture_parameter_values"):
            info = tv.get_editor_property("parameter_info")
            nm = str(info.get_editor_property("name"))
            tex = tv.get_editor_property("parameter_value")
            out.append("{}={}".format(nm, tex.get_path_name() if tex else "None"))
    except Exception as e:
        out.append("<err {}>".format(e))
    return out


def main():
    ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    log("PIE={}".format(ues.get_game_world() is not None))
    world = ues.get_editor_world()
    log("current level: {}".format(world.get_package().get_name() if world else "?"))

    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    found = 0
    for a in eas.get_all_level_actors():
        try:
            lbl = a.get_actor_label()
        except Exception:
            continue
        if "pl_s_e" not in lbl and "pl_n" not in lbl and "_terr" not in lbl.lower():
            continue
        found += 1
        loc = a.get_actor_location()
        r = a.get_actor_rotation()
        s = a.get_actor_scale3d()
        cls = a.get_class().get_name()
        mesh = "?"
        mat = "?"
        try:
            smc = a.static_mesh_component
            if smc.static_mesh:
                mesh = smc.static_mesh.get_path_name()
            m0 = smc.get_material(0)
            if m0:
                mat = m0.get_path_name()
        except Exception:
            pass
        log("ACTOR '{}' cls={} loc=({:.0f},{:.0f},{:.0f}) rot=({:.1f},{:.1f},{:.1f}) "
            "scale=({:.3f},{:.3f},{:.3f})".format(lbl, cls, loc.x, loc.y, loc.z,
                                                  r.roll, r.pitch, r.yaw, s.x, s.y, s.z))
        log("   mesh={}".format(mesh))
        log("   mat0={}".format(mat))
    log("terrace-ish actors found: {}".format(found))

    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    try:
        ar.wait_for_completion()
    except Exception:
        pass
    eal = unreal.EditorAssetLibrary
    # materials in /Game/ArenaArtPass (MICs -> read their texture picks)
    for d in ar.get_assets_by_path("/Game/ArenaArtPass", True, False):
        cls = ""
        try:
            cls = str(d.asset_class_path.asset_name)
        except Exception:
            pass
        p = "{}.{}".format(str(d.package_name), str(d.asset_name))
        if cls == "MaterialInstanceConstant":
            a = eal.load_asset(p)
            parent = "?"
            try:
                pr = a.get_editor_property("parent")
                parent = pr.get_path_name() if pr else "?"
            except Exception:
                pass
            log("MIC {} | parent={} | {}".format(str(d.asset_name), parent, " ; ".join(mic_textures(a))))
        elif cls == "Material":
            log("MATERIAL {} | {}".format(str(d.asset_name), p))
        elif cls == "Texture2D":
            log("TEXTURE {} | {}".format(str(d.asset_name), p))
    log("PROBE5 DONE")


main()
