"""Запись боя на стенде: кто где стоял и что делал (2026-10-11).

Работает внутри редактора. Вешается на тик редактора и раз в полсекунды игрового времени пишет строку
на каждого AShooterNPC игрового мира в Saved/BenchTrack/track.csv. Перекомпиляции не нужно.

    start()   начать запись (файл перезаписывается)
    stop()    снять запись с тика

Столбцы: wall (секунды unix, чтобы сшить с логом [TACTIC]), t (время игры), name, team, x, y, hp, dead,
shooting, focus (в кого смотрит), los (может ли стрелять по нему), crouch, speed, spread, posture (поза
отряда), ratio (перевес), suppressed, element (в идущей половине штурма), commander.
Разбор и картинка: Source/Tools/CombatBench/analyze_fight.py (обычный python, не редактор).
"""

import builtins
import os
import time
import unreal

OUT_DIR = os.path.join(unreal.Paths.project_saved_dir(), "BenchTrack")
OUT = os.path.join(OUT_DIR, "track.csv")
PERIOD = 0.5


def _tick(_dt):
    st = builtins._bench_track
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
    if not world:
        return
    now = unreal.GameplayStatics.get_time_seconds(world)
    if now - st["last"] < PERIOD:
        return
    st["last"] = now
    wall = time.time()
    lines = []
    coord = unreal.AICombatCoordinator.get_coordinator(world)
    for n in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.ShooterNPC):
        c = n.get_controller()
        focus = c.get_focus_actor() if c else None
        loc = n.get_actor_location()
        acc = n.get_component_by_class(unreal.AIAccuracyComponent)
        posture = str(coord.get_posture_for(n)).split(".")[-1].split(":")[0] if coord else ""
        lines.append("{:.2f},{:.2f},{},{},{:.0f},{:.0f},{:.0f},{},{},{},{},{},{:.0f},{:.1f},{},{:.2f},{},{},{}".format(
            wall, now, n.get_name(), n.get_editor_property("team_byte"), loc.x, loc.y,
            n.get_editor_property("current_hp"), int(n.is_dead()), int(n.is_currently_shooting()),
            focus.get_name() if focus else "", int(bool(focus) and n.has_line_of_sight_to(focus)),
            int(n.character_movement.is_crouching()), n.get_velocity().length(),
            acc.get_editor_property("last_calculated_spread") if acc else 0.0,
            posture, coord.get_force_ratio_for(n) if coord else 1.0,
            int(bool(acc) and acc.is_suppressed()), int(bool(coord) and coord.is_in_assault_element(n)),
            int(bool(coord) and coord.is_squad_commander(n))))
    for pc in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.PlayerController):
        p = pc.get_controlled_pawn()
        if p:
            loc = p.get_actor_location()
            lines.append("{:.2f},{:.2f},{},{},{:.0f},{:.0f},0,0,0,,0,0,{:.0f},0,,1,0,0,0".format(
                wall, now, "PLAYER", 0, loc.x, loc.y, p.get_velocity().length()))
    if lines:
        with open(OUT, "a", encoding="utf-8") as f:
            f.write("\n".join(lines) + "\n")


def start():
    stop()
    os.makedirs(OUT_DIR, exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as f:
        f.write("wall,t,name,team,x,y,hp,dead,shooting,focus,los,crouch,speed,spread,posture,ratio,suppressed,element,commander\n")
    builtins._bench_track = {"last": -1.0}
    builtins._bench_track_handle = unreal.register_slate_post_tick_callback(_tick)
    print("TRACK started ->", OUT)


def stop():
    handle = getattr(builtins, "_bench_track_handle", None)
    if handle is not None:
        unreal.unregister_slate_post_tick_callback(handle)
        builtins._bench_track_handle = None
        print("TRACK stopped")
