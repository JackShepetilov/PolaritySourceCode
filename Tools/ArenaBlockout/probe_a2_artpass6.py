# Load the art dupe and READ the lead's example terrace-edge actor (ART_A2_Courtyard_pl_s_e2) vs the
# terrace platforms, plus the material actually assigned to each surface group. Results in
# Saved/Logs/Polarity_2.log (current session) under [ARTPASS_PROBE6].

import unreal

TAG = "[ARTPASS_PROBE6]"
DST = "/Game/Variant_Shooter/Arenas/Biome1/A2_Courtyard/Lvl_A2_Courtyard_Art"


def log(m):
    unreal.log("{} {}".format(TAG, m))


def warn(m):
    unreal.log_warning("{} {}".format(TAG, m))


def main():
    ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    if ues.get_game_world() is not None:
        warn("PIE running - abort")
        return
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    w = ues.get_editor_world()
    cur = w.get_package().get_name() if w else ""
    if cur != DST:
        dirty = unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()
        foreign = [p for p in dirty if p.get_name() != DST]
        if foreign:
            warn("UNSAVED foreign maps: {} - not loading".format(", ".join(p.get_name() for p in foreign)))
            return
        if not les.load_level(DST):
            warn("load_level failed")
            return
        log("loaded {}".format(DST))
    else:
        log("already on dupe")

    seen_groups = {}
    pl_actors = []
    total = 0
    for a in eas.get_all_level_actors():
        try:
            lbl = a.get_actor_label()
        except Exception:
            continue
        if not lbl.startswith("ART_A2"):
            continue
        total += 1
        try:
            folder = str(a.get_folder_path())
        except Exception:
            folder = "?"
        # representative material per folder group
        if folder not in seen_groups:
            try:
                smc = a.static_mesh_component
                mat = smc.get_material(0)
                mesh = smc.static_mesh
                seen_groups[folder] = (lbl,
                                       mat.get_path_name() if mat else "None",
                                       mesh.get_path_name() if mesh else "None")
            except Exception as e:
                seen_groups[folder] = (lbl, "<err {}>".format(e), "?")
        if "pl_" in lbl:
            pl_actors.append(a)

    log("total ART_A2 actors: {}".format(total))
    log("--- representative material per group ---")
    for folder, (lbl, mat, mesh) in sorted(seen_groups.items()):
        log("GROUP {} | sample={} | mat={} | mesh={}".format(folder, lbl, mat, mesh))

    log("--- terrace (pl_*) actors incl. example pl_s_e2 ---")
    for a in pl_actors:
        lbl = a.get_actor_label()
        loc = a.get_actor_location()
        r = a.get_actor_rotation()
        s = a.get_actor_scale3d()
        o, e = a.get_actor_bounds(False)
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
        log("PL '{}' cls={} loc=({:.0f},{:.0f},{:.0f}) rot=({:.1f},{:.1f},{:.1f}) "
            "scale=({:.3f},{:.3f},{:.3f}) extent=({:.0f},{:.0f},{:.0f})".format(
                lbl, cls, loc.x, loc.y, loc.z, r.roll, r.pitch, r.yaw, s.x, s.y, s.z, e.x, e.y, e.z))
        log("    mesh={} mat={}".format(mesh, mat))
    log("PROBE6 DONE")


main()
