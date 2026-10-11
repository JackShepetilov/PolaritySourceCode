"""Разбор записи боя. Обычный python, не редактор.

    python analyze_fight.py [папка боя]

Запись делает сама игра (UFightRecorderSubsystem): каждый бой с пульта стенда пишется в свою папку
Saved/BenchTrack/<дата>_<сценарий>/ (track.csv, events.csv, geometry.csv, summary.txt). Без аргумента
берётся последний бой (Saved/BenchTrack/last.txt).

Пишет в папку боя:
    map.png       вид сверху: следы, где стояли, где погибли
    frames.png    шесть снимков по всему бою
    start.png     шесть снимков первых секунд после контакта (кто как отреагировал)
    timeline.png  кто когда стрелял, шёл, стоял
    report.txt    таблицы: реакция на контакт, стрельба и попадания, простои, позы
"""

import csv
import io
import os
import re
import sys
from collections import defaultdict

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle

ROOT = r"C:/Users/Professional/Documents/Unreal Projects/Polarity_Main5_8"
TRACK_ROOT = os.path.join(ROOT, "Saved", "BenchTrack")
if len(sys.argv) > 1:
    OUT = sys.argv[1]
else:
    OUT = io.open(os.path.join(TRACK_ROOT, "last.txt"), encoding="utf-8").read().strip()
print("бой:", OUT)


def is_player(name):
    return name.startswith("PLAYER")


# ---------- track.csv ----------
rows = list(csv.DictReader(io.open(os.path.join(OUT, "track.csv"), encoding="utf-8")))
for r in rows:
    for k in ("t", "x", "y", "hp", "speed", "spread"):
        r[k] = float(r[k])
    for k in ("team", "dead", "shooting", "los", "crouch"):
        r[k] = int(r[k])
    r["ratio"] = float(r.get("ratio") or 1.0)
    for k in ("suppressed", "element", "commander"):
        r[k] = int(r.get(k) or 0)
    r["posture"] = (r.get("posture") or "").upper()
    r["z"] = float(r.get("z") or 0.0)
by = defaultdict(list)
for r in rows:
    by[r["name"]].append(r)
T_END = max(r["t"] for r in rows)
players = sorted(n for n in by if is_player(n))
names = sorted((n for n in by if not is_player(n)), key=lambda n: (by[n][0]["team"], int(re.sub(r"\D", "", n) or 0)))


def is_red(name):
    return by[name][0]["team"] == 1


short = {n: ("R" if is_red(n) else "B") + re.sub(r"\D", "", n) for n in names}
for n in players:
    short[n] = "YOU" if n == "PLAYER" else "P" + n.split("_")[-1]


def colour(name):
    if is_player(name):
        return "#0a8a2a"
    return "#d02a1f" if is_red(name) else "#1f5fd0"


# ---------- events.csv ----------
events = []
ev_path = os.path.join(OUT, "events.csv")
if os.path.exists(ev_path):
    for e in csv.DictReader(io.open(ev_path, encoding="utf-8")):
        e["t"] = float(e["t"])
        events.append(e)
notes = defaultdict(list)
ducks = defaultdict(list)          # кто -> [(t, причина)]: каждый нырок, а не слово бойца (то не пишется дважды подряд)
hurts = defaultdict(list)          # кто -> [(t, {dmg, sum, hp, crouch, reload, speed, doing})]: каждое попадание по врагу
guns = {}                          # кто -> класс оружия (пишется на первом выстреле)
shots = defaultdict(list)          # кто -> [(t, в кого, пристрелочный)]
hits = defaultdict(list)           # кто -> [(t, по кому, урон)]
taken = defaultdict(list)          # кто -> [(t, от кого, урон)]
deaths = {}                        # кто -> (t, кем)
marks = []                         # (t, что случилось с боем: тревога, подмога)
scenario, outcome = "", ""
for e in events:
    if e["event"] == "tactic" and e["extra"].startswith("DUCK:"):
        ducks[e["who"]].append((e["t"], e["extra"][5:].strip()))
    elif e["event"] == "tactic" and e["extra"].startswith("HURT:"):
        body, _sep, doing = e["extra"][5:].partition(" doing=")
        rec = {"doing": doing}
        for part in body.split():
            k, _eq, v = part.partition("=")
            rec[k] = float(v)
        hurts[e["who"]].append((e["t"], rec))
    elif e["event"] == "gun":
        guns[e["who"]] = re.sub(r"^BP_|_C$", "", e["extra"])
    elif e["event"] == "tactic":
        notes[e["who"]].append((e["t"], e["extra"]))
    elif e["event"] == "shot":
        shots[e["who"]].append((e["t"], e["other"], e["value"] == "1"))
    elif e["event"] == "hit":
        hits[e["who"]].append((e["t"], e["other"], float(e["value"])))
        taken[e["other"]].append((e["t"], e["who"], float(e["value"])))
    elif e["event"] == "death":
        deaths[e["who"]] = (e["t"], e["other"])
    elif e["event"] == "mark":
        marks.append((e["t"], e["extra"]))
    elif e["event"] == "start":
        scenario = (scenario + " + " if scenario else "") + e["who"]
    elif e["event"] == "end":
        outcome = e["extra"]


