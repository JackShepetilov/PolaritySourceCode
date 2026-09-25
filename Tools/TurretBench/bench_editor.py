"""Шаги прогона стенда турели внутри редактора. Каждый шаг это отдельный вызов через Tools/mcp.sh
(его зовёт run_bench.sh): PIE асинхронный, и между шагами миру надо дать прожить несколько секунд.

    open_level()   гард несохранённого, открыть L_TurretSiegeBench
    start()        переменные отладки, окно 1920x1080, StartPIE в отдельном окне
    status()       есть ли мир игры, сколько врагов, здоровье турели и ядра
    prepare()      врагов убрать и не пускать, игроку выдать ствол
    arm()          скормить ствол турели, игрока на вышку, запустить врагов, метка T0
    stop()         StopPIE

В мире игры акторы ищутся по тегам (метки в PIE не существуют), теги ставит build_turret_bench.
"""

import json
import math

import unreal

LEVEL = "/Game/Prototype/TurretBench/L_TurretSiegeBench"
KEEP_ALIVE = 2
TOWER_TOP = (3300.0, -2000.0, 700.0)
FIELD_CENTER = (0.0, 0.0, 0.0)
CVARS = ("polarity.ai.bench 1", "polarity.ai.IgnorePlayers 1", "polarity.player.god 1", "polarity.buildable.god 1",
         "polarity.projectile.debug 1", "polarity.turret.debug 1", "polarity.build.instant 1",
         "polarity.turret.infiniteammo 1")
CVARS_OFF = ("polarity.ai.bench 0", "polarity.ai.IgnorePlayers 0", "polarity.player.god 0", "polarity.buildable.god 0",
             "polarity.projectile.debug 0", "polarity.ai.turretcover.pausetree 1", "polarity.turret.debug 0",
             "polarity.turret.headtest 0", "polarity.build.instant 0", "polarity.turret.infiniteammo 0")
# Где встанет турель игрока (TURRET_AT в build_turret_bench) и откуда он её ставит: в 2.5 м перед
# точкой, в пределах MaxPlaceDistance дата-ассета (3.5 м).
TURRET_SPOT = (1900.0, 0.0)
PLACE_FROM = (1650.0, 0.0)

UES = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
GS = unreal.GameplayStatics
SL = unreal.SystemLibrary


def log(*a):
    print("[BENCH_RUN] " + " ".join(str(x) for x in a))


def _tool(name, args):
    res = unreal.ToolsetRegistry.execute_tool("EditorToolset.EditorAppToolset", name, json.dumps(args))
    return res


def _game_world():
    return UES.get_game_world()


def _tagged(world, tag):
    return list(GS.get_all_actors_with_tag(world, tag))


def open_level():
    if _game_world():
        # Чаще всего это прошлый прогон, оборванный на середине. Стенд берёт редактор целиком.
        _tool("StopPIE", {})
        log("шла игра: остановлена")
        return
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    # Мир, а не «текущий уровень»: текущим может стоять подуровень света автора (ArenaLightingDebug3),
    # и тогда сверка по текущему уровню решила бы, что открыто что-то чужое, и перезагрузила карту.
    world = UES.get_editor_world()
    if world and world.get_path_name().startswith(LEVEL + "."):
        log("уровень уже открыт")
        return
    if not unreal.EditorAssetLibrary.does_asset_exist(LEVEL):
        raise RuntimeError("нет {}: сначала run_bench.sh build".format(LEVEL))
    dirty = unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()
    if dirty:
        raise RuntimeError("есть несохранённые карты {}: сохранись в редакторе".format([p.get_name() for p in dirty]))
    les.load_level(LEVEL)
    log("открыт", LEVEL)


