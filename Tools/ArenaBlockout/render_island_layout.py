# Renders the biome island layout JSON (Island/biome1_island_layout.json) into a
# review SVG (Docs/BiomeIsland_Layout_v<N>.svg) and prints a validation report:
# transition budgets, slope slot spacing, entrance yaws, start->citadel sightline.
#
# Pure local python - NO unreal. Run:  python render_island_layout.py [layout.json]
# The same JSON later feeds build_island.py (terrain + slot placement).

import json
import math
import os
import sys

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
DEFAULT_LAYOUT = os.path.join(TOOLS_DIR, "Island", "biome1_island_layout.json")
OUT_SVG = os.path.normpath(os.path.join(
    TOOLS_DIR, "..", "..", "Polarity", "Docs", "BiomeIsland_Layout_v1.svg"))


def load(path):
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def entrance_yaw(slot_pos, target_pos):
    """World yaw for a sublevel whose LOCAL entrance faces -Y, so that the
    entrance looks back along the route (toward the previous node)."""
    dx, dy = target_pos[0] - slot_pos[0], target_pos[1] - slot_pos[1]
    n = math.hypot(dx, dy)
    return math.degrees(math.atan2(dx / n, -dy / n))


def main():
    layout = load(sys.argv[1] if len(sys.argv) > 1 else DEFAULT_LAYOUT)
    slots = {s["id"]: s for s in layout["slots"]}
    ls = layout["landscape"]
    isl = layout["island"]
    half = (ls["resolution"] - 1) * ls["scale"][0] * 0.5

    # ---------- validation report ----------
    def dist(a, b):
        pa, pb = slots[a]["pos"], slots[b]["pos"]
        return math.hypot(pa[0] - pb[0], pa[1] - pb[1])

    print("== transitions (route, center-to-center / approx water path) ==")
    route = layout["route"]
    budget_lo, budget_hi = layout["rules"]["transition_budget_uu"]
    for a, b in zip(route, route[1:]):
        cc = dist(a, b)
        water = cc - slots[a].get("r", 0) - slots[b].get("r", 0)
        flag = "ok" if budget_lo <= water <= budget_hi + 3000 else "CHECK"
        print("  %-10s -> %-10s  %6.0f cc  ~%5.0f path  [%s]" % (a, b, cc, water, flag))

    print("== slope slot spacing (min %d) ==" % layout["rules"]["min_slope_slot_spacing"])
    slope = ["shoulder", "G1", "G2", "G3", "Villa", "Citadel"]
    for i, a in enumerate(slope):
        for b in slope[i + 1:]:
            cc = dist(a, b)
            flag = "ok" if cc >= layout["rules"]["min_slope_slot_spacing"] else "TOO CLOSE"
            print("  %-9s <-> %-9s  %6.0f  [%s]" % (a, b, cc, flag))

    print("== entrance yaw (arena local -Y faces enter_from node) ==")
    yaws = {}
    for s in layout["slots"]:
        if "enter_from" not in s:
            continue
        y = entrance_yaw(s["pos"], slots[s["enter_from"]]["pos"])
        yaws[s["id"]] = y
        print("  %-9s yaw %7.1f  (entrance faces %s)" % (s["id"], y, s["enter_from"]))

    sx, sy, sz = slots["start_reef"]["pos"]
    cx, cy, cz = slots["Citadel"]["pos"]
    px, py = isl["center"]
    dx, dy = cx - sx, cy - sy
    L = math.hypot(dx, dy)
    t = max(0.0, min(1.0, ((px - sx) * dx + (py - sy) * dy) / (L * L)))
    qd = math.hypot(sx + t * dx - px, sy + t * dy - py)
    print("== sightline start->citadel ==")
    print("  closest approach to island center: %.0f (land radius %d) -> %s" % (
        qd, isl["land_radius"],
        "clears open water" if qd >= isl["land_radius"]
        else "crosses land rim by %.0f - citadel must sit on the cliff lip" % (
            isl["land_radius"] - qd)))

    # ---------- SVG ----------
    W, H = 980, 1430
    MX, MY, SIDE = 170, 150, 620
    S = SIDE / (2 * half)

    def TX(x):
        return MX + (x + half) * S

    def TY(y):
        return MY + (half - y) * S

    def m(v):
        return "%.0f" % (v / 100.0)

    e = []
    e.append('<svg viewBox="0 0 {} {}" xmlns="http://www.w3.org/2000/svg" role="img" '
             'style="font-family: sans-serif; background:#ffffff;">'.format(W, H))
    e.append('<title>Биом 1 «остров-цитадель» — масштабный макет v1</title>')
    e.append('<defs><marker id="arb" viewBox="0 0 10 10" refX="8" refY="5" markerWidth="6.5" '
             'markerHeight="6.5" orient="auto-start-reverse">'
             '<path d="M0,0 L10,5 L0,10 z" fill="#2070c8"/></marker>'
             '<marker id="arg" viewBox="0 0 10 10" refX="8" refY="5" markerWidth="6" '
             'markerHeight="6" orient="auto-start-reverse">'
             '<path d="M0,0 L10,5 L0,10 z" fill="#888888"/></marker></defs>')
    e.append('<text x="60" y="40" font-size="17" font-weight="500" fill="#222222">'
             'Биом 1: остров-цитадель — макет в масштабе (v1, 2026-06-11)</text>')
    e.append('<text x="60" y="62" font-size="12" fill="#666666">'
             'Ландшафт 2017×2017 @100 = 2×2 км; спираль ПРОТИВ часовой; '
             '4 мальдивы (2 S + 2 M) → плечо → 3 стража → вилла → цитадель</text>')
    e.append('<text x="60" y="100" font-size="13" font-weight="500" fill="#666666">Вид сверху'
             ' (север вверх)</text>')
    # map frame = ocean
    e.append('<rect x="{}" y="{}" width="{}" height="{}" fill="#2070c8" fill-opacity="0.10" '
             'stroke="#c9c9c9" stroke-width="1.5"/>'.format(MX, MY, SIDE, SIDE))
    e.append('<text x="{}" y="{}" font-size="11" fill="#666666">океан (вода Z=0, не смертельна)'
             '</text>'.format(MX + 8, MY + SIDE - 10))
    # landmass + cliff arc + peak contours
    icx, icy = TX(isl["center"][0]), TY(isl["center"][1])
    ir = isl["land_radius"] * S
    e.append('<circle cx="{:.0f}" cy="{:.0f}" r="{:.0f}" fill="#c9c9c9" fill-opacity="0.65" '
             'stroke="#666666" stroke-width="1.5"/>'.format(icx, icy, ir))
    pkx, pky = TX(isl["peak"][0]), TY(isl["peak"][1])
    for rr, op in ((14000, 0.5), (9000, 0.65), (4500, 0.8)):
        e.append('<circle cx="{:.0f}" cy="{:.0f}" r="{:.0f}" fill="#b0a89a" '
                 'fill-opacity="{}" stroke="#8a8276" stroke-width="0.8"/>'.format(
                     pkx, pky, rr * S, op))
    # cliff ticks along south-west rim near the peak
    for ang in range(195, 280, 12):
        a = math.radians(ang)
        x1 = icx + ir * math.cos(a)
        y1 = icy - ir * math.sin(a) * -1
        # rim point in svg space (note svg y inversion handled via TX/TY instead)
        wx = isl["center"][0] + isl["land_radius"] * math.cos(a)
        wy = isl["center"][1] + isl["land_radius"] * math.sin(a)
        x1, y1 = TX(wx), TY(wy)
        x2 = TX(wx + 2600 * math.cos(a))
        y2 = TY(wy + 2600 * math.sin(a))
        e.append('<line x1="{:.0f}" y1="{:.0f}" x2="{:.0f}" y2="{:.0f}" stroke="#222222" '
                 'stroke-width="1.6"/>'.format(x2, y2, x1, y1))
    e.append('<text x="{:.0f}" y="{:.0f}" font-size="11" fill="#666666">обрыв</text>'.format(
        TX(isl["center"][0] - isl["land_radius"] - 9000), TY(isl["center"][1] - 22000)))

    # spiral route arrows
    for a, b in zip(route, route[1:]):
        pa, pb = slots[a]["pos"], slots[b]["pos"]
        ra, rb = slots[a].get("r", 0) * S, slots[b].get("r", 0) * S
        x1, y1, x2, y2 = TX(pa[0]), TY(pa[1]), TX(pb[0]), TY(pb[1])
        dxs, dys = x2 - x1, y2 - y1
        n = math.hypot(dxs, dys)
        pad_a, pad_b = (ra + 4) / n, (rb + 7) / n
        color, marker = ("#2070c8", "arb") if a in ("start_reef", "M1", "M2", "M3", "M4") \
            else ("#888888", "arg")
        e.append('<line x1="{:.0f}" y1="{:.0f}" x2="{:.0f}" y2="{:.0f}" stroke="{}" '
                 'stroke-width="2" stroke-dasharray="5 4" marker-end="url(#{})"/>'.format(
                     x1 + dxs * pad_a, y1 + dys * pad_a,
                     x2 - dxs * pad_b, y2 - dys * pad_b, color, marker))
    # sightline start -> citadel
    e.append('<line x1="{:.0f}" y1="{:.0f}" x2="{:.0f}" y2="{:.0f}" stroke="#222222" '
             'stroke-width="1.5" stroke-dasharray="2 6"/>'.format(
                 TX(sx), TY(sy), TX(cx), TY(cy)))

    # slots
    for s in layout["slots"]:
        x, y = TX(s["pos"][0]), TY(s["pos"][1])
        r = s.get("r", 4000) * S
        if s["kind"] == "handmade":
            e.append('<rect x="{:.0f}" y="{:.0f}" width="{:.0f}" height="{:.0f}" '
                     'fill="#e7e2d6" stroke="#222222" stroke-width="2"/>'.format(
                         x - r, y - r, 2 * r, 2 * r))
            e.append('<line x1="{:.0f}" y1="{:.0f}" x2="{:.0f}" y2="{:.0f}" stroke="#222222" '
                     'stroke-width="2"/>'.format(x, y - r, x, y - r - 16))
        elif s["kind"] == "arena":
            e.append('<circle cx="{:.0f}" cy="{:.0f}" r="{:.0f}" fill="#e7e2d6" '
                     'stroke="#666666" stroke-width="1.6"/>'.format(x, y, r))
        elif s["kind"] == "reef":
            e.append('<circle cx="{:.0f}" cy="{:.0f}" r="{:.0f}" fill="#c9c9c9" '
                     'stroke="#666666" stroke-width="2"/>'.format(x, y, r))
        else:  # waypoint
            e.append('<circle cx="{:.0f}" cy="{:.0f}" r="{:.0f}" fill="none" '
                     'stroke="#666666" stroke-width="1.4" stroke-dasharray="3 3"/>'.format(
                         x, y, r))
        yaw_txt = ""
        if s["id"] in yaws:
            yaw_txt = ", yaw {:.0f}".format(yaws[s["id"]])
        label = {"reef": "СТАРТ", "waypoint": "плечо"}.get(s["kind"], s.get("default", s["id"]))
        e.append('<text x="{:.0f}" y="{:.0f}" font-size="11" font-weight="500" fill="#222222" '
                 'text-anchor="middle">{}</text>'.format(x, y - r - 14, label))
        e.append('<text x="{:.0f}" y="{:.0f}" font-size="10" fill="#444444" '
                 'text-anchor="middle">{} | {} м{}</text>'.format(
                     x, y - r - 3, s["id"], m(s["pos"][2]), yaw_txt))

    # legend + scale bar + north
    ly = MY + SIDE + 26
    e.append('<text x="{}" y="{}" font-size="11" fill="#222222">синие стрелки — спираль '
             'мальдив (вэлвы по воде ~120 м пути); серые — подъём по склону (порядок стражей '
             'свободный); пунктир — прямая видимость старт→цитадель</text>'.format(MX, ly))
    bx = MX + SIDE - 50000 * S
    e.append('<line x1="{:.0f}" y1="{}" x2="{:.0f}" y2="{}" stroke="#222222" '
             'stroke-width="2"/>'.format(bx, ly + 22, bx + 50000 * S, ly + 22))
    e.append('<text x="{:.0f}" y="{}" font-size="10" fill="#222222">500 м (50 000 uu)'
             '</text>'.format(bx, ly + 36))

    # ---------- side profile ----------
    py0 = ly + 70
    e.append('<text x="60" y="{}" font-size="13" font-weight="500" fill="#666666">'
             'Профиль маршрута (высоты в масштабе, длины — по порядку маршрута)</text>'.format(py0))
    prof_top, prof_bot = py0 + 20, py0 + 380
    zmax = 20000.0
    px0, pxw = 80, 840
    step = pxw / (len(route) - 1)

    def PZ(z):
        return prof_bot - (z / zmax) * (prof_bot - prof_top)

    e.append('<rect x="{}" y="{:.0f}" width="{}" height="10" fill="#2070c8" '
             'fill-opacity="0.18"/>'.format(px0, PZ(0), pxw))
    e.append('<text x="{}" y="{:.0f}" font-size="10" fill="#666666">вода Z=0</text>'.format(
        px0 + pxw + 6, PZ(0) + 8))
    pts = []
    for i, rid in enumerate(route):
        z = slots[rid]["pos"][2]
        pts.append((px0 + i * step, PZ(z)))
    # citadel peak block
    e.append('<polyline points="{}" fill="none" stroke="#666666" stroke-width="2"/>'.format(
        " ".join("{:.0f},{:.0f}".format(x, y) for x, y in pts)))
    for (x, y), rid in zip(pts, route):
        s = slots[rid]
        z = s["pos"][2]
        label = {"reef": "старт", "waypoint": "плечо"}.get(s["kind"], s.get("default", rid))
        e.append('<circle cx="{:.0f}" cy="{:.0f}" r="4" fill="#e7e2d6" stroke="#222222" '
                 'stroke-width="1.5"/>'.format(x, y))
        e.append('<text x="{:.0f}" y="{:.0f}" font-size="10" fill="#222222" '
                 'text-anchor="middle">{}</text>'.format(x, y - 22, label))
        e.append('<text x="{:.0f}" y="{:.0f}" font-size="10" fill="#444444" '
                 'text-anchor="middle">{} м</text>'.format(x, y - 10, m(z)))
    e.append('<text x="{}" y="{}" font-size="11" fill="#222222">мальдивы низкие (5→28 м), '
             'драма высоты — подъём по острову: плечо 45 → стражи 60/75/90 → вилла 115 → '
             'цитадель 160 (пик 170) + обрыв в воду</text>'.format(px0 - 20, prof_bot + 30))
    e.append('<text x="{}" y="{}" font-size="11" fill="#666666">Бюджеты: вэлвы 5–15 тыс. uu '
             'пути; зазор полей-стаканов ≥ 2–3 тыс.; шаг слотов склона ≥ 10.5 тыс. '
             '(см. отчёт render_island_layout.py)</text>'.format(px0 - 20, prof_bot + 50))
    e.append('</svg>')

    with open(OUT_SVG, "w", encoding="utf-8") as f:
        f.write("\n".join(e))
    print("\nSVG written: {}".format(OUT_SVG))


if __name__ == "__main__":
    main()