def note_at(name, t):
    last = ""
    for nt, text in notes.get(name, ()):
        if nt > t:
            break
        last = text
    return last


def alive_rows(name):
    return [r for r in by[name] if not r["dead"]]


# ---------- geometry.csv: коробки уровня вокруг боя ----------
boxes = []
floor_z = min(r["z"] for r in rows) - 95.0
geo_path = os.path.join(OUT, "geometry.csv")
if os.path.exists(geo_path):
    for line in io.open(geo_path, encoding="utf-8").read().splitlines()[1:]:
        if line.startswith("#"):
            continue
        cx, cy, sx, sy, z0, z1 = (float(v) for v in line.split(","))
        if z1 < floor_z + 60.0 or z0 > floor_z + 200.0:
            continue        # пол, потолок, галерея над головой
        if sx > 3000.0 and sy > 3000.0:
            continue        # плита пола
        boxes.append((cx, cy, sx, sy, z1 - floor_z))
X0, X1 = min(r["x"] for r in rows) - 1200.0, max(r["x"] for r in rows) + 1200.0
Y0, Y1 = min(r["y"] for r in rows) - 1200.0, max(r["y"] for r in rows) + 1200.0


def draw_level(ax):
    for (cx, cy, sx, sy, h) in boxes:
        ax.add_patch(Rectangle((cx - sx / 2, cy - sy / 2), sx, sy,
                               fc="#c9b06a" if h > 150 else "#e9dba8", ec="#7a6a3a", lw=0.5))
    ax.set_xlim(X0, X1)
    ax.set_ylim(Y0, Y1)
    ax.set_aspect("equal")
    ax.set_xticks([])
    ax.set_yticks([])


everyone = names + players

# ---------- map.png ----------
fig, ax = plt.subplots(figsize=(13, 13))
draw_level(ax)
for n in everyone:
    rs = alive_rows(n)
    if not rs:
        continue
    col = colour(n)
    ax.plot([r["x"] for r in rs], [r["y"] for r in rs], color=col, lw=1.4 if is_player(n) else 0.8, alpha=0.5)
    still = [r for r in rs if r["speed"] < 20]
    ax.scatter([r["x"] for r in still], [r["y"] for r in still], s=10, color=col, alpha=0.18, lw=0)
    ax.scatter([rs[0]["x"]], [rs[0]["y"]], s=45, facecolor="white", edgecolor=col, lw=1.5, zorder=5)
    last = rs[-1]
    died = n in deaths or len(rs) < len(by[n])
    ax.scatter([last["x"]], [last["y"]], s=90, marker="x" if died else "o", color=col, zorder=6)
    ax.annotate(short[n], (last["x"], last["y"]), textcoords="offset points", xytext=(6, 6), fontsize=9, color=col)
ax.set_title("{}. Белый кружок: старт. Крест: погиб. Точка: где был в конце. Зелёный: игрок. "
             "Тёмные блоки выше 1.5 м, светлые ниже".format(scenario), fontsize=10)
fig.savefig(os.path.join(OUT, "map.png"), dpi=110, bbox_inches="tight")
plt.close(fig)


