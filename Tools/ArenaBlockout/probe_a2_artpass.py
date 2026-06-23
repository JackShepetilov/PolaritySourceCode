# One-shot probe for the A2 art-pass: resolves exact asset paths, static-mesh bounds,
# and material parameter names so apply_art_pass.py can be written against REAL data
# (no guessing UE material/mesh APIs). Read results in Saved/Logs/Polarity.log [ARTPASS_PROBE].
#
# Run in the open editor (REST):
#   POST 127.0.0.1:3000/mcp/tool/run_console_command {"command":"py \"<abs>/probe_a2_artpass.py\""}

import unreal

TAG = "[ARTPASS_PROBE]"


def log(m):
    unreal.log("{} {}".format(TAG, m))


def warn(m):
    unreal.log_warning("{} {}".format(TAG, m))


def main():
    ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    if ues.get_game_world() is not None:
        warn("PIE IS RUNNING - read-only probe is fine, but do NOT build until PIE stops")

    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    try:
        ar.wait_for_completion()
    except Exception as e:
        warn("wait_for_completion: {}".format(e))

    eal = unreal.EditorAssetLibrary
    for d in ["/Game/temple", "/Game/Greek_island", "/Game/PolygonCasino"]:
        try:
            log("dir_exists {} = {}".format(d, eal.does_directory_exist(d)))
        except Exception as e:
            warn("dir_exists {}: {}".format(d, e))

    # ---- index via AssetRegistry (more reliable than list_assets during a busy session) ----
    roots = ["/Game/Greek_island", "/Game/PolygonCasino", "/Game/temple"]
    index = {}
    for r in roots:
        cnt = 0
        try:
            datas = ar.get_assets_by_path(r, True, False)  # recursive, include on-disk too
            for dat in datas:
                nm = str(dat.asset_name)
                pkg = str(dat.package_name)
                index[nm] = "{}.{}".format(pkg, nm)
                cnt += 1
        except Exception as e:
            warn("get_assets_by_path {}: {}".format(r, e))
        log("root {} -> {} assets".format(r, cnt))
    log("indexed {} total assets".format(len(index)))

    # ---- candidate meshes: report path + full size (2*box_extent) + origin -------
    mesh_targets = [
        "SM_column_01", "SM_small_column",
        "SM_Fountain_Plain_01", "SM_altar", "SM_altar_02", "SM_Statue", "SM_Front_Statue",
        "SM_house_01", "SM_house_02", "SM_house_03",
        "SM_wall_module_01", "SM_wall_module_02", "SM_back_wall_02", "SM_wall_end",
        "SM_temple_wall", "SM_temple_roof", "SM_temple_bottom", "SM_entrance_temple",
        "SM_door_01", "SM_vases",
        "SM_Bld_Base_Pillar_01", "SM_Bld_Base_Pillar_02", "SM_Bld_Walkway_Pillars_01",
    ]
    for nm in mesh_targets:
        p = index.get(nm)
        if not p:
            warn("MESH MISSING: {}".format(nm))
            continue
        a = unreal.EditorAssetLibrary.load_asset(p)
        if a is None:
            warn("MESH LOAD FAIL: {} ({})".format(nm, p))
            continue
        try:
            b = a.get_bounds()
            e = b.box_extent
            o = b.origin
            log("MESH {} | {} | size=({:.0f},{:.0f},{:.0f}) origin=({:.0f},{:.0f},{:.0f})".format(
                nm, p, e.x * 2, e.y * 2, e.z * 2, o.x, o.y, o.z))
        except Exception as ex:
            warn("bounds {} failed: {}".format(nm, ex))

    # ---- candidate materials: report class, base material, and parameter names ---
    mat_targets = [
        "MI_stone", "MI_large_bricks", "MI_large_bricks_red", "MI_roof",
        "MI_floor_temple", "MI_temple_trim_01", "MI_temple_trim_02", "MI_temple_trim_03",
        "MI_house_vertex_P", "MI_floor_vertex_P", "MI_vases", "MI_Statue_Front",
        "Walls_HorizStripes", "WhiteStone", "Gold", "Bronze", "PlazaLabyrinth",
        "DarkGlass", "DarkWood",
    ]
    mei = unreal.MaterialEditingLibrary
    for nm in mat_targets:
        p = index.get(nm)
        if not p:
            warn("MAT MISSING: {}".format(nm))
            continue
        a = unreal.EditorAssetLibrary.load_asset(p)
        if a is None:
            warn("MAT LOAD FAIL: {} ({})".format(nm, p))
            continue
        cls = a.get_class().get_name()
        base = None
        try:
            base = a.get_base_material()
        except Exception:
            try:
                base = a.get_editor_property("parent")
            except Exception:
                base = None
        log("MAT {} | {} | class={} | base={}".format(
            nm, p, cls, base.get_path_name() if base else "?"))
        target = base if base else a
        try:
            tex = [str(x) for x in mei.get_texture_parameter_names(target)]
            sca = [str(x) for x in mei.get_scalar_parameter_names(target)]
            vec = [str(x) for x in mei.get_vector_parameter_names(target)]
            log("   PARAMS tex={} | scalar={} | vector={}".format(tex, sca, vec))
        except Exception as ex:
            warn("   param read failed for {}: {}".format(nm, ex))

    # ---- available Greek textures (exact paths for authoring my MICs) ------------
    tex_names = sorted(n for n in index if n.startswith("T_"))
    for nm in tex_names:
        log("TEX {} | {}".format(nm, index[nm]))

    log("PROBE DONE")


main()
