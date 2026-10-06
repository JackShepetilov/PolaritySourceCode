"""Register the enemy bar and damage-sum palette tags and seed their colours.

Colours only go in where the palette has none yet, so an author's edits in
Project Settings -> Polarity -> Palette are never overwritten. The C++ fallbacks
(UEnemyBarWidget::ResolveColors, UDamageSumLayerWidget::NativeConstruct) are the
same values, so the game looks the same before and after this runs.

Run in the editor through a launcher (the text of a file name with its extension breaks execute_python_code).
"""
import unreal

COMMENT = "Enemy bar and damage sum, see Docs/HUD/Enemy_Bar_DamageNumbers_Mockup_2026-10-04.html"

# Tag -> sRGB colour (r, g, b, a) as authored in the mockup.
SEEDS = {
    "Palette.HUD.Enemy.Plate": (0x17, 0x0F, 0x0B, 0xA8),
    "Palette.HUD.Enemy.Track": (0xFF, 0xFF, 0xFF, 0x14),
    "Palette.HUD.Enemy.Health": (0xC8, 0x47, 0x3F, 0xFF),
    "Palette.HUD.Enemy.Ghost": (0xFF, 0xE9, 0xC2, 0xFF),
    "Palette.HUD.Enemy.Armor.T0": (0xF4, 0xF6, 0xFF, 0xFF),
    "Palette.HUD.Enemy.Armor.T1": (0x3F, 0xA9, 0xF5, 0xFF),
    "Palette.HUD.Enemy.Armor.T2": (0xB4, 0x5C, 0xF0, 0xFF),
    "Palette.HUD.Enemy.Armor.T3": (0xF0, 0x40, 0x4F, 0xFF),
    "Palette.HUD.Damage.Flesh": (0xFF, 0x2B, 0x3A, 0xFF),
    "Palette.HUD.Damage.Head": (0xFF, 0xB6, 0x1F, 0xFF),
    "Palette.HUD.Damage.Remote": (0xB8, 0xB0, 0xA0, 0xFF),
    "Palette.HUD.Damage.Outline": (0x1A, 0x0F, 0x08, 0xFF),
    "Palette.HUD.Damage.WhiteArmorOutline": (0x2B, 0x2B, 0x30, 0xFF),
}


def make_tag(name):
    tag = unreal.GameplayTag()
    tag.import_text('(TagName="%s")' % name)
    return tag


# Entries this run overwrites even when set: the damage-number colours were re-picked 2026-10-04
# (author asked for brighter ones) and written that day. Keep it empty so later runs keep author edits.
FORCE = set()


def main():
    unreal.GameplayTagService.add_tags(list(SEEDS.keys()), COMMENT)
    print("ADDED: tags", len(SEEDS))

    for name, (r, g, b, a) in SEEDS.items():
        tag = make_tag(name)
        if unreal.PolarityPalette.has_color(tag) and name not in FORCE:
            print("KEPT:", name)
            continue
        # sRGB to linear, the same conversion FLinearColor(FColor) does in C++.
        color = unreal.MathLibrary.conv_color_to_linear_color(unreal.Color(r=r, g=g, b=b, a=a))
        unreal.PolarityPalette.set_color(tag, color)
        print("MODIFIED:", name, color)


main()
