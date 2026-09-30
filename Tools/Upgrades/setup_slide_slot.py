import unreal

# Slide-slot upgrades (2026-09-30): DA_SwordSlide rewritten as the Apex Axle "Sliding Shooter"
# (shots while sliding come from the reserve), and the new SlideFireFriction (less slide friction
# while firing without a break). Run after the C++ is built, through Tools/mcp.sh py <this file>.
# No file name with the python extension in this header: execute_python_code then takes the whole
# script for a file path (Docs/Gotchas/Python_Editor.md).
# Idempotent: creates what is missing, never recreates an asset. DA_SwordSlide's tuning is rewritten
# on purpose (the old fields are gone), its name, tag and icon are left alone.

UP = "/Game/Variant_Shooter/Blueprints/Upgrades/"
SWORD_SLIDE = UP + "Slide/DA_Slidemaxxing"
FOLDER = UP + "Slide"
FRICTION_NAME = "DA_Upgrade_SlideFireFriction"
POOL = UP + "DA_DispenserUpgradePool"
REGISTRY = UP + "DA_UpgradeRegistry"
JUMP_DA = UP + "Jump/DA_Upgrade_ExtraJump"


def tag(name):
    t = unreal.GameplayTag()
    t.import_text('(TagName="%s")' % name)
    return t


def q(v):
    return '"' + str(v).replace('"', "'") + '"'


def display(desc, rarity, stats):
    d = unreal.UpgradeLevelDisplay()
    rows = ",".join("(Label=%s,Value=%s)" % (q(l), q(v)) for l, v in stats)
    d.import_text("(Description=%s,Stats=(%s),Rarity=%s)" % (q(desc), rows, rarity))
    return d


def struct(cls, text_fields):
    s = cls()
    s.import_text("(" + text_fields + ")")
    return s


def main():
    # Upgrade.SlideFireFriction is in Config/DefaultGameplayTags.ini already.
    print("TAG:", tag("Upgrade.SlideFireFriction").export_text())

    # ---- Sliding Shooter (DA_SwordSlide): Lv1 rare 50% of the mag (Apex), Lv2 legendary 100% ----
    ss = unreal.EditorAssetLibrary.load_asset(SWORD_SLIDE)
    if not ss:
        print("MISSING:", SWORD_SLIDE)
    else:
        print("BEFORE DA_SwordSlide:", ss.get_editor_property("display_name"), ss.get_editor_property("upgrade_tag"),
              ss.get_editor_property("category"), ss.get_editor_property("icon"))
        ss.set_editor_property("description", "Shots fired while sliding come from the reserve, not the magazine.")
        ss.set_editor_property("component_class", unreal.Upgrade_SwordSlide)
        ss.set_editor_property("level_data", [
            struct(unreal.SwordSlideLevelData, "MagazineFraction=0.5,MinSlideSpeed=392"),
            struct(unreal.SwordSlideLevelData, "MagazineFraction=1.0,MinSlideSpeed=392")])
        ss.set_editor_property("level_displays", [
            display("Shots while sliding come from the reserve, up to half a magazine per slide.", "Rare",
                    [("Per slide", "50% mag")]),
            display("Shots while sliding come from the reserve, up to a full magazine per slide.", "Legendary",
                    [("Per slide", "100% mag")])])
        ss.set_editor_property("max_level", 2)
        unreal.EditorAssetLibrary.save_loaded_asset(ss)
        print("MODIFIED: DA_SwordSlide")

    # ---- SlideFireFriction: Lv1 common -40% friction, Lv2 epic -65% ----
    path = FOLDER + "/" + FRICTION_NAME
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        ff = unreal.EditorAssetLibrary.load_asset(path)
    else:
        fac = unreal.DataAssetFactory()
        fac.set_editor_property("data_asset_class", unreal.UpgradeDefinition_SlideFireFriction)
        ff = unreal.AssetToolsHelpers.get_asset_tools().create_asset(FRICTION_NAME, FOLDER, None, fac)
        print("CREATED:", path)
    if ff and len(ff.get_editor_property("level_data")) == 0:
        ff.set_editor_property("upgrade_tag", tag("Upgrade.SlideFireFriction"))
        ff.set_editor_property("category", unreal.SkillCategory.BORING)
        ff.set_editor_property("display_name", "SlideFireFriction")
        ff.set_editor_property("description", "Keep firing while sliding and the slide loses less speed.")
        ff.set_editor_property("component_class", unreal.Upgrade_SlideFireFriction)
        ff.set_editor_property("level_data", [
            struct(unreal.SlideFireFrictionLevelData, "FrictionScale=0.6,RefireSlack=0.25"),
            struct(unreal.SlideFireFrictionLevelData, "FrictionScale=0.35,RefireSlack=0.25")])
        ff.set_editor_property("level_displays", [
            display("Keep firing while sliding: less slide friction.", "Common", [("Slide friction", "-40%")]),
            display("Keep firing while sliding: much less slide friction.", "Epic", [("Slide friction", "-65%")])])
        ff.set_editor_property("max_level", 2)
        unreal.EditorAssetLibrary.save_loaded_asset(ff)
        print("MODIFIED:", path)

    # ---- pool: the Slide slot holds both ----
    pool = unreal.EditorAssetLibrary.load_asset(POOL)
    slots = list(pool.get_editor_property("slots"))
    print("SLOTS:", [str(s.get_editor_property("display_name")) for s in slots])
    changed = False
    found = False
    for i, s in enumerate(slots):
        if str(s.get_editor_property("display_name")) != "Slide":
            continue
        found = True
        entries = list(s.get_editor_property("upgrades"))
        have = [e.get_editor_property("upgrade") for e in entries]
        for a in (ss, ff):
            if a and a not in have:
                e = unreal.DispenserUpgradeEntry()
                e.set_editor_property("upgrade", a)
                e.set_editor_property("weight", 1.0)
                entries.append(e)
                changed = True
                print("ADDED to Slide slot:", a.get_name())
        s.set_editor_property("upgrades", entries)
        slots[i] = s
        print("Slide slot now:", [e.get_editor_property("upgrade").get_name() for e in entries if e.get_editor_property("upgrade")])
    if not found:
        print("NO Slide slot in the pool: not added, ask the author")
    if changed:
        pool.set_editor_property("slots", slots)
        unreal.EditorAssetLibrary.save_loaded_asset(pool)
        print("MODIFIED:", POOL)

    # ---- registry: only if the jump-slot upgrades are listed there too ----
    reg = unreal.EditorAssetLibrary.load_asset(REGISTRY)
    if reg:
        allu = list(reg.get_editor_property("all_upgrades"))
        jump = unreal.EditorAssetLibrary.load_asset(JUMP_DA)
        if jump in allu:
            added = False
            for a in (ss, ff):
                if a and a not in allu:
                    allu.append(a)
                    added = True
                    print("ADDED to registry:", a.get_name())
            if added:
                reg.set_editor_property("all_upgrades", allu)
                unreal.EditorAssetLibrary.save_loaded_asset(reg)
                print("MODIFIED:", REGISTRY)
        else:
            print("REGISTRY: jump upgrades not listed there, left alone")


main()