# ---------- frames.png и start.png ----------
def draw_frames(times, title, path):
    fig, axes = plt.subplots(2, 3, figsize=(18, 13))
    for ax, tt in zip(axes.flat, times):
        draw_level(ax)
        for n in everyone:
            rs = [r for r in by[n] if r["t"] <= tt + 0.01]
            if not rs:
                continue
            r = rs[-1]
            col = colour(n)
            if r["dead"] or (not is_player(n) and tt - r["t"] > 1.5):
                ax.scatter([r["x"]], [r["y"]], s=40, marker="x", color=col, alpha=0.35)
                continue
            ax.scatter([r["x"]], [r["y"]], s=110 if is_player(n) else 70, color=col,
                       edgecolor="black" if r["shooting"] else "none", lw=1.6, zorder=5,
                       marker="*" if is_player(n) else ("^" if r["element"] else ("s" if r["commander"] else "o")))
            label = short[n] if is_player(n) else "{} {}".format(short[n], note_at(n, tt)[:18])
            ax.annotate(label, (r["x"], r["y"]), textcoords="offset points", xytext=(5, 5), fontsize=7, color=col)
            if r["focus"] in by and r["los"]:
                fr = [q for q in by[r["focus"]] if q["t"] <= tt + 0.01]
                if fr:
                    ax.plot([r["x"], fr[-1]["x"]], [r["y"], fr[-1]["y"]], color=col, lw=0.6, alpha=0.5)
        ax.set_title("t = {:.1f} с".format(tt), fontsize=11)
    fig.suptitle(title, fontsize=12)
    fig.savefig(path, dpi=90, bbox_inches="tight")
    plt.close(fig)


LEGEND = "Обводка: стреляет. Линия: видит цель. Звезда: игрок. Треугольник: идёт в штурм. Квадрат: командир. Крест: погиб"
draw_frames([T_END * k / 5.0 for k in range(6)], "Весь бой. " + LEGEND, os.path.join(OUT, "frames.png"))

# Контакт: первый раз, когда кто-то видит свою цель.
sight_times = [r["t"] for r in rows if r["los"] and not r["dead"]]
CONTACT = min(sight_times) if sight_times else 0.0
draw_frames([min(T_END, CONTACT + d) for d in (0.0, 1.0, 2.0, 3.0, 5.0, 8.0)],
            "Первые секунды после контакта (контакт на {:.1f} с). ".format(CONTACT) + LEGEND,
            os.path.join(OUT, "start.png"))

# ---------- timeline.png ----------
fig, ax = plt.subplots(figsize=(16, 0.42 * len(everyone) + 1.5))
for i, n in enumerate(everyone):
    col = colour(n)
    for r in by[n]:
        if r["dead"]:
            continue
        if is_player(n):
            c = "#bbbbbb" if r["speed"] < 20 else "#77cc77"
        else:
            c = col if r["shooting"] else ("#bbbbbb" if r["speed"] < 20 else "#77cc77")
        ax.add_patch(Rectangle((r["t"], i - 0.38), 0.5, 0.76, fc=c, ec="none"))
    for t, _who, _dmg in taken.get(n, ()):
        ax.plot([t, t], [i - 0.38, i + 0.38], color="black", lw=0.8)
    if is_player(n):
        for t, _target, _pin in shots.get(n, ()):
            ax.plot([t], [i], marker="|", color=col, ms=6)
ax.axvline(CONTACT, color="#444", lw=0.8, ls="--")
for mt, text in marks:
    ax.axvline(mt, color="#c07000", lw=1.2)
    ax.annotate(text[:28], (mt, len(everyone) - 0.45), fontsize=7, color="#c07000", rotation=0, ha="left")
# Подавление игрока: полоска под его строкой, чем темнее, тем сильнее.
for i, n in enumerate(everyone):
    if is_player(n):
        for r in by[n]:
            if r["suppressed"] > 0 and not r["dead"]:
                ax.add_patch(Rectangle((r["t"], i - 0.48), 0.5, 0.1, fc="#7a1fa0", ec="none",
                                       alpha=min(1.0, 0.2 + r["suppressed"] / 100.0)))
ax.set_yticks(range(len(everyone)))
ax.set_yticklabels([short[n] for n in everyone])
ax.set_xlim(0, T_END + 0.5)
ax.set_ylim(-0.6, len(everyone) - 0.4)
ax.set_xlabel("секунды боя. Цвет команды: стреляет. Зелёный: идёт. Серый: стоит и молчит. Пусто: мёртв. "
              "Чёрная риска: в него попали. Пунктир: контакт")
fig.savefig(os.path.join(OUT, "timeline.png"), dpi=100, bbox_inches="tight")
plt.close(fig)

# ---------- report.txt ----------
out = []
out.append("Сценарий: {}".format(scenario))
out.append("Бой {:.1f} с, бойцов {}, игроков {}. Итог: {}".format(T_END, len(names), len(players), outcome or "?"))
out.append("Контакт (кто-то первый раз видит цель): {:.1f} с".format(CONTACT))
for mt, text in marks:
    out.append("{:>6.1f} с: {}".format(mt, text))
