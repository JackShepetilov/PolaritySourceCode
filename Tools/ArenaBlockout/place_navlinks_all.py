# One-shot rollout: run place_navlinks.py for EVERY biome-1 arena in sequence.
# Run ONLY inside Lvl_ArenaTestRun (streams all 9 arenas), PIE off, no dirty foreign maps.
# Reuses the validated per-arena script verbatim; an arena that isn't loaded is skipped
# safely (place_navlinks raises before writing anything). Log tag: [NAVALL] + [NAVPLACE]
import unreal
import sys
import os

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "place_navlinks.py")
src = open(SRC, encoding="utf-8").read()

ARENAS = ["A1_Pier", "A2_Courtyard", "A3_Dome", "A4_Hangar", "A5_Amphitheater",
          "A6_Villa", "A7_Gallery", "A8_Lighthouse", "A9_Temple"]


def log(m):
    unreal.log("[NAVALL] {}".format(m))


ok, skipped = [], []
for a in ARENAS:
    sys.argv = ["place_navlinks.py", a]
    try:
        exec(compile(src, SRC, "exec"), {"__name__": "__main__"})
        ok.append(a)
    except Exception as e:
        skipped.append(a)
        log("{} SKIPPED/FAILED: {}".format(a, e))

log("ROLLOUT DONE. placed={} skipped={}".format(ok, skipped))
