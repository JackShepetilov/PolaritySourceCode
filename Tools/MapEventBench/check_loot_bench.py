import unreal  # first line on purpose: execute_python_code only accepts code that starts with it

# What actually landed on the loot bench, as a table. Run it WHILE PIE is going.
#
#   Tools/mcp.sh py Source/Tools/MapEventBench/check_loot_bench.py
#
# One run is one sample. The generator is random by design, so the useful thing is to run it, stop
# PIE, start PIE, and run it again: what should hold steady is the ORDER of the zones, not the
# numbers inside any one of them.
#
# Log filter tag: [LOOTCHECK]

BANDS = ("Common", "Rare", "Epic", "Legendary")


def band_of(value):
    # Same four thresholds as PolarityLoot::BandForValue. Kept in step by hand: if that function
    # moves, this moves.
    if value >= 0.85:
        return 3
    if value >= 0.60:
        return 2
    if value >= 0.30:
        return 1
    return 0


def main():
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
    if not world:
        raise RuntimeError("No game world - start PIE first")

    pois = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.PoiActor)
    sheets = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.LootSheet)
    anchors = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.LootAnchor)

    print("  zones {}, anchors {}, sheets {}".format(len(pois), len(anchors), len(sheets)))
    print("")
    print("  {:<10} {:>7} {:>7} {:>7} {:>7}  {}".format(
        "zone", "quality", "sheets", "items", "best", "what landed"))

    grand_items = 0

    # A sheet knows nothing about which point it belongs to, so the zones are matched by distance:
    # this bench puts them far enough apart that nearest-centre is unambiguous.
    for poi in sorted(pois, key=lambda p: p.get_actor_location().x):
        tag = str(poi.get_editor_property("poi_tag"))
        quality = poi.get_editor_property("loot_quality")
        centre = poi.get_actor_location()
        reach = poi.get_editor_property("influence_radius")

        mine = [s for s in sheets
                if (s.get_actor_location() - centre).size() <= reach]

        items = 0
        best = 0.0
        counts = {}
        for s in mine:
            laid = s.get_laid_out()
            items += len(laid)
            best = max(best, s.get_best_value())
            for a in laid:
                n = a.get_class().get_name().replace("_C", "")
                counts[n] = counts.get(n, 0) + 1

        grand_items += items
        what = ", ".join("{} x{}".format(k, v) for k, v in sorted(counts.items()))
        print("  {:<10} {:>7.2f} {:>7} {:>7} {:>7.2f}  {}".format(
            tag, quality, len(mine), items, best, what if what else "-"))

    print("")
    print("  total items on the bench: {}".format(grand_items))
    print("  band of the best thing per zone is the column 'best': {}".format(
        ", ".join("{} >= {}".format(BANDS[i], t) for i, t in ((3, 0.85), (2, 0.60), (1, 0.30)))))


main()
