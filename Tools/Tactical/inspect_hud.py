import unreal
P = "/Game/Variant_Shooter/UI/Widgets/HUD/Registry/WBP_HudRoot"
h = unreal.WidgetService.get_hierarchy(P)
for w in h:
    print(w)
