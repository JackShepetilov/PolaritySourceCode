# Buildables: input actions, mapping contexts, and the builder component on the base character.
import unreal, json

EAL = unreal.EditorAssetLibrary
IS = unreal.InputService
ACT = "/Game/Variant_Shooter/Input/Actions"
INP = "/Game/Variant_Shooter/Input"
BUILD = "/Game/Variant_Shooter/Buildables"
CHAR = "/Game/Variant_Shooter/Blueprints/BP_ShooterCharacter1"

def log(*a):
    print(" ".join(str(x) for x in a))

def compile_bp(path):
    res = unreal.ToolsetRegistry.execute_tool(
        "editor_toolset.toolsets.blueprint.BlueprintTools", "compile_blueprint",
        json.dumps({"blueprint": {"refPath": path + "." + path.split("/")[-1]}}))
    if res.error:
        log("COMPILE ERROR", path, res.error)
    bp = unreal.load_asset(path)
    log("compiled", path, bp.get_editor_property("status") if bp else "no asset")

def ensure_action(name):
    p = ACT + "/" + name
    if not IS.input_action_exists(p):
        r = IS.create_action(name, ACT, "Boolean")
        log("CREATED:", p, r)
    return p

def ensure_context(name, priority):
    p = INP + "/" + name
    if not IS.mapping_context_exists(p):
        r = IS.create_mapping_context(name, INP, priority)
        log("CREATED:", p, r)
    return p

def ensure_mapping(ctx, action, key):
    # one action, one key per context here; the existence check is per action
    if not IS.key_mapping_exists(ctx, action):
        ok = IS.add_key_mapping(ctx, action, key)
        log("MAPPED:", ctx.split("/")[-1], action.split("/")[-1], key, ok)
    else:
        log("kept mapping", ctx.split("/")[-1], action.split("/")[-1])

def main():
    ia_menu = ensure_action("IA_BuildMenu")
    ia_slots = [ensure_action("IA_BuildSlot%d" % i) for i in (1, 2, 3, 4)]
    ia_confirm = ensure_action("IA_BuildConfirm")
    ia_rotate = ensure_action("IA_BuildRotate")
    ia_cancel = ensure_action("IA_BuildCancel")

    imc_menu = ensure_context("IMC_BuildMenu", 10)
    imc_place = ensure_context("IMC_BuildPlacement", 10)
    imc_weapons = INP + "/IMC_Weapons"

    ensure_mapping(imc_weapons, ia_menu, "B")
    for p, key in zip(ia_slots, ("One", "Two", "Three", "Four")):
        ensure_mapping(imc_menu, p, key)
    ensure_mapping(imc_place, ia_confirm, "LeftMouseButton")
    ensure_mapping(imc_place, ia_rotate, "RightMouseButton")
    ensure_mapping(imc_place, ia_cancel, "Escape")

    for p in (imc_menu, imc_place, imc_weapons, ia_menu, ia_confirm, ia_rotate, ia_cancel) + tuple(ia_slots):
        EAL.save_asset(p)

    # ---------- the builder component on the base character ----------
    cls = EAL.load_blueprint_class(CHAR)
    cdo = unreal.get_default_object(cls)
    bc = cdo.get_editor_property("builder_component")
    defs = [unreal.load_asset(BUILD + "/DA_Buildable_" + n) for n in ("Turret", "Dispenser", "Teleporter")]
    bc.set_editor_property("buildables", defs)
    bc.set_editor_property("build_menu_action", unreal.load_asset(ia_menu))
    bc.set_editor_property("menu_mapping_context", unreal.load_asset(imc_menu))
    bc.set_editor_property("slot_actions", [unreal.load_asset(p) for p in ia_slots])
    bc.set_editor_property("placement_mapping_context", unreal.load_asset(imc_place))
    bc.set_editor_property("confirm_action", unreal.load_asset(ia_confirm))
    bc.set_editor_property("rotate_action", unreal.load_asset(ia_rotate))
    bc.set_editor_property("cancel_action", unreal.load_asset(ia_cancel))
    bc.set_editor_property("preview_class", EAL.load_blueprint_class(BUILD + "/BP_BuildablePreview"))
    compile_bp(CHAR)
    EAL.save_asset(CHAR)
    log("MODIFIED:", CHAR, "builder component configured")

    # proof by spawn
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    cls = EAL.load_blueprint_class(CHAR)
    a = eas.spawn_actor_from_class(cls, unreal.Vector(0, 0, -100000))
    b = a.get_editor_property("builder_component")
    log("SPAWN CHECK buildables:", [d.get_name() for d in b.get_editor_property("buildables")],
        "menu action:", b.get_editor_property("build_menu_action"),
        "slots:", len(b.get_editor_property("slot_actions")),
        "preview:", b.get_editor_property("preview_class"))
    eas.destroy_actor(a)
    log("DONE")

main()
