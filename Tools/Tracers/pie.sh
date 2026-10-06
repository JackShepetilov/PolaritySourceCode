#!/usr/bin/env bash
# Source/Tools/Tracers/pie.sh "setup()"  -> runs one action of pie_tracers in the editor
R="C:/Users/Professional/Documents/Unreal Projects/Polarity_Main5_8"
F="${TEMP:-/tmp}/pie_act.txt"
ACT=$(printf '%s' "$1" | sed 's/"/\\"/g')
cat > "$F" <<PY
import unreal
p = "$R/Source/Tools/Tracers/pie_tracers" + "." + "py"
g = {"__name__": "__main__", "__file__": p, "ACT": "$ACT"}
exec(compile(open(p, encoding="utf-8").read(), p, "exec"), g)
PY
"$R/Tools/mcp.sh" py "$F"
