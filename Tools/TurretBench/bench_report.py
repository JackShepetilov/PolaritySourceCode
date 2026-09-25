"""Сводка прогона стенда турели по логу редактора. Запускается обычным питоном вне редактора:

    python bench_report.py <Saved/Logs/Polarity.log> <смещение в байтах> [файл_для_копии]

Читает лог начиная со смещения (run_bench.sh запоминает размер лога до старта PIE), дальше
только после последней метки «[BENCH] T0» — до неё враги ещё не выпущены. Считает по классам бойцов:
выстрелы, какая доля лучей упёрлась в цель, урон по турели и ядру (по [BUILD_DEBUG] TakeDamage),
пики по турели, почему не стреляли ([SHOOT_DEBUG]) и во что упирались промахи.
"""

import collections
import io
import re
import sys

TS = re.compile(r"^\[(\d{4}\.\d\d\.\d\d-\d\d\.\d\d\.\d\d):(\d{3})\]")
PAT = {
    "t0": re.compile(r"\[BENCH\] T0 fight starts"),
    "shot": re.compile(r"\[BENCH\] SHOT npc=(\S+) weapon=(\S+) target=(\S+) ray=(\S+) onTarget=(\d) dist=(-?[\d.]+) crouched=(\d) spread=(-?[\d.]+) supp=(\d) suppfire=(\d) cover=(.*)$"),
    "died": re.compile(r"\[BENCH\] DIED npc=(\S+) cover=(.+?) target=(\S+)"),
    "dmg": re.compile(r"\[BUILD_DEBUG\] (\S+) TakeDamage ([\d.]+) from (\S+) \(causer (\S+)\) \| sourceTeam=(\d+) myTeam=(\d+) sameSide=(\d)"),
    "peek": re.compile(r"\[TURRET_DEBUG\] (\S+) AtHide -> Peeking"),
    "arrived": re.compile(r"\[TURRET_DEBUG\] (\S+) at P, window"),
    "never": re.compile(r"\[TURRET_DEBUG\] (\S+) NEVER REACHED P"),
    "wentbad": re.compile(r"\[TURRET_DEBUG\] (\S+) cover at H went bad"),
    "cover": re.compile(r"\[TURRET_DEBUG\] (\S+) takes cover from"),
    "ignores": re.compile(r"\[TURRET_DEBUG\] (\S+) ignores (\S+), (\S+) is the closer threat"),
    "winend": re.compile(r"\[TURRET_DEBUG\] (\S+) peek window ends on \S+: everGotPermission=(\d) stillReloading=(\d)"),
    "gate": re.compile(r"\[SHOOT_DEBUG\] (\S+): (.+?) \(target (\S+)\)"),
    "aim": re.compile(r"\[AIM_DEBUG\] (\S+) aiming at (\S+) but the ray stops on (\S+)"),
    "steer": re.compile(r"\[BENCH\] STEER npc=(\S+) owner=(MINE|OTHER) cover=(.+?) goalOff=(-?[\d.]+) treePaused=(\d)"),
    "proj": re.compile(r"\[PROJ_DEBUG\] \S+ HIT-WORLD \| other=(\S+) cosmetic=(\d) .* weapon=(\S+)"),
    "tgate": re.compile(r"\[TURRET_DEBUG\] BP_Buildable_Turret\S* vice (\d+): (.+)$"),
    "head": re.compile(r"\[TURRET_DEBUG\] \S+(: .*(articulated|pitch axis|BACK of the head).*| articulated head: .*)$"),
    "jaw": re.compile(r"\[TURRET_DEBUG\] \S+ jaw on "),
}

KIND = {"BP_ShooterNPC": "стрелки", "BP_GrenadierNPC": "гранатомётчики"}


def kind_of(name):
    base = re.sub(r"_C_\d+$", "", name)
    return KIND.get(base, base)


def is_npc(name):
    return bool(re.search(r"NPC_C_\d+$", name))


