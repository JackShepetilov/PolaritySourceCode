# Probe #2 for the A2 art-pass rework: confirm the on-theme LSJ texture paths and
# find decorative prop meshes (vases/planters/palms/benches). Read-only (no level load).
# Results in Saved/Logs/Polarity.log [ARTPASS_PROBE2].

import unreal

TAG = "[ARTPASS_PROBE2]"


def log(m):
    unreal.log("{} {}".format(TAG, m))


def warn(m):
    unreal.log_warning("{} {}".format(TAG, m))


def main():
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    try:
        ar.wait_for_completion()
    except Exception:
        pass

    roots = ["/Game/temple", "/Game/PolygonCasino", "/Game/Greek_island",
             "/Game/TropicalFoliage", "/Game/PolygonNatureBiomes", "/Game/Kobo_Nature"]
    index = {}
    classes = {}
    for r in roots:
        try:
            for d in ar.get_assets_by_path(r, True, False):
                nm = str(d.asset_name)
                index[nm] = "{}.{}".format(str(d.package_name), nm)
                try:
                    classes[nm] = str(d.asset_class_path.asset_name)
                except Exception:
                    classes[nm] = "?"
        except Exception as e:
            warn("get_assets_by_path {}: {}".format(r, e))
    log("indexed {} assets".format(len(index)))

    # 1) confirm on-theme textures exist
    want_tex = ["WhiteStone", "Walls_HorizStripes", "Gold", "Bronze", "PlazaLabyrinth",
                "DarkWood", "T_Church_Roof_01", "T_Marble_Tex_01", "T_Concrete",
                "T_Church_Wall_01"]
    for nm in want_tex:
        p = index.get(nm)
        log("TEX {} -> {}".format(nm, p if p else "MISSING"))

    # 2) list PolygonCasino Walls_Floors + Misc texture names (for stone/marble/roof choices)
    wf = sorted(n for n, p in index.items()
                if "PolygonCasino" in p and n.startswith("T_")
                and ("Walls_Floors" in p or "Misc" in p))
    for n in wf:
        log("PC_TEX {} -> {}".format(n, index[n]))

    # 3) decorative prop meshes across the nature/greek/casino packs
    import re
    rx = re.compile(r"(?i)vase|amphora|urn|\bpot\b|jar|planter|palm|\btree\b|\bplant\b|"
                    r"bench|seat|statue|fountain|lamp|torch|bush|pillar|column|olive|"
                    r"flower|shrub|foliage|cypress")
    hits = [n for n, c in classes.items() if c == "StaticMesh" and rx.search(n)]
    log("prop-mesh candidates: {}".format(len(hits)))
    for nm in sorted(hits)[:60]:
        a = unreal.EditorAssetLibrary.load_asset(index[nm])
        sz = "?"
        try:
            e = a.get_bounds().box_extent
            sz = "({:.0f},{:.0f},{:.0f})".format(e.x * 2, e.y * 2, e.z * 2)
        except Exception:
            pass
        log("PROP {} | {} | {}".format(nm, sz, index[nm]))
    log("PROBE2 DONE")


main()
