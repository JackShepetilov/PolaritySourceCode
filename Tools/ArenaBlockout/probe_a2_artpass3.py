# Probe #3: find a WORLD-ALIGNED / triplanar master material in the project and dump its
# parameters, so the art-pass can use world-projection tiling (adaptive, size-independent)
# instead of the broken per-UV scalar Tiling. Read-only. Results: Polarity.log [ARTPASS_PROBE3].

import re
import unreal

TAG = "[ARTPASS_PROBE3]"


def log(m):
    unreal.log("{} {}".format(TAG, m))


def warn(m):
    unreal.log_warning("{} {}".format(TAG, m))


def dump_params(mat, label):
    mei = unreal.MaterialEditingLibrary
    try:
        tex = [str(x) for x in mei.get_texture_parameter_names(mat)]
    except Exception:
        tex = ["<err>"]
    try:
        sca = [str(x) for x in mei.get_scalar_parameter_names(mat)]
    except Exception:
        sca = ["<err>"]
    try:
        vec = [str(x) for x in mei.get_vector_parameter_names(mat)]
    except Exception:
        vec = ["<err>"]
    sw = []
    try:
        sw = [str(x) for x in mei.get_static_switch_parameter_names(mat)]
    except Exception:
        sw = ["<n/a>"]
    log("MAT {} | tex={} | scalar={} | vector={} | switch={}".format(label, tex, sca, vec, sw))


def main():
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    try:
        ar.wait_for_completion()
    except Exception:
        pass

    # all Material (not instance) assets, filter by name for world/triplanar/proto masters
    try:
        cls = unreal.TopLevelAssetPath("/Script/Engine", "Material")
        datas = ar.get_assets_by_class(cls, False)
    except Exception as e:
        warn("get_assets_by_class: {}".format(e))
        datas = []
    rx = re.compile(r"(?i)planar|prototype|grid|worldaligned|world_aligned|triplanar|kit_detail|kit_pom")
    found = []
    for d in datas:
        nm = str(d.asset_name)
        if rx.search(nm):
            found.append((nm, "{}.{}".format(str(d.package_name), nm)))
    log("candidate masters: {}".format(len(found)))
    for nm, path in sorted(found):
        a = unreal.EditorAssetLibrary.load_asset(path)
        if a is None:
            warn("load fail {}".format(path))
            continue
        log("---- {} | {}".format(nm, path))
        dump_params(a, nm)

    # also confirm the master from the user's screenshot specifically
    for guess in ["/Game/PolygonPrototype/Materials/M_PolygonPrototype_Gobal_Grid_01_Base.M_PolygonPrototype_Gobal_Grid_01_Base",
                  "/Game/LevelPrototyping/Materials/M_PolygonPrototype_Gobal_Grid_01_Base.M_PolygonPrototype_Gobal_Grid_01_Base"]:
        if unreal.EditorAssetLibrary.does_asset_exist(guess):
            log("FOUND screenshot master at {}".format(guess))
    log("PROBE3 DONE")


main()
