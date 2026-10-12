#!/usr/bin/env bash
# Run one function of a Source/Tools/ReloadSlots script inside the editor.
#   Source/Tools/ReloadSlots/run.sh <module without .py> <function>
set -u
D="${TEMP:-/tmp}/reload_slots"; mkdir -p "$D"
ROOT="$(cd "$(dirname "$0")/../../.." && pwd -W 2>/dev/null || pwd)"
cat > "$D/launch.py" <<PY
import unreal
p = r"$ROOT/Source/Tools/ReloadSlots/$1" + "." + "py"
g = {"__name__": "rs"}
exec(compile(open(p, encoding="utf-8").read(), p, "exec"), g)
g["$2"]()
print("STEP_DONE $1.$2")
PY
"$ROOT/Tools/mcp.sh" py "$D/launch.py"