for p in players:
    rs = [r for r in by[p] if not r["dead"]]
    if rs:
        sup = [r["suppressed"] for r in rs]
        out.append("Подавление {}: в среднем {:.0f}%, выше половины {:.0f}% времени".format(
            short[p], sum(sup) / len(sup), 100.0 * sum(1 for v in sup if v >= 50) / len(sup)))

out.append("")
out.append("РЕАКЦИЯ НА КОНТАКТ. Секунды от того, как боец сам первый раз увидел цель.")
out.append("{:<5}{:>8}{:>9}{:>9}{:>10}  {}".format("кто", "увидел", "шагнул", "выстрел", "попал", "что говорил в первые 3 с"))
for n in names:
    rs = alive_rows(n)
    saw = next((r["t"] for r in rs if r["los"]), None)
    if saw is None:
        out.append("{:<5}{:>8}".format(short[n], "не видел"))
        continue
    moved = next((r["t"] for r in rs if r["t"] >= saw and r["speed"] > 50), None)
    fired = next((t for t, _o, _p in shots.get(n, ()) if t >= saw - 0.5), None)
    landed = next((t for t, _o, _d in hits.get(n, ()) if t >= saw - 0.5), None)
    said = [text for nt, text in notes.get(n, ()) if saw - 0.5 <= nt <= saw + 3.0]

    def rel(v):
        return "{:+.1f}".format(v - saw) if v is not None else "нет"
    out.append("{:<5}{:>8.1f}{:>9}{:>9}{:>10}  {}".format(short[n], saw, rel(moved), rel(fired), rel(landed),
                                                         " > ".join(s[:26] for s in said)[:80]))

out.append("")
out.append("СТРЕЛЬБА И ПОПАДАНИЯ")
out.append("{:<5}{:>6}{:>8}{:>8}{:>7}{:>7}{:>8}{:>9}{:>8}{:>9}  {}".format(
    "кто", "жил", "выстр", "пристр", "попал", "точн%", "урон", "получил", "выстр/с", "стрел%", "кем убит"))
for n in everyone:
    rs = alive_rows(n)
    if not rs:
        continue
    life = max(0.5, rs[-1]["t"] - rs[0]["t"] + 0.5)
    sh = shots.get(n, ())
    ht = hits.get(n, ())
    k = float(len(rs))
    out.append("{:<5}{:>6.0f}{:>8}{:>8}{:>7}{:>7.0f}{:>8.0f}{:>9.0f}{:>8.2f}{:>9.0f}  {}".format(
        short[n], life, len(sh), sum(1 for s in sh if s[2]), len(ht),
        100.0 * len(ht) / len(sh) if sh else 0.0, sum(h[2] for h in ht), sum(h[2] for h in taken.get(n, ())),
        len(sh) / life, 100.0 * sum(r["shooting"] for r in rs) / k,
        short.get(deaths[n][1], deaths[n][1] or "?") if n in deaths else "-"))

for p in players:
    out.append("")
    out.append("ПО ИГРОКУ {}: кто сколько в него стрелял и попал".format(short[p]))
    for n in names:
        at = [s for s in shots.get(n, ()) if s[1] == p]
        if not at:
            continue
        landed = [h for h in hits.get(n, ()) if h[1] == p]
        out.append("  {:<5} выстрелов {:>4} (пристрелочных {:>4}), попаданий {:>3}, урон {:>5.0f}, самое крупное {:>3.0f}  {}".format(
            short[n], len(at), sum(1 for s in at if s[2]), len(landed), sum(h[2] for h in landed),
            max([h[2] for h in landed] or [0.0]), guns.get(n, "")))
    if p in deaths:
        killer = deaths[p][1]
        out.append("  убил: {} {}".format(short.get(killer, killer), guns.get(killer, "")))