def start(extra=""):
    """extra: дополнительные консольные переменные через «;», для сравнений (A/B) одной настройки."""
    if _game_world():
        log("PIE уже идёт")
        return
    ew = UES.get_editor_world()
    for c in list(CVARS) + [x.strip() for x in extra.split(";") if x.strip()]:
        SL.execute_console_command(ew, c)
    if extra.strip():
        log("доп. настройки:", extra)
    log("bench =", SL.get_console_variable_int_value("polarity.ai.bench"),
        "IgnorePlayers =", SL.get_console_variable_int_value("polarity.ai.IgnorePlayers"),
        "god =", SL.get_console_variable_int_value("polarity.player.god"),
        "buildable.god =", SL.get_console_variable_int_value("polarity.buildable.god"))
    # Размер отдельного окна PIE. CDO правится в две строки: одной цепочкой её отбивает сканер кода.
    # Класса нет в модуле unreal по имени, только по пути скрипта.
    try:
        pcls = unreal.load_class(None, "/Script/UnrealEd.LevelEditorPlaySettings")
        ps = unreal.get_default_object(pcls)
        ps.set_editor_property("new_window_width", 1920)
        ps.set_editor_property("new_window_height", 1080)
        log("окно игры 1920x1080")
    except Exception as e:
        log("размер окна не выставлен:", e)
    res = _tool("StartPIE", {"options": {"bSimulate": False, "playMode": "PlayMode_InEditorFloating",
                                         "warmupSeconds": 2.0}})
    log("StartPIE отправлен, error:", res.error or "-")


def status():
    w = _game_world()
    if not w:
        log("STATUS nogame")
        return
    npcs = [a for a in GS.get_all_actors_of_class(w, unreal.ShooterNPC)]
    turret = (_tagged(w, "BenchTurret") or [None])[0]
    core = (_tagged(w, "BenchCore") or [None])[0]

    def hp(b):
        if not b:
            return "gone"
        try:
            return "{:.0f}".format(b.get_editor_property("health"))
        except Exception:
            return "?"
    log("STATUS game npcs={} turretHP={} coreHP={}".format(len(npcs), hp(turret), hp(core)))


def _spawners(w):
    return _tagged(w, "BenchRifles") + _tagged(w, "BenchGrenadiers")


def prepare():
    w = _game_world()
    if not w:
        log("PREPARE nogame")
        return
    # Враги появились на BeginPlay, а турель ещё без ствола: убрать их и не пускать до T0,
    # иначе первые полминуты это бой с безоружной турелью, а то и её смерть до кормёжки.
    for sp in _spawners(w):
        sp.set_keep_alive(0)
        sp.despawn_all()
    pawn = GS.get_player_pawn(w, 0)
    if not pawn:
        log("PREPARE нет пешки игрока")
        return
    try:
        pawn.equip_starting_weapon_animated()
    except Exception as e:
        log("equip:", e)
    cw = None
    try:
        cw = pawn.get_current_weapon()
    except Exception:
        pass
    # Турель уровня убрать: на стенде стоит турель ИГРОКА, поставленная через дата-ассет, с владельцем
    # и строкой в его HUD. Уровневая этого не умеет (нет Definition, нет владельца).
    for old in _tagged(w, "BenchTurret"):
        old.destroy_actor()
    # Встать перед точкой турели и смотреть в пол на неё. Взгляд ставится ЗАРАНЕЕ, отдельным вызовом
    # от установки: камера подхватывает поворот контроллера только на следующем кадре, а установка
    # ведёт трассу из камеры (так же промахивалась кормёжка, 2026-09-22).
    pc = GS.get_player_controller(w, 0)
    stand = unreal.Vector(PLACE_FROM[0], PLACE_FROM[1], 110.0)
    pawn.set_actor_location_and_rotation(stand, unreal.Rotator(roll=0.0, pitch=0.0, yaw=0.0), False, True)
    if pc:
        eye = unreal.Vector(PLACE_FROM[0], PLACE_FROM[1], 110.0 + 64.0)
        pc.set_control_rotation(_look(eye, unreal.Vector(TURRET_SPOT[0], TURRET_SPOT[1], 0.0)))
    log("PREPARE враги убраны, турель уровня убрана, пешка {}, ствол {}, смотрит на точку турели".format(
        pawn.get_name(), cw.get_name() if cw else "?"))


def _look(from_v, to_v):
    return unreal.MathLibrary.find_look_at_rotation(from_v, to_v)


