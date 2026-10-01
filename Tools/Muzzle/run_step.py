import unreal
STEPS = ["step_assets", "step_weapons"]
p = "C:/Users/Professional/Documents/Unreal Projects/Polarity_Main5_8/Source/Tools/Muzzle/setup_muzzle_assets" + "." + "py"
g = {"__name__": "muzzle"}
exec(compile(open(p, encoding="utf-8").read(), p, "exec"), g)
for STEP in STEPS:
    g[STEP]()
