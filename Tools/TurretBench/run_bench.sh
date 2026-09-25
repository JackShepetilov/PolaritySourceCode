#!/usr/bin/env bash
# Турельный стенд одной командой (Git Bash, из любой папки):
#
#   Source/Tools/TurretBench/run_bench.sh           прогон 60 с
#   Source/Tools/TurretBench/run_bench.sh 120       прогон 120 с
#   Source/Tools/TurretBench/run_bench.sh build     (пере)собрать L_TurretSiegeBench и навмеш
#   Source/Tools/TurretBench/run_bench.sh 60 "polarity.ai.turretcover.pausetree 0"
#                                                   прогон с доп. настройками (через «;»), для сравнений
#
# Прогон: поднять редактор, если закрыт; открыть уровень (гард несохранённого); включить отладку
# (polarity.ai.bench 1, polarity.ai.IgnorePlayers 1, враги дерутся только с турелью и ядром);
# запустить игру в отдельном окне 1920x1080; снять модалку ошибок блюпринтов; убрать врагов, выдать
# игроку ствол, скормить его турели, поднять игрока на вышку, выпустить врагов (метка T0);
# ждать; снять два кадра окна игры; остановить игру и сбросить переменные; напечатать сводку.
# Кадры и сводка: Saved/TurretBench/. Всё про редактор идёт через Tools/mcp.sh (только из bash).
set -u
ARG="${1:-60}"
EXTRA="${2:-}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJ="$(cd "$HERE/../../.." && pwd)"
MCP="$PROJ/Tools/mcp.sh"
LOG="$PROJ/Saved/Logs/Polarity.log"
OUT="$PROJ/Saved/TurretBench"
STAMP="$(date +%Y%m%d_%H%M%S)"
TMP="${TEMP:-/tmp}/turret_bench"
mkdir -p "$OUT" "$TMP"
HERE_W="$(cygpath -m "$HERE")"
OUT_W="$(cygpath -w "$OUT")"
EDITOR_EXE="/c/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe"

say() { echo "[run_bench] $*"; }

port_up() { netstat -an 2>/dev/null | grep -q "127.0.0.1:8000 .*LISTENING"; }

ensure_editor() {
    if port_up; then return 0; fi
    say "редактор закрыт: запускаю и жду порт MCP (до 10 мин)"
    "$EDITOR_EXE" "$(cygpath -w "$PROJ/Polarity.uproject")" >/dev/null 2>&1 &
    for _ in $(seq 1 120); do
        sleep 5
        if port_up; then
            say "порт поднялся, даю редактору догрузиться"
            sleep 30
            return 0
        fi
    done
    say "порт MCP так и не поднялся"
    exit 1
}

# Один шаг bench_editor внутри редактора. Длинный код execute_python_code режет, поэтому в редактор
# уходит короткая заглушка, а модуль читается с диска.
edcall() {
    cat > "$TMP/call.py" <<EOF
import unreal
p = r"$HERE_W/bench_editor" + "." + "py"
g = {"__name__": "be"}
exec(compile(open(p, encoding="utf-8").read(), p, "exec"), g)
$1
EOF
    MCP_TIMEOUT=120 bash "$MCP" py "$TMP/call.py" 2>&1 | grep -v '^$'
}

step() {
    local title="$1" code="$2" out
    say "$title"
    out="$(edcall "$code")"
    echo "$out" | sed 's/^/    /'
    if echo "$out" | grep -q "SUCCESS=False\|Traceback\|RuntimeError\|NO RESPONSE\|MCP INIT FAILED\|RPC ERROR"; then
        say "стоп: шаг упал"
        return 1
    fi
    LAST_OUT="$out"
    return 0
}

shot() {
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$(cygpath -w "$HERE/winshot.ps1")" \
        -TitleLike "Polarity Preview" -Out "$OUT_W\\shot_${STAMP}_$1.png" | sed 's/^/    /'
}

if [ "$ARG" = "build" ]; then
    ensure_editor
    cat > "$TMP/build.py" <<EOF
import unreal
p = r"$HERE_W/build_turret_bench" + "." + "py"
g = {"__name__": "tb"}
exec(compile(open(p, encoding="utf-8").read(), p, "exec"), g)
g["main"]()
g["step_navdata"]()
EOF
    MCP_TIMEOUT=300 bash "$MCP" py "$TMP/build.py"
    exit $?
fi

RUN="$ARG"
case "$RUN" in ''|*[!0-9]*) say "длительность должна быть числом секунд, а не '$RUN'"; exit 2 ;; esac

ensure_editor
step "1/7 открыть уровень" 'g["open_level"]()' || exit 1

OFFSET=$(wc -c < "$LOG")

# Модалка «Blueprint Asset Compilation Errors» появляется на первом PIE сессии редактора и держит
# редактор, пока её не кликнуть. Ловим её параллельно со стартом.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$(cygpath -w "$HERE/dismiss_pie_modal.ps1")" -Seconds 45 \
    > "$TMP/modal.txt" 2>&1 &
MODAL_PID=$!
if ! step "2/7 запустить игру" "g[\"start\"](r\"\"\"$EXTRA\"\"\")"; then
    kill "$MODAL_PID" 2>/dev/null
    edcall 'g["stop"]()' | sed 's/^/    /'
    exit 1
fi

say "3/7 жду мир игры"
ok=0
for _ in 1 2 3 4 5 6; do
    out="$(edcall 'g["status"]()')"
    echo "$out" | sed 's/^/    /'
    if echo "$out" | grep -q "STATUS game"; then ok=1; break; fi
    sleep 3
done
wait "$MODAL_PID" 2>/dev/null
sed 's/^/    /' "$TMP/modal.txt"
if [ "$ok" != 1 ]; then
    say "мир игры не появился: смотри окно редактора"
    edcall 'g["stop"]()' | sed 's/^/    /'
    exit 1
fi

step "4/7 подготовить: враги убраны, игроку ствол" 'g["prepare"]()' || { edcall 'g["stop"]()'; exit 1; }
sleep 3
step "5/7 вооружить турель, игрока на вышку, выпустить врагов" 'g["arm"]()' || { edcall 'g["stop"]()'; exit 1; }

say "6/7 бой ${RUN} с"
third=$(( RUN / 3 ))
sleep "$third"; shot A
sleep "$third"; shot B
sleep $(( RUN - 2 * third ))
edcall 'g["status"]()' | sed 's/^/    /'

# Крупный план турели: голова, ствол в тисках, губка. Проверка скелетной турели на глаз.
edcall 'g["closeup"]()' | sed 's/^/    /'
sleep 2; shot C

step "7/7 остановить игру" 'g["stop"]()'

say "сводка"
python "$HERE/bench_report.py" "$LOG" "$OFFSET" "$OUT/report_${STAMP}.txt"
say "кадры и сводка: $OUT (shot_${STAMP}_A/B.png, report_${STAMP}.txt)"