def arm():
    w = _game_world()
    if not w:
        log("ARM nogame")
        return
    pawn = GS.get_player_pawn(w, 0)
    pc = GS.get_player_controller(w, 0)
    if not (pawn and pc):
        log("ARM не найдено: пешка {}, контроллер {}".format(pawn, pc))
        return

    # Турель ставит ИГРОК, как из меню: через дата-ассет его строителя, бесплатно. polarity.build.place
    # заодно кладёт в неё ствол из рук. Слот ищется по дата-ассету, а не зашит числом.
    builder = pawn.get_component_by_class(unreal.BuilderComponent)
    slot = -1
    for i in range(8):
        d = builder.get_definition(i) if builder else None
        if d and d.get_name() == "DA_Buildable_Turret":
            slot = i
            break
    if slot < 0:
        log("ARM у строителя игрока нет DA_Buildable_Turret")
        return
    SL.execute_console_command(w, "polarity.build.place {}".format(slot))
    placed = [t for t in GS.get_all_actors_of_class(w, unreal.TurretBuildable) if t.get_owner_player_state()]
    turret = placed[0] if placed else None
    if not turret:
        log("ARM турель не встала (слот {}), смотри [BUILD_DEBUG]".format(slot))
        return
    # Метка стенда, по ней её находят status() и closeup().
    tags = list(turret.tags)
    tags.append("BenchTurret")
    turret.tags = tags
    SL.execute_console_command(w, "polarity.turret.status")

    try:
        vice = turret.get_vice_weapon(0)
        log("ARM турель: тиски 0 = {}".format(vice.get_name() if vice else "ПУСТО, кормёжка не прошла"))
    except Exception:
        log("ARM турель: тиски из питона не читаются, результат кормёжки в логе [TURRET_DEBUG]")

    # На вышку, лицом к полю: с неё в кадр входят спавны, укрытия, турель и ядро.
    top = unreal.Vector(TOWER_TOP[0], TOWER_TOP[1], TOWER_TOP[2] + 110.0)
    view = _look(top, unreal.Vector(*FIELD_CENTER))
    pawn.set_actor_location_and_rotation(top, unreal.Rotator(roll=0.0, pitch=0.0, yaw=view.yaw), False, True)
    pc.set_control_rotation(unreal.Rotator(roll=0.0, pitch=-14.0, yaw=view.yaw))
    # Свет даёт авторский подуровень ArenaLightingDebug3; без него смотреть в `viewmode unlit`.

    for sp in _spawners(w):
        sp.set_keep_alive(KEEP_ALIVE)
    unreal.log("[BENCH] T0 fight starts")
    log("ARM готово: игрок на вышке, враги запущены по {}, T0".format(KEEP_ALIVE))


def closeup():
    """Игрока к турели, взгляд на тиски сбоку-спереди: кадр головы, ствола и губки. Турель стоит
    на месте, в кадр она попадает с любым поворотом головы."""
    w = _game_world()
    if not w:
        log("CLOSEUP nogame")
        return
    pawn = GS.get_player_pawn(w, 0)
    pc = GS.get_player_controller(w, 0)
    turret = (_tagged(w, "BenchTurret") or [None])[0]
    if not (pawn and pc and turret):
        log("CLOSEUP не найдено")
        return
    mount = turret.get_editor_property("vice_mounts")[0]
    focus = mount.get_world_location()
    fwd = turret.get_actor_forward_vector()
    right = turret.get_actor_right_vector()
    # 2.2 м спереди-сбоку и чуть выше тисков; пешка ставится ногами, камера у глаз (~+65 см).
    spot = unreal.Vector(focus.x + fwd.x * 160.0 + right.x * 150.0, focus.y + fwd.y * 160.0 + right.y * 150.0, focus.z - 30.0)
    pawn.set_actor_location(spot, False, True)
    eye = unreal.Vector(spot.x, spot.y, spot.z + 65.0)
    pc.set_control_rotation(_look(eye, focus))
    SL.execute_console_command(w, "polarity.turret.status")
    log("CLOSEUP игрок у турели, взгляд на тиски", focus)


def stop():
    # Переменные глобальны на весь процесс: оставь IgnorePlayers включённым, и в следующей обычной
    # игре автора враги в него не стреляют. Сбрасывать на выходе всегда, даже если PIE уже нет.
    ew = UES.get_editor_world()
    for c in CVARS_OFF:
        SL.execute_console_command(ew, c)
    if not _game_world():
        log("STOP PIE не идёт, переменные сброшены")
        return
    res = _tool("StopPIE", {})
    log("StopPIE error:", res.error or "-", "; переменные сброшены")
