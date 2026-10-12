"""Проверка брони зоной, каски и лута в живом PIE на L_DamagePlayground (2026-10-12).
Стреляет не оружием, а точечным уроном в названную кость: проверяется воронка урона, не прицел.

    start()     окно 1920x1080, PIE в отдельном окне
    prep()      игрока не трогают, подписи попаданий включены, ствол в руках
    torso()     грунт 1: три попадания по 30 в spine_03, потом добить в торс
    limbs()     грунт 2: предплечье и голень по 30, потом добить в голень
    head_a()    грунт 3: 30 + 30 в голову (второй должен уронить)
    head_b()    грунт 3: ещё 30 + 30 (каска слетает), потом 10 в голову (смерть)
    state()     здоровье, броня, каска каждого грунта ряда и что лежит на телах

Запуск: Tools/dp_step.sh verify_body_armor <функция>. Итог читать в логе по [HITPART], [HELMET_DEBUG],
[CARE] и [VERIFY].
"""

import json
import unreal

GS = unreal.GameplayStatics
SL = unreal.SystemLibrary
UES = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)


def log(*a):
    print("[VERIFY] " + " ".join(str(x) for x in a))


def _world():
    return UES.get_game_world()


def start():
    if _world():
        log("PIE уже идёт")
        return
    pcls = unreal.load_class(None, "/Script/UnrealEd.LevelEditorPlaySettings")
    ps = unreal.get_default_object(pcls)
    ps.set_editor_property("NewWindowWidth", 1920)
    ps.set_editor_property("NewWindowHeight", 1080)
    res = unreal.ToolsetRegistry.execute_tool("EditorToolset.EditorAppToolset", "StartPIE", json.dumps(
        {"options": {"bSimulate": False, "playMode": "PlayMode_InEditorFloating", "warmupSeconds": 2.0}}))
    log("StartPIE отправлен, error:", res.error or "-")


def stop():
    res = unreal.ToolsetRegistry.execute_tool("EditorToolset.EditorAppToolset", "StopPIE", "{}")
    log("StopPIE отправлен, error:", res.error or "-")


def front():
    """Тот же выстрел в грудь, но спереди: во что он приходит на самом деле."""
    _hit(4, "spine_03", 30.0)
    log(_line(4))


def prep():
    w = _world()
    if not w:
        log("нет игры")
        return
    for c in ("polarity.ai.IgnorePlayers 1", "polarity.debug.hitparts 1"):
        SL.execute_console_command(w, c)
    pawn = GS.get_player_pawn(w, 0)
    try:
        pawn.equip_starting_weapon_animated()
    except Exception as e:
        log("ствол не выдан:", str(e)[:80])
    log("pawn", pawn.get_name() if pawn else None, "at", pawn.get_actor_location() if pawn else None)


def _grunt(n):
    w = _world()
    tag = "DamagePlaygroundParts"
    row = [a for a in GS.get_all_actors_of_class(w, unreal.ShooterNPC) if a.actor_has_tag(tag)]
    row.sort(key=lambda a: a.get_actor_location().y)
    return row[n - 1] if len(row) >= n else None


def _hit(n, bone, dmg, side=-1.0):
    w = _world()
    g = _grunt(n)
    if not g:
        log("нет грунта", n)
        return
    pawn = GS.get_player_pawn(w, 0)
    pc = GS.get_player_controller(w, 0)
    # Настоящая трасса по мешу, как у оружия (ResolveHitBone): кость приходит из физассета, так что
    # заодно видно, как тело на самом деле называется.
    where = g.mesh.get_socket_location(bone)
    # side -1: спереди (грунт смотрит на игрока, в минус X), +1: со спины. Спереди грудь закрыта
    # предплечьями со стволом: трасса в spine_03 приходит в lowerarm_l (замер 2026-10-12).
    start = where + unreal.Vector(150.0 * side, 0.0, 0.0)
    res = g.mesh.line_trace_component(start, where - unreal.Vector(60.0 * side, 0.0, 0.0), False, False, False)
    hit = res[-1] if isinstance(res, tuple) else res
    GS.apply_point_damage(g, dmg, unreal.Vector(1, 0, 0), hit, pc, pawn, unreal.DamageType_Ranged)


