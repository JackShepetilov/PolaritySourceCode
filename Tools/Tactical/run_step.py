import unreal
p = "C:/Users/Professional/Documents/Unreal Projects/Polarity_Main5_8/Source/Tools/Tactical/setup_tactical_assets" + "." + "py"
g = {"__name__": "tactical"}
exec(compile(open(p, encoding="utf-8").read(), p, "exec"), g)
STEP = "step_hud_root_add"
g[STEP]()
