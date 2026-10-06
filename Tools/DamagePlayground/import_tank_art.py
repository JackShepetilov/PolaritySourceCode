"""Reuse the verified material/import pipeline with separate new tank textures and materials."""
import unreal

path = r"C:/Users/Professional/Documents/Unreal Projects/Polarity_Main5_8/Source/Tools/DamagePlayground/import_impact_art.py"
pipeline = {"__name__": "tank_art_pipeline"}
exec(compile(open(path, encoding="utf-8").read(), path, "exec"), pipeline)
pipeline["ROWS"] = (
    ("T_Tank_Tracks_4x4", "M_Tank_Tracks", "masked", True),
    ("T_Tank_Glass_4x4", "M_Tank_Glass", "masked", True),
    ("T_Tank_Smoke_2x2", "M_Tank_Smoke", "translucent", True),
    ("T_Tank_Fire_2x2", "M_Tank_Fire", "additive", True),
    ("T_Tank_Sparks_4x4", "M_Tank_Sparks", "additive", True),
    ("T_Tank_Ricochet_2x2", "M_Tank_Ricochet", "additive", True))


def main():
    pipeline["import_textures"]()
    pipeline["build_materials"]()
