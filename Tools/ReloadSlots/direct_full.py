# One montage after another on a live NPC running in a circle, whole length, foot travel per half second.
import json
import math

import unreal

OUT = unreal.Paths.project_saved_dir() + "ReloadSlots/direct_full.jsonl"
LIST = ["/Game/InfimaGames/AnimatedLowPolyWeapons/Art/Characters/Animations/ARs/AM_TP_CH_AR_02_Reload_Empty",
        "/Game/Variant_Shooter/Anims/TP_Reload/AM_AR02_TP_Reload_Empty",
        "/Game/InfimaGames/AnimatedLowPolyWeapons/Art/Characters/Animations/ARs/AM_TP_CH_AR_01_Reload_Empty",
        "/Game/Variant_Shooter/Anims/TP_Reload/AM_AR01_TP_Reload_Empty"]


def arm():
    w = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
    if not w:
        print("NO PIE")
        return
    pawn = unreal.GameplayStatics.get_player_pawn(w, 0)
    npcs = [a for a in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.ShooterNPC)
            if a.get_class().get_name().startswith("BP_ShooterNPC")]
    npcs.sort(key=lambda a: (a.get_actor_location() - pawn.get_actor_location()).length())
    npc = npcs[0]
    body = npc.get_editor_property("mesh")
    anim = body.get_anim_instance()
    monts = [unreal.load_object(None, p + "." + p.split("/")[-1]) for p in LIST]
    open(OUT, "w").close()
    st = {"t": 0.0, "i": -1, "t0": 0.0, "rows": [], "ct": 0.0, "chunks": [], "h": None}

    def tick(dt):
        try:
            st["t"] += dt
            a = st["t"] * 1.2
            npc.add_movement_input(unreal.Vector(math.cos(a), math.sin(a), 0.0), 1.0, True)
            el = st["t"] - st["t0"]
            if st["i"] >= 0:
                f = body.get_socket_transform("foot_l", unreal.RelativeTransformSpace.RTS_COMPONENT).translation
                v = npc.get_velocity()
                st["rows"].append((f.x, f.y, f.z, math.hypot(v.x, v.y)))
                st["ct"] += dt
                if st["ct"] >= 0.5:
                    r = st["rows"]
                    st["chunks"].append([round(max(max(x[k] for x in r) - min(x[k] for x in r) for k in range(3)), 1),
                                         round(sum(x[3] for x in r) / len(r))])
                    st["rows"], st["ct"] = [], 0.0
            if st["i"] < 0 and el < 1.0:
                return
            if st["i"] < 0 or el >= monts[st["i"]].get_play_length():
                if st["i"] >= 0:
                    with open(OUT, "a", encoding="utf-8") as fh:
                        fh.write(json.dumps({"montage": monts[st["i"]].get_name(), "foot_speed": st["chunks"]}) + "\n")
                st["i"] += 1
                st.update(t0=st["t"], rows=[], ct=0.0, chunks=[])
                if st["i"] >= len(monts):
                    unreal.unregister_slate_post_tick_callback(st["h"])
                    unreal.log("DIRECT_FULL DONE")
                    return
                anim.montage_play(monts[st["i"]], 1.0)
        except Exception as exc:
            unreal.unregister_slate_post_tick_callback(st["h"])
            unreal.log_error("DIRECT_FULL FAILED %s" % exc)

    st["h"] = unreal.register_slate_post_tick_callback(tick)
    print("DIRECT_FULL ARMED on", npc.get_name())