out.append("")
out.append("ДВИЖЕНИЕ")
out.append("{:<5}{:>7}{:>7}{:>7}{:>8}{:>8}  {}".format("кто", "видел%", "стоял%", "молч%", "прошёл", "до цели", "последнее слово"))
idle_spans = []
for n in names:
    rs = alive_rows(n)
    if not rs:
        continue
    k = float(len(rs))
    los = sum(r["los"] for r in rs) / k
    still = sum(1 for r in rs if r["speed"] < 20) / k
    silent = sum(1 for r in rs if r["speed"] < 20 and not r["shooting"]) / k
    path = sum(((a["x"] - b["x"]) ** 2 + (a["y"] - b["y"]) ** 2) ** 0.5 for a, b in zip(rs, rs[1:])) / 100.0
    dists = []
    for r in rs:
        fr = [q for q in by.get(r["focus"], ()) if abs(q["t"] - r["t"]) < 0.3]
        if fr:
            dists.append(((r["x"] - fr[0]["x"]) ** 2 + (r["y"] - fr[0]["y"]) ** 2) ** 0.5 / 100.0)
    out.append("{:<5}{:>7.0f}{:>7.0f}{:>7.0f}{:>7.0f}м{:>7.0f}м  {}".format(
        short[n], los * 100, still * 100, silent * 100, path, sum(dists) / len(dists) if dists else 0.0,
        note_at(n, rs[-1]["t"])[:44]))
    # простои: стоит и молчит дольше 4 секунд подряд
    start = None
    for r in rs + [None]:
        idle = r is not None and r["speed"] < 20 and not r["shooting"]
        if idle and start is None:
            start = r
        elif not idle and start is not None:
            end_t = (r["t"] if r else rs[-1]["t"] + 0.5)
            if end_t - start["t"] >= 4.0:
                idle_spans.append((short[n], start["t"], end_t, start, note_at(n, start["t"] + 1.0)))
            start = None

out.append("")
out.append("ПРОСТОИ (стоит и молчит 4 с и дольше):")
for who, a, b, r, note in sorted(idle_spans, key=lambda s: s[1]):
    out.append("  {:<4} {:>5.1f}-{:<5.1f} ({:>3.0f} с)  видел цель: {}  присед: {}  цель: {:<8} | {}".format(
        who, a, b, b - a, "да" if r["los"] else "нет", "да" if r["crouch"] else "нет",
        short.get(r["focus"], r["focus"] or "нет"), note[:50]))

# Причина нырнуть: DUCK_DAMAGE урона за DUCK_WINDOW секунд (FSTTask_ShooterPeek::DuckDamageThreshold,
# AShooterNPC::RecentDamageWindow). Ответ: событие DUCK в секунду после неё.
DUCK_DAMAGE, DUCK_WINDOW = 35.0, 5.0
out.append("")
out.append("НЫРКИ ОТ УРОНА ({:.0f} урона за {:.0f} с). Причин, из них: умер сразу, нырнул, без ответа".format(DUCK_DAMAGE, DUCK_WINDOW))
tot = [0, 0, 0, 0]
no_answer = defaultdict(int)
for n in names:
    if n not in taken or not is_red(n):
        continue
    row = [0, 0, 0, 0]
    acc = []
    for t, _who, dmg in sorted(taken[n]):
        acc = [(a, b) for a, b in acc if t - a <= DUCK_WINDOW] + [(t, dmg)]
        if sum(b for _a, b in acc) < DUCK_DAMAGE:
            continue
        acc = []
        row[0] += 1
        if n in deaths and deaths[n][0] - t < 0.3:
            row[1] += 1
        elif any(0.0 <= dt - t <= 1.0 for dt, _r in ducks.get(n, ())):
            row[2] += 1
        else:
            row[3] += 1
            no_answer[note_at(n, t)[:40]] += 1
    out.append("  {:<4} {:>3} {:>3} {:>3} {:>3}".format(short[n], *row))
    tot = [a + b for a, b in zip(tot, row)]
out.append("  {:<4} {:>3} {:>3} {:>3} {:>3}".format("все", *tot))
reasons = defaultdict(int)
for n in names:
    for _t, why in ducks.get(n, ()):
        reasons[why] += 1
if reasons:
    out.append("  нырки по виду: " + ", ".join("{} x{}".format(k, v) for k, v in sorted(reasons.items(), key=lambda kv: -kv[1])))
if no_answer:
    out.append("  без ответа, что делал: " + ", ".join("{} x{}".format(k, v) for k, v in sorted(no_answer.items(), key=lambda kv: -kv[1])))

