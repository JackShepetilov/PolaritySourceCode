# SURGICAL: hide the blockout's yellow EMF-prop combat boxes (BLOCKOUT_-tagged BP_EMFProp) in the
# art dupe, WITHOUT touching anything else (no reskin, no material/master changes, no other actors).
# Respects the lead's manual level edits. Operates only on the already-open dupe (refuses to load).
# Log: [EMF_CLEAN].

import unreal

TAG = "[EMF_CLEAN]"
DST = "/Game/Variant_Shooter/Arenas/Biome1/A2_Courtyard/Lvl_A2_Courtyard_Art"
BLK_TAG = "BLOCKOUT_A2_Courtyard"


def log(m):
    unreal.log("{} {}".format(TAG, m))


def warn(m):
    unreal.log_warning("{} {}".format(TAG, m))


def main():
    ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    if ues.get_game_world() is not None:
        warn("PIE running - abort")
        return
    w = ues.get_editor_world()
    cur = w.get_package().get_name() if w else ""
    if cur != DST:
        warn("Open {} first (current: {}). Not loading - your edits stay safe.".format(DST, cur))
        return
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    hidden = 0
    for a in eas.get_all_level_actors():
        try:
            tags = [str(t) for t in a.tags]
            if BLK_TAG not in tags:
                continue
            if "EMFProp" not in a.get_class().get_name():
                continue
            comps = a.get_components_by_class(unreal.StaticMeshComponent)
            for smc in comps:
                smc.set_visibility(False, True)
            try:
                a.set_is_temporarily_hidden_in_editor(True)
            except Exception:
                pass
            hidden += 1
        except Exception:
            pass
    log("Hid {} blockout EMF-prop boxes (NOT saved - review and Ctrl+S to keep)".format(hidden))
    log("EMF_CLEAN DONE")


main()