def _line(n):
    g = _grunt(n)
    if not g:
        return "grunt {}: gone".format(n)
    sh = g.get_component_by_class(unreal.ShieldFieldComponent)
    he = g.get_component_by_class(unreal.HelmetComponent)
    return "grunt {}: hp {:.0f} armor {:.0f} helmet {} dead {} down {}".format(
        n, g.get_editor_property("current_hp"), sh.get_current_shield() if sh else -1,
        "{:.0f}".format(he.get_helmet_fraction() * he.get_editor_property("helmet_pool")) if he else "none",
        g.is_dead(), g.is_knocked_down())


def state():
    w = _world()
    if not w:
        log("нет игры")
        return
    for n in (1, 2, 3, 4):
        log(_line(n))
    for a in GS.get_all_actors_of_class(w, unreal.StrippableArmor):
        body = a.get_editor_property("body")
        log("armor on body {}: {:.1f} (fraction {:.2f}) glow {}".format(
            body.get_name() if body else None, a.get_editor_property("armor_amount"),
            a.get_editor_property("armor_fraction"),
            body.mesh.get_overlay_material().get_name() if body and body.mesh.get_overlay_material() else None))


def torso():
    for _i in range(3):
        _hit(1, "spine_03", 30.0, 1.0)
        log(_line(1))
    for _i in range(2):
        _hit(1, "spine_03", 30.0, 1.0)
    log("after kill:", _line(1))


def limbs():
    _hit(2, "lowerarm_l", 30.0)
    log(_line(2))
    _hit(2, "calf_r", 30.0)
    log(_line(2))
    for _i in range(4):
        _hit(2, "calf_r", 30.0)
    log("after kill:", _line(2))


def head_a():
    _hit(3, "head", 30.0)
    log(_line(3))
    _hit(3, "head", 30.0)
    log(_line(3))


def head_b():
    _hit(3, "head", 30.0)
    log(_line(3))
    _hit(3, "head", 30.0)
    log(_line(3))
    _hit(3, "head", 10.0)
    log("after bare head:", _line(3))


# ==================== настоящие пули игрока ====================
# (грунт ряда, подпись, кость, вторая кость или None, чего ждём). Точка прицела: середина между
# двумя костями (кость это сустав, а не середина сегмента), у одиночной кости сама кость.
SHOTS = (
    (1, "chest", "spine_03", None, "ARMOR"),
    (1, "belly", "spine_01", None, "ARMOR"),
    (1, "pelvis", "pelvis", None, "ARMOR"),
    (1, "upper arm L", "upperarm_l", "lowerarm_l", "ARMOR"),
    (1, "thigh L", "thigh_l", "calf_l", "ARMOR"),
    (2, "forearm L", "lowerarm_l", "hand_l", "LIMB"),
    (2, "forearm R", "lowerarm_r", "hand_r", "LIMB"),
    (2, "shin L", "calf_l", "foot_l", "LIMB"),
    (2, "shin R", "calf_r", "foot_r", "LIMB"),
    (4, "upper arm R", "upperarm_r", "lowerarm_r", "ARMOR"),
    (4, "thigh R", "thigh_r", "calf_r", "ARMOR"),
    (4, "hand L", "hand_l", None, "LIMB"),
    (4, "foot L", "foot_l", "ball_l", "LIMB"),
    (4, "neck", "neck_01", None, "ARMOR"),
    (3, "head", "head", None, "HELMET"),
    (3, "head", "head", None, "HELMET"),
)
STAND_OFF = 350.0
_shoot = {"handle": None}


