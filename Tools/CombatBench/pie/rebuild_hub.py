import unreal
print("CLASSES", unreal.find_object(None, "/Script/Polarity.BenchUpgradeButton") is not None, unreal.find_object(None, "/Script/Polarity.PlayerSuppressionComponent") is not None)
p = r"C:/Users/Professional/Documents/Unreal Projects/Polarity_Main5_8/Source/Tools/CombatBench/build_bench_hub.py"
g = {"__name__": "x"}
exec(compile(open(p, encoding="utf-8").read(), p, "exec"), g)
g["step_hub"]()
