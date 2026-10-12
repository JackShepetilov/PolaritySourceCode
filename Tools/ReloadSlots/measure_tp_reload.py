# Third person reload montages on a live NPC: which slot carries them, does a crouched body stand
# up, do the legs keep moving on the run. Driven frame by frame; results in Saved/ReloadSlots/.
#   run.sh measure_tp_reload start_pie | prepare | probe | arm | report
import json

import unreal

OUT = unreal.Paths.project_saved_dir() + "ReloadSlots/tp_reload.jsonl"
NPC_CLASS = "/Game/Variant_Shooter/Blueprints/AI/BPs/BP_ShooterNPC.BP_ShooterNPC_C"
WEAPON_DIR = "/Game/Variant_Shooter/Blueprints/Pickups/Weapons"
FIELDS = ["ReloadMontageTP", "SecondaryReloadMontageTP", "ReloadEndMontageTP"]
SLOTS = ["DefaultSlot", "Action Standing", "Action Aiming"]


def game_world():
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()


def start_pie():
    args = {"options": {"bSimulate": False, "playMode": "PlayMode_InEditorFloating", "warmupSeconds": 3}}
    res = unreal.ToolsetRegistry.execute_tool("EditorToolset.EditorAppToolset", "StartPIE", json.dumps(args))
    print("START_PIE", res.is_complete, res.error)


def prepare():
    w = game_world()
    if not w:
        print("NO PIE")
        return
    unreal.SystemLibrary.execute_console_command(w, "polarity.ai.IgnorePlayers 1")
    unreal.load_object(None, NPC_CLASS)
    unreal.SystemLibrary.execute_console_command(w, "summon " + NPC_CLASS)
    print("SUMMONED")


def find_npc(w):
    npcs = [a for a in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.ShooterNPC)
            if a.get_class().get_name().startswith("BP_ShooterNPC")]
    pawn = unreal.GameplayStatics.get_player_pawn(w, 0)
    npcs.sort(key=lambda a: (a.get_actor_location() - pawn.get_actor_location()).length())
    return npcs[0] if npcs else None


def weapon_montages():
    """{montage path: {"montage": obj, "users": ["BP_X.Field", ...]}} from the weapon class defaults."""
    reg = unreal.AssetRegistryHelpers.get_asset_registry()
    flt = unreal.ARFilter(package_paths=[WEAPON_DIR], recursive_paths=True, class_paths=[
        unreal.TopLevelAssetPath("/Script/Engine", "Blueprint")])
    out = {}
    for ad in reg.get_assets(flt):
        cls = unreal.load_object(None, "%s.%s_C" % (ad.package_name, ad.asset_name))
        if not cls or not unreal.MathLibrary.class_is_child_of(cls, unreal.ShooterWeapon):
            continue
        cdo = unreal.get_default_object(cls)
        for f in FIELDS:
            try:
                m = cdo.get_editor_property(f)
            except Exception:
                m = None
            if m:
                e = out.setdefault(m.get_path_name(), {"montage": m, "users": []})
                e["users"].append("%s.%s" % (ad.asset_name, f))
    return out


def probe():
    w = game_world()
    if not w:
        print("NO PIE")
        return
    npc = find_npc(w)
    print("NPC", npc.get_name() if npc else None)
    if not npc:
        return
    mv = npc.get_movement_component()
    print("MOVE", mv.get_class().get_name(), [n for n in dir(mv) if "crouch" in n.lower()])
    print("CHAR", [n for n in dir(npc) if "crouch" in n.lower()])
    body = npc.get_editor_property("mesh")
    print("ANIM", body.get_anim_instance().get_class().get_name())
    for path, e in sorted(weapon_montages().items()):
        print("MONTAGE", path.split(".")[-1], round(e["montage"].get_play_length(), 2), e["users"])


def _flag(v):
    return bool(v() if callable(v) else v)


