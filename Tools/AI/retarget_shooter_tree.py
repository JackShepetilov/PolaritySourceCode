# ST_ShooterNPC_Push without the shield as the Push/Peek switch (2026-10-10).
#
#   Push        enter: Class Can Push AND Wants Open Fight;  -> Peek on Wants Cover
#   Peek        home state;                                  -> Push on Wants Open Fight
#   Last Stand                                               -> Peek on Can Retake Cover
#   Search / Investigate used to jump straight to Push; they go to Armed States now, which picks.
#
# The tree as it was is kept beside it as ST_ShooterNPC_Push_ShieldGate. Run once: main().
import unreal

S = unreal.StateTreeService
EAL = unreal.EditorAssetLibrary
T = "/Game/Variant_Shooter/Blueprints/AI/STs/ST_ShooterNPC_Push"
BACKUP = T + "_ShieldGate"
ARMED = "Root/Armed States"


def log(*a):
    msg = " ".join(str(x) for x in a)
    print(msg)
    unreal.log("[TREE_SETUP] " + msg)


def state(path):
    for st in S.get_state_tree_info(T).all_states:
        if st.path == path:
            return st
    raise RuntimeError("no state " + path)


def transition_to(path, target_name):
    for i, t in enumerate(state(path).transitions):
        if t.target_state_name == target_name:
            return i, t
    return -1, None


def swap_condition(path, target_name, old, new, new_target=""):
    i, t = transition_to(path, target_name)
    if t is None:
        log("SKIP", path, "has no transition to", target_name)
        return
    names = [c.struct_type for c in t.conditions]
    if new in names and old not in names:
        log("already done:", path, "->", target_name)
    else:
        if old in names:
            log("remove", old, S.remove_transition_condition(T, path, i, names.index(old)))
        log("add", new, S.add_transition_condition(T, path, i, new))
    if new_target:
        log("retarget", path, "->", new_target, S.update_transition(T, path, i, target_path=new_target))


def main():
    if unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor():
        log("PIE is running, nothing done")
        return
    if not EAL.does_asset_exist(BACKUP):
        log("CREATED backup", EAL.duplicate_asset(T, BACKUP) is not None, EAL.save_asset(BACKUP, False))

    push, peek, last = ARMED + "/Push", ARMED + "/Peek", ARMED + "/Last Stand"

    if "STCondition_ShooterWantsOpenFight" not in [c.struct_type for c in state(push).enter_conditions]:
        log("push enter + WantsOpenFight", S.add_enter_condition(T, push, "STCondition_ShooterWantsOpenFight"))
    swap_condition(push, "Peek", "STCondition_ShooterShieldDown", "STCondition_ShooterWantsCover")
    swap_condition(peek, "Push", "STCondition_ShooterShieldUp", "STCondition_ShooterWantsOpenFight")
    swap_condition(last, "Push", "STCondition_ShooterShieldUp", "STCondition_ShooterCanRetakeCover", new_target=peek)

    for path in (ARMED + "/Investigate Location", ARMED + "/Search for Enemy"):
        i, t = transition_to(path, "Push")
        if t is not None:
            log("retarget", path, "-> Armed States", S.update_transition(T, path, i, target_path=ARMED))

    res = S.compile_state_tree(T)
    log("COMPILE", res)
    for p in (push, peek, last, ARMED + "/Investigate Location", ARMED + "/Search for Enemy"):
        st = state(p)
        log(p, "enter", [c.struct_type for c in st.enter_conditions],
            "tr", [(t.trigger, t.target_state_name, [c.struct_type for c in t.conditions]) for t in st.transitions])
    log("SAVED", S.save_state_tree(T))