def main():
    path, offset = sys.argv[1], int(sys.argv[2])
    copy_to = sys.argv[3] if len(sys.argv) > 3 else None
    with open(path, "rb") as fh:
        fh.seek(offset)
        text = fh.read().decode("utf-8", errors="replace")
    lines = text.splitlines()
    all_lines = lines
    t0 = max((i for i, l in enumerate(lines) if PAT["t0"].search(l)), default=None)
    if t0 is None:
        print("НЕТ МЕТКИ T0 в этом прогоне: турель не вооружили или враги не выпущены. Смотри [BENCH_RUN] выше.")
        sys.exit(1)
    lines = lines[t0:]

    def stamp(l):
        m = TS.match(l)
        return (m.group(1) + ":" + m.group(2)) if m else None
    first = next((s for s in map(stamp, lines) if s), "?")
    last = next((s for s in map(stamp, reversed(lines)) if s), "?")

    seen = collections.defaultdict(set)
    c = collections.defaultdict(collections.Counter)      # kind -> counter
    gates = collections.defaultdict(collections.Counter)  # kind -> reason -> times entered
    misses = collections.defaultdict(collections.Counter) # kind -> what the ray stopped on
    dmg_to = collections.Counter()                         # "turret"/"core" -> damage
    dmg_hits = collections.Counter()
    dmg_by = collections.defaultdict(collections.Counter)  # kind -> "turret"/"core" -> damage
    refused = collections.Counter()
    proj_hits = collections.defaultdict(collections.Counter)  # weapon class -> what the round struck

    for l in lines:
        m = PAT["proj"].search(l)
        if m:
            other, cosmetic, weapon = m.groups()
            if not int(cosmetic):
                proj_hits[re.sub(r"_C_\d+$", "", weapon)][other] += 1
            continue
        m = PAT["steer"].search(l)
        if m:
            npc, owner, cover, off, paused = m.groups()
            if is_npc(npc):
                k = kind_of(npc)
                c[k]["steer_" + owner.lower()] += 1
                c[k]["steer_paused"] += int(paused)
            continue
        m = PAT["shot"].search(l)
        if m:
            npc, weapon, target, ray, on, dist, crouched, spread, supp, suppfire, cover = m.groups()
            k = kind_of(npc)
            seen[k].add(npc)
            c[k]["shots"] += 1
            c[k]["on"] += int(on)
            c[k]["crouched_shots"] += int(crouched)
            c[k]["supp_shots"] += int(supp)
            c[k]["suppfire_shots"] += int(suppfire)
            c[k]["spread_sum"] += max(0.0, float(spread))
            if "Turret" in target:
                c[k]["shots_at_turret"] += 1
            elif "Core" in target:
                c[k]["shots_at_core"] += 1
            if not int(on):
                misses[k][ray] += 1
            continue
        m = PAT["dmg"].search(l)
        if m:
            victim, amount, source, causer, st, mt, same = m.groups()
            what = "turret" if "Turret" in victim else ("core" if "Core" in victim else victim)
            if int(same):
                refused[what] += 1
                continue
            dmg_to[what] += float(amount)
            dmg_hits[what] += 1
            if is_npc(source):
                dmg_by[kind_of(source)][what] += float(amount)
            continue
        for key in ("died", "peek", "arrived", "never", "wentbad", "cover", "ignores"):
            m = PAT[key].search(l)
            if m:
                npc = m.group(1)
                if is_npc(npc):
                    k = kind_of(npc)
                    seen[k].add(npc)
                    c[k][key] += 1
                break
        else:
            m = PAT["winend"].search(l)
            if m and is_npc(m.group(1)):
                k = kind_of(m.group(1))
                c[k]["windows"] += 1
                c[k]["win_perm"] += int(m.group(2))
                c[k]["win_reload"] += int(m.group(3))
                continue
            m = PAT["gate"].search(l)
            if m and is_npc(m.group(1)):
                gates[kind_of(m.group(1))][m.group(2)] += 1
                continue

    kinds = [k for k in ("стрелки", "гранатомётчики") if k in seen] + sorted(k for k in seen if k not in KIND.values())
    out = []
    p = out.append
    p("=== ТУРЕЛЬНЫЙ СТЕНД: сводка ===")
    p("время после T0: {} -> {}".format(first, last))
    p("")
    row = lambda title, f: p("{:<34}".format(title) + "".join("{:>16}".format(f(k)) for k in kinds))
    p("{:<34}".format("") + "".join("{:>16}".format(k) for k in kinds))
    row("бойцов за прогон", lambda k: len(seen[k]))
    row("погибло", lambda k: c[k]["died"])
    row("выстрелов всего", lambda k: c[k]["shots"])
    row("  из них по турели", lambda k: c[k]["shots_at_turret"])
    row("  из них по ядру", lambda k: c[k]["shots_at_core"])
    row("  луч дошёл до цели", lambda k: "{:.0f}%".format(100.0 * c[k]["on"] / c[k]["shots"]) if c[k]["shots"] else "-")
    row("  стреляли сидя", lambda k: c[k]["crouched_shots"])
    row("  нарочно мимо: подавлен", lambda k: c[k]["supp_shots"])
    row("  нарочно мимо: огонь на подавление", lambda k: c[k]["suppfire_shots"])
    row("  средний разброс, град", lambda k: "{:.1f}".format(c[k]["spread_sum"] / c[k]["shots"]) if c[k]["shots"] else "-")
    row("урон по турели", lambda k: "{:.0f}".format(dmg_by[k]["turret"]))
    row("урон по ядру", lambda k: "{:.0f}".format(dmg_by[k]["core"]))
    row("ушли в укрытие от турели", lambda k: c[k]["cover"])
    row("выходов на пик", lambda k: c[k]["peek"])
    row("  дошли до угла", lambda k: c[k]["arrived"])
    row("  не дошли до угла", lambda k: c[k]["never"])
    row("  окон с разрешением стрелять", lambda k: "{}/{}".format(c[k]["win_perm"], c[k]["windows"]))
    row("  окон, съеденных перезарядкой", lambda k: "{}/{}".format(c[k]["win_reload"], c[k]["windows"]))
    row("укрытие испортилось", lambda k: c[k]["wentbad"])
    row("ноги ведёт пик/марш (замеров)", lambda k: c[k]["steer_mine"])
    row("ноги ведёт кто-то ещё (замеров)", lambda k: c[k]["steer_other"])
    row("  из них дерево спало", lambda k: c[k]["steer_paused"])
    row("турель проигнорирована (игрок ближе)", lambda k: c[k]["ignores"])
    p("")
    p("Турель получила {:.0f} урона ({} попаданий), ядро {:.0f} ({} попаданий).".format(
        dmg_to["turret"], dmg_hits["turret"], dmg_to["core"], dmg_hits["core"]))
    if refused:
        p("Отбито как свой огонь: {}".format(dict(refused)))
    for k in kinds:
        if misses[k]:
            p("")
            p("{}: промахи луча упирались в {}".format(k, ", ".join("{} x{}".format(a, n) for a, n in misses[k].most_common(6))))
        if gates[k]:
            p("{}: почему не стреляли (сколько раз вошли в состояние) {}".format(
                k, ", ".join("{} x{}".format(r, n) for r, n in gates[k].most_common(8))))
    for weapon, hits in sorted(proj_hits.items()):
        total = sum(hits.values())
        on_turret = sum(n for o, n in hits.items() if "Turret" in o)
        p("")
        p("снаряды {}: {} попаданий, из них в турель {}; куда: {}".format(
            weapon, total, on_turret, ", ".join("{} x{}".format(o, n) for o, n in hits.most_common(6))))
    # The turret's own side: the skeleton setup and the jaw (logged at spawn and feed, before T0),
    # then its shots and why its vices held fire (polarity.turret.debug 1).
    p("")
    p("--- турель ---")
    for l in all_lines:
        if PAT["head"].search(l) or PAT["jaw"].search(l):
            p(l.split("[TURRET_DEBUG] ", 1)[-1].strip())
    fired = collections.Counter()
    holds = collections.Counter()
    for l in lines:
        m = PAT["tgate"].search(l)
        if not m:
            continue
        vice, what = m.group(1), m.group(2)
        if what.startswith("FIRED"):
            fired[vice] += 1
        else:
            holds[what.split(" (")[0].strip()] += 1
    p("выстрелов турели: {}".format(", ".join("тиски {} x{}".format(v, n) for v, n in sorted(fired.items())) or "НИ ОДНОГО"))
    if holds:
        p("почему тиски молчали (входов в состояние): {}".format(
            ", ".join("{} x{}".format(r, n) for r, n in holds.most_common(8))))
    report = "\n".join(out)
    print(report)
    if copy_to:
        with io.open(copy_to, "w", encoding="utf-8") as fh:
            fh.write(report + "\n")


if __name__ == "__main__":
    main()