def shoot():
    """Игрок встаёт перед грунтом, наводит прицел в кость и стреляет от бедра настоящей пулей, по
    одной на строку SHOTS. Идёт сам, по кадрам; в лог пишет [SHOT] перед каждым выстрелом, оружие
    отвечает [HITPART]. Нужны polarity.debug.nospread 1 и hitparts 1 (ставит сам)."""
    w = _world()
    if not w:
        log("нет игры")
        return
    if _shoot["handle"]:
        log("уже стреляю")
        return
    for c in ("polarity.ai.IgnorePlayers 1", "polarity.debug.hitparts 1", "polarity.debug.nospread 1",
              "polarity.debug.infiniteammo 1"):
        SL.execute_console_command(w, c)
    st = {"i": 0, "phase": 0, "t": 0.0}

    def point(g, a, b):
        p = g.mesh.get_socket_location(a)
        if b:
            p = (p + g.mesh.get_socket_location(b)) * 0.5
        elif a == "head":
            p = p + unreal.Vector(0.0, 0.0, 8.0)
        return p

    def aim(g, a, b):
        pc = GS.get_player_controller(w, 0)
        cam = GS.get_player_camera_manager(w, 0).get_camera_location()
        pc.set_control_rotation(unreal.MathLibrary.find_look_at_rotation(cam, point(g, a, b)))

    def tick(dt):
        try:
            if not UES.get_game_world() or st["i"] >= len(SHOTS):
                unreal.unregister_slate_post_tick_callback(_shoot["handle"])
                _shoot["handle"] = None
                unreal.log("[SHOT] DONE")
                return
            n, label, a, b, want = SHOTS[st["i"]]
            g = _grunt_by_slot(n)
            pawn = GS.get_player_pawn(w, 0)
            st["t"] += dt
            if not g or g.is_dead():
                unreal.log("[SHOT] {} SKIP {} (grunt {} gone or dead)".format(st["i"], label, n))
                st["i"] += 1
                st["phase"], st["t"] = 0, 0.0
            elif st["phase"] == 0:
                loc = g.get_actor_location()
                pawn.set_actor_location(unreal.Vector(loc.x - STAND_OFF, loc.y, pawn.get_actor_location().z), False, False)
                aim(g, a, b)
                st["phase"], st["t"] = 1, 0.0
            elif st["phase"] == 1 and st["t"] > 0.5:
                aim(g, a, b)
                st["phase"], st["t"] = 2, 0.0
            elif st["phase"] == 2 and st["t"] > 0.25:
                # На какой кости стоит прицел: трасса по мешу из камеры по взгляду. С ней и сравнивать
                # кость, в которую пришла пуля.
                cam = GS.get_player_camera_manager(w, 0)
                c0 = cam.get_camera_location()
                c1 = c0 + cam.get_camera_rotation().get_forward_vector() * 2000.0
                res = g.mesh.line_trace_component(c0, c1, False, False, False)
                names = [str(x) for x in (res if isinstance(res, tuple) else (res,)) if isinstance(x, unreal.Name)]
                unreal.log("[SHOT] {} grunt {} aim {} want {} crosshair_on {}".format(st["i"], n, label, want, names[0] if names else "?"))
                pawn.do_start_firing()
                st["phase"], st["t"] = 3, 0.0
            elif st["phase"] == 3 and st["t"] > 0.1:
                pawn.do_stop_firing()
                st["phase"], st["t"] = 4, 0.0
            elif st["phase"] == 4 and st["t"] > 1.0:
                st["i"] += 1
                st["phase"], st["t"] = 0, 0.0
        except Exception as e:
            unreal.log("[SHOT] ERROR {}".format(e))
            unreal.unregister_slate_post_tick_callback(_shoot["handle"])
            _shoot["handle"] = None

    _shoot["handle"] = unreal.register_slate_post_tick_callback(tick)
    log("стрельба пошла: {} выстрелов".format(len(SHOTS)))


_slots = {}


def _grunt_by_slot(n):
    """Грунт ряда по номеру места. Места запоминаются при первом вызове: мёртвый не сдвигает счёт."""
    if not _slots:
        w = _world()
        row = [a for a in GS.get_all_actors_of_class(w, unreal.ShooterNPC) if a.actor_has_tag("DamagePlaygroundParts")]
        row.sort(key=lambda a: a.get_actor_location().y)
        for i, a in enumerate(row):
            _slots[i + 1] = a
    return _slots.get(n)


# ==================== падение от попадания в голову ====================
_watch = {"handle": None}