# Каждое попадание по врагу и что он сделал в следующую секунду. Строки пишет сам боец (HURT), "sum" это
# то самое число, которое читает правило нырка. Нет строк HURT: запись старая, раздела нет.
if hurts:
    out.append("")
    out.append("КАЖДОЕ ПОПАДАНИЕ ПО ВРАГУ. sum: урон за окно; ответ в следующую секунду")
    out.append("{:>6} {:<4}{:>5}{:>5}{:>5}  {:<7}{:<34}{}".format("t", "кто", "урон", "sum", "hp", "поза", "что делал", "ответ"))
    table = defaultdict(lambda: [0, 0, 0, 0])      # что делал -> [попаданий с причиной, умер, ответил, без ответа]
    rows = []
    for n in names:
        for t, h in hurts.get(n, ()):
            reason = h.get("sum", 0.0) >= DUCK_DAMAGE
            answer = next((why for dt, why in ducks.get(n, ()) if 0.0 <= dt - t <= 1.0), "")
            dead = h.get("hp", 1.0) <= 0.0
            verdict = "убит" if dead else (answer if answer else ("НЕТ ОТВЕТА" if reason else "мало урона"))
            pose = ("сидит" if h.get("crouch") else "стоит") + ("+пз" if h.get("reload") else "")
            rows.append((t, "{:>6.1f} {:<4}{:>5.0f}{:>5.0f}{:>5.0f}  {:<7}{:<34}{}".format(
                t, short[n], h.get("dmg", 0.0), h.get("sum", 0.0), h.get("hp", 0.0), pose, h["doing"][:33], verdict)))
            if reason:
                key = h["doing"][:33]
                table[key][0] += 1
                table[key][1 if dead else (2 if answer else 3)] += 1
    for _t, line in sorted(rows):
        out.append(line)
    out.append("")
    out.append("ИТОГ ПО ПРИЧИНАМ (sum >= {:.0f}): что делал, причин, убит, ответил, без ответа".format(DUCK_DAMAGE))
    for key, v in sorted(table.items(), key=lambda kv: -kv[1][3]):
        out.append("  {:<34}{:>4}{:>4}{:>4}{:>4}".format(key, *v))
    out.append("  {:<34}{:>4}{:>4}{:>4}{:>4}".format("все", *[sum(v[i] for v in table.values()) for i in range(4)]))

out.append("")
out.append("ЧТО ГОВОРИЛИ (весь бой, сколько раз):")
count = defaultdict(int)
for n in names:
    for nt, text in notes.get(n, ()):
        count[re.sub(r"; \d+ mate.*", "", text)[:48]] += 1
for text, c in sorted(count.items(), key=lambda kv: -kv[1])[:22]:
    out.append("  {:>4}  {}".format(c, text))

out.append("")
out.append("ПОЗА И СКОРОСТЬ (средний модуль скорости, см/с):")
agg = defaultdict(lambda: [0.0, 0, 0, 0])
for n in names:
    for r in alive_rows(n):
        a = agg[(("красные" if is_red(n) else "синие"), r["posture"] or "?")]
        a[0] += r["speed"]
        a[1] += 1
        a[2] += r["shooting"]
        a[3] += r["los"]
for (team, posture), (sp, k, sh, lo) in sorted(agg.items()):
    out.append("  {:<8}{:<10} {:>5} замеров, скорость {:>4.0f}, стреляет {:>3.0f}%, видит цель {:>3.0f}%".format(
        team, posture, k, sp / k, 100.0 * sh / k, 100.0 * lo / k))

step = 10.0 if T_END <= 90.0 else 20.0
out.append("")
out.append("ПО {:.0f} СЕКУНД: живых, поза, перевес, выстрелов (синие | красные)".format(step))
t = 0.0
while t < T_END:
    line = "  {:>3.0f}-{:<3.0f}".format(t, t + step)
    for red in (False, True):
        alive = set()
        post = defaultdict(int)
        ratios = []
        fired = 0
        for n in names:
            if is_red(n) != red:
                continue
            fired += sum(1 for s in shots.get(n, ()) if t <= s[0] < t + step)
            for r in by[n]:
                if t <= r["t"] < t + step and not r["dead"]:
                    alive.add(n)
                    post[r["posture"] or "?"] += 1
                    ratios.append(r["ratio"])
        if ratios:
            top = max(post.items(), key=lambda kv: kv[1])[0]
            line += "   {} живых {:<9} {:>4.1f} {:>4} выстр".format(len(alive), top, sum(ratios) / len(ratios), fired)
        else:
            line += "   {:<32}".format("-")
    out.append(line)
    t += step

io.open(os.path.join(OUT, "report.txt"), "w", encoding="utf-8").write("\n".join(out) + "\n")
print("\n".join(out))
