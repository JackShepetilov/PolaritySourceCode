# Real reloads in a real fight: summon BP_ShooterNPC around the player (each rolls its own gun),
# let them shoot dry, and log legs / crouch / montage for every frame a weapon reports IsReloading.
#   run.sh fight_reload arm      -> Saved/ReloadSlots/fight_reload.jsonl, log line FIGHT_RELOAD DONE
import json
import math

import unreal

OUT = unreal.Paths.project_saved_dir() + "ReloadSlots/fight_reload.jsonl"
NPC_CLASS = "/Game/Variant_Shooter/Blueprints/AI/BPs/BP_ShooterNPC.BP_ShooterNPC_C"
SLOTS = ["DefaultSlot", "Action Standing", "Action Aiming"]
COUNT, RUN_SECONDS, CHUNK = 14, 150.0, 0.5
CFG = {"count": COUNT, "seconds": RUN_SECONDS, "out": OUT}


def _flag(v):
    return bool(v() if callable(v) else v)


def arm_more():
    """Second pass on the enemies already alive: also the hand and whether the body was on screen."""
    CFG.update(count=0, seconds=120.0, out=OUT.replace("fight_reload", "fight_reload_more"))
    arm()


def arm():
    w = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
    if not w:
        print("NO PIE")
        return
    cmd = lambda c: unreal.SystemLibrary.execute_console_command(w, c)
    cmd("polarity.player.god 1")
    cmd("polarity.ai.IgnorePlayers 0")
    unreal.load_object(None, NPC_CLASS)
    pc = unreal.GameplayStatics.get_player_controller(w, 0)
    open(CFG["out"], "w").close()
    st = {"t": 0.0, "h": None, "spawned": 0, "next_spawn": 0.0, "next_scan": 0.0, "npcs": {}}

    def emit(row):
        with open(CFG["out"], "a", encoding="utf-8") as fh:
            fh.write(json.dumps(row) + "\n")

    def scan():
        guns = {}
        for g in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.ShooterWeapon):
            o = g.get_owner()
            if o:
                guns[o.get_name()] = g
        for a in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.ShooterNPC):
            n = a.get_name()
            if n in st["npcs"] or n not in guns or not a.get_class().get_name().startswith("BP_ShooterNPC"):
                continue
            st["npcs"][n] = {"npc": a, "gun": guns[n], "gun_name": guns[n].get_class().get_name(),
                             "body": a.get_editor_property("mesh"), "mv": a.get_movement_component(),
                             "cap": a.get_editor_property("capsule_component"),
                             "pre": [], "rel": None, "chunk": [], "chunk_t": 0.0, "idle_chunks": []}

    def snap(e, dt):
        npc, body = e["npc"], e["body"]
        floor = npc.get_actor_location().z - e["cap"].get_scaled_capsule_half_height()
        foot = body.get_socket_transform("foot_l", unreal.RelativeTransformSpace.RTS_COMPONENT).translation
        v = npc.get_velocity()
        anim = body.get_anim_instance()
        m = anim.get_current_active_montage()
        hand = body.get_socket_transform("hand_r", unreal.RelativeTransformSpace.RTS_COMPONENT).translation
        row = {"dt": dt, "hand": (hand.x, hand.y, hand.z), "seen": body.was_recently_rendered(0.2), "pelvis": body.get_socket_location("pelvis").z - floor,
               "half": e["cap"].get_scaled_capsule_half_height(), "f": (foot.x, foot.y, foot.z),
               "speed": math.hypot(v.x, v.y), "crouch": _flag(e["mv"].is_crouching),
               "montage": m.get_name() if m else ""}
        for s in SLOTS:
            row[s] = anim.blueprint_get_slot_montage_local_weight(s)
        return row

    def foot_range(rows):
        return max(max(r["f"][k] for r in rows) - min(r["f"][k] for r in rows) for k in range(3))

    def chunk(e, row, sink):
        """Foot travel over half second stretches of uninterrupted running."""
        if row["speed"] < 200.0:
            e["chunk"], e["chunk_t"] = [], 0.0
            return
        e["chunk"].append(row)
        e["chunk_t"] += row["dt"]
        if e["chunk_t"] >= CHUNK:
            rows = e["chunk"]
            hand = max(max(r["hand"][k] for r in rows) - min(r["hand"][k] for r in rows) for k in range(3))
            sink.append([round(foot_range(rows), 1), round(hand, 1), round(sum(r["seen"] for r in rows) / len(rows), 2)])
            e["chunk"], e["chunk_t"] = [], 0.0

    def flush(e, name):
        rel = e["rel"]
        rows = rel["rows"]
        if not rows:
            return
        avg = lambda rs, k: round(sum(r[k] for r in rs) / len(rs), 2) if rs else None
        mont = {}
        for r in rows:
            mont[r["montage"]] = mont.get(r["montage"], 0) + 1
        emit({"npc": name, "gun": e["gun_name"], "t": round(st["t"], 1),
              "seconds": round(sum(r["dt"] for r in rows), 2),
              "pre_crouch": avg(e["pre_snapshot"], "crouch"), "pre_pelvis": avg(e["pre_snapshot"], "pelvis"),
              "crouch": avg(rows, "crouch"), "pelvis": avg(rows, "pelvis"), "half": avg(rows, "half"),
              "pelvis_max": round(max(r["pelvis"] for r in rows), 1),
              "speed": avg(rows, "speed"), "moving_share": avg([{"m": r["speed"] >= 200.0} for r in rows], "m"),
              "run_foot_chunks": rel["chunks"], "idle_run_foot_chunks": e["idle_chunks"][-8:],
              "montages": mont, "slots": {s: round(max(r[s] for r in rows), 2) for s in SLOTS}})

    def step(dt):
        st["t"] += dt
        if st["spawned"] < CFG["count"] and st["t"] >= st["next_spawn"]:
            pc.set_control_rotation(unreal.Rotator(roll=0.0, pitch=0.0, yaw=36.0 * st["spawned"]))
            if st["spawned"] > 0 or st["t"] > 0.3:
                cmd("summon " + NPC_CLASS)
                st["spawned"] += 1
            st["next_spawn"] = st["t"] + 0.7
        if st["t"] >= st["next_scan"]:
            scan()
            st["next_scan"] = st["t"] + 1.0
        for name in list(st["npcs"]):
            e = st["npcs"][name]
            try:
                row = snap(e, dt)
                reloading = e["gun"].is_reloading()
            except Exception:
                del st["npcs"][name]
                continue
            if reloading:
                if e["rel"] is None:
                    e["pre_snapshot"] = list(e["pre"])
                    e["rel"] = {"rows": [], "chunks": []}
                    e["chunk"], e["chunk_t"] = [], 0.0
                e["rel"]["rows"].append(row)
                chunk(e, row, e["rel"]["chunks"])
            else:
                if e["rel"] is not None:
                    flush(e, name)
                    e["rel"] = None
                    e["chunk"], e["chunk_t"] = [], 0.0
                chunk(e, row, e["idle_chunks"])
                e["pre"].append(row)
                if len(e["pre"]) > 20:
                    e["pre"].pop(0)
        if st["t"] >= CFG["seconds"]:
            unreal.unregister_slate_post_tick_callback(st["h"])
            cmd("polarity.ai.IgnorePlayers 1")
            unreal.log("FIGHT_RELOAD DONE npcs=%s" % sorted(x["gun_name"] for x in st["npcs"].values()))

    def tick(dt):
        try:
            if not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world():
                unreal.unregister_slate_post_tick_callback(st["h"])
                return
            step(dt)
        except Exception as exc:
            unreal.unregister_slate_post_tick_callback(st["h"])
            unreal.log_error("FIGHT_RELOAD FAILED %s" % exc)

    st["h"] = unreal.register_slate_post_tick_callback(tick)
    print("FIGHT_RELOAD ARMED")