def _watch_fall(g, seconds=2.6, wall_x=None):
    """Раз в 0.2 с игрового времени пишет, где капсула и где лежит тело. wall_x: лицо стены за
    бойцом; минус в столбце clear значит, что кость ушла за стену."""
    w = _world()
    t0 = GS.get_time_seconds(w)
    st = {"next": 0.0}

    def tick(_dt):
        try:
            ww = UES.get_game_world()
            if not ww:
                raise RuntimeError("игра кончилась")
            t = GS.get_time_seconds(ww) - t0
            if t >= st["next"]:
                st["next"] += 0.2
                a = g.get_actor_location()
                bones = {b: g.mesh.get_socket_location(b) for b in ("head", "pelvis", "foot_l", "hand_r")}
                far = max(v.x for v in bones.values())
                line = "t {:.1f} capsule ({:.0f},{:.0f},{:.0f}) yaw {:.0f} head ({:.0f},{:.0f},{:.0f}) pelvis z {:.0f} down {}".format(
                    t, a.x, a.y, a.z, g.get_actor_rotation().yaw, bones["head"].x, bones["head"].y, bones["head"].z,
                    bones["pelvis"].z, g.is_knocked_down())
                if wall_x is not None:
                    line += " clear {:.0f}".format(wall_x - far)
                unreal.log("[FALL] " + line)
            if t > seconds:
                raise StopIteration()
        except Exception as e:
            if not isinstance(e, StopIteration):
                unreal.log("[FALL] ERROR {}".format(e))
            unreal.unregister_slate_post_tick_callback(_watch["handle"])
            _watch["handle"] = None
            unreal.log("[FALL] DONE")

    _watch["handle"] = unreal.register_slate_post_tick_callback(tick)


def _knock(n, behind=False, wall_x=None):
    """Два попадания по 30 в голову грунта n (второе роняет). Игрок стоит спереди или сзади: он и
    есть стрелок, от него считается сторона."""
    w = _world()
    g = _grunt_by_slot(n)
    pawn = GS.get_player_pawn(w, 0)
    loc = g.get_actor_location()
    fwd = g.get_actor_forward_vector()
    side = -1.0 if behind else 1.0
    pawn.set_actor_location(unreal.Vector(loc.x + fwd.x * 350.0 * side, loc.y + fwd.y * 350.0 * side, pawn.get_actor_location().z), False, False)
    pc = GS.get_player_controller(w, 0)
    head = g.mesh.get_socket_location("head")
    for _i in range(2):
        start = head + fwd * (150.0 * side)
        res = g.mesh.line_trace_component(start, head - fwd * (60.0 * side), False, False, False)
        GS.apply_point_damage(g, 30.0, fwd * (-side), res[-1] if isinstance(res, tuple) else res, pc, pawn, unreal.DamageType_Ranged)
    log("knock grunt {} from {}: yaw before {:.0f}, player at ({:.0f},{:.0f})".format(
        n, "behind" if behind else "front", g.get_actor_rotation().yaw, pawn.get_actor_location().x, pawn.get_actor_location().y))
    _watch_fall(g, wall_x=wall_x)


def slow(scale="0.1"):
    SL.execute_console_command(_world(), "polarity.debug.timescale " + str(scale))
    log("timescale", scale)


def fall_front():
    _knock(3)


def fall_back():
    _knock(2, behind=True)


def _wall_ahead(start, direction, reach=1500.0):
    hit = SL.line_trace_single(_world(), start, start + direction * reach, unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, False, [],
                               unreal.DrawDebugTrace.NONE, True)
    return hit.to_tuple()[4] if hit else None


def fall_wall(gap=110.0):
    """Грунт 4 ставится спиной к заднику стенда на gap см, игрок спереди. Падать назад некуда."""
    w = _world()
    g = _grunt_by_slot(4)
    loc = g.get_actor_location()
    wall = _wall_ahead(unreal.Vector(1900.0, loc.y, loc.z), unreal.Vector(1.0, 0.0, 0.0))
    if wall is None:
        log("стена не найдена")
        return
    g.set_actor_location(unreal.Vector(wall.x - gap, loc.y, loc.z), False, False)
    g.set_actor_rotation(unreal.Rotator(roll=0.0, pitch=0.0, yaw=180.0), False)
    log("wall face x {:.0f}, grunt 4 at x {:.0f} (gap {:.0f})".format(wall.x, wall.x - gap, gap))
    _knock(4, wall_x=wall.x)