def arm():
    """Two passes per montage: crouched and still, then running in a circle. Each pass takes a
    baseline window first, then the same window with the montage on the body."""
    import math
    w = game_world()
    if not w:
        print("NO PIE")
        return
    npc = find_npc(w)
    body = npc.get_editor_property("mesh")
    anim = body.get_anim_instance()
    mv = npc.get_movement_component()
    cap = npc.get_editor_property("capsule_component")
    home = npc.get_actor_location()
    jobs = []
    for path, e in sorted(weapon_montages().items()):
        for mode in ("crouch", "run"):
            jobs.append({"name": path.split(".")[-1], "users": e["users"], "montage": e["montage"], "mode": mode})
    open(OUT, "w").close()
    st = {"t": 0.0, "i": 0, "phase": "setup", "t0": 0.0, "h": None, "base": [], "mont": []}

    def snap():
        floor = npc.get_actor_location().z - cap.get_scaled_capsule_half_height()
        foot = body.get_socket_transform("foot_l", unreal.RelativeTransformSpace.RTS_COMPONENT).translation
        v = npc.get_velocity()
        row = {"pelvis": body.get_socket_location("pelvis").z - floor, "fx": foot.x, "fy": foot.y, "fz": foot.z,
               "speed": math.hypot(v.x, v.y), "crouch": _flag(mv.is_crouching)}
        for s in SLOTS:
            row[s] = anim.blueprint_get_slot_montage_local_weight(s)
        return row

    def fold(rows):
        if not rows:
            return {}
        avg = lambda k: round(sum(r[k] for r in rows) / len(rows), 2)
        rng = lambda k: round(max(r[k] for r in rows) - min(r[k] for r in rows), 1)
        out = {"n": len(rows), "pelvis": avg("pelvis"), "speed": avg("speed"), "crouch": avg("crouch"),
               "foot_range": round(max(rng("fx"), rng("fy"), rng("fz")), 1)}
        for s in SLOTS:
            out[s] = round(max(r[s] for r in rows), 2)
        return out

    def tick(dt):
        try:
            step(dt)
        except Exception as exc:
            unreal.unregister_slate_post_tick_callback(st["h"])
            unreal.log_error("RELOAD_SLOTS FAILED %s" % exc)

    def step(dt):
        if not game_world():
            unreal.unregister_slate_post_tick_callback(st["h"])
            return
        st["t"] += dt
        if st["i"] >= len(jobs):
            unreal.unregister_slate_post_tick_callback(st["h"])
            mv.stop_crouching()
            unreal.log("RELOAD_SLOTS DONE %s" % OUT)
            return
        job = jobs[st["i"]]
        m = job["montage"]
        length = m.get_play_length()
        w0, w1 = min(0.35, length * 0.3), min(length - 0.25, 1.6)
        el = st["t"] - st["t0"]
        if job["mode"] == "run":
            a = st["t"] * 1.2
            npc.add_movement_input(unreal.Vector(math.cos(a), math.sin(a), 0.0), 1.0, True)
        if st["phase"] == "setup":
            npc.set_actor_location(home, False, True)
            if job["mode"] == "crouch":
                mv.start_crouching()
            else:
                mv.stop_crouching()
            st.update(phase="settle", t0=st["t"], base=[], mont=[])
        elif st["phase"] == "settle" and el >= 1.0:
            st.update(phase="base", t0=st["t"])
        elif st["phase"] == "base":
            st["base"].append(snap())
            if el >= (w1 - w0):
                anim.montage_play(m, 1.0)
                st.update(phase="mont", t0=st["t"])
        elif st["phase"] == "mont":
            if el >= w0:
                st["mont"].append(snap())
            if el >= w1:
                anim.montage_stop(0.1, m)
                with open(OUT, "a", encoding="utf-8") as fh:
                    fh.write(json.dumps({"montage": job["name"], "mode": job["mode"], "users": job["users"],
                                         "base": fold(st["base"]), "mont": fold(st["mont"])}) + "\n")
                st.update(phase="cool", t0=st["t"])
        elif st["phase"] == "cool" and el >= 0.5:
            st["i"] += 1
            st["phase"] = "setup"

    st["h"] = unreal.register_slate_post_tick_callback(tick)
    print("RELOAD_SLOTS ARMED jobs=%d" % len(jobs))


def report():
    rows = [json.loads(l) for l in open(OUT, encoding="utf-8") if l.strip()]
    by = {}
    for r in rows:
        by.setdefault(r["montage"], {})[r["mode"]] = r
    print("%-36s %-16s | crouch pelvis base>mont | run foot base>mont speed" % ("montage", "slot"))
    for name, d in sorted(by.items()):
        c, r = d.get("crouch"), d.get("run")
        any_row = c or r
        slot = ",".join(s for s in SLOTS if any_row["mont"].get(s, 0) > 0.5) or "none"
        cs = "%5.1f > %5.1f (cr %.0f)" % (c["base"]["pelvis"], c["mont"]["pelvis"], c["mont"]["crouch"]) if c else "-"
        rs = "%5.1f > %5.1f  %4.0f>%4.0f" % (r["base"]["foot_range"], r["mont"]["foot_range"], r["base"]["speed"], r["mont"]["speed"]) if r else "-"
        print("%-36s %-16s | %s | %s" % (name, slot, cs, rs))
