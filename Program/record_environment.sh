#!/usr/bin/env bash
# Records the machine and build state a campaign ran under. Run it immediately
# before the campaign and keep the output next to the results CSV.
#
# This matters more than usual here: the binary is built with -march=native, so it
# is specific to this CPU, and energy readings are sensitive to governor, boost and
# thermal state in a way wall-clock time is not. Without this file the numbers are
# not reproducible, only repeatable.
#
# Usage:  ./record_environment.sh [OUTPUT_FILE]
#         CONFIG_NAME=parallel ./record_environment.sh   (same label as run_campaign.sh)
set -euo pipefail
export LC_ALL=C

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

OUT=${1:-../Results/campanha_env_${CONFIG_NAME:-sequential}.txt}
RAPL_PKG=/sys/class/powercap/intel-rapl:0
mkdir -p "$(dirname "$OUT")"

{
    echo "=== when ==="
    date -Is

    echo
    echo "=== machine ==="
    hostname
    uname -srmo
    (. /etc/os-release 2>/dev/null && echo "$PRETTY_NAME") || true
    lscpu | grep -iE 'model name|^cpu\(s\)|thread|core|cache' || true

    echo
    echo "=== code ==="
    git rev-parse HEAD 2>/dev/null || echo "not a git repository"
    if [[ -n $(git status --porcelain 2>/dev/null) ]]; then
        echo "working tree: DIRTY (results not tied to a clean commit)"
        git status --porcelain
    else
        echo "working tree: clean"
    fi

    echo
    echo "=== build ==="
    ${CXX:-g++} --version | head -1
    grep -E '^(COMMON_FLAGS|PERFORMANCE_FLAGS|DEBUG_FLAGS)' Makefile || true
    if [[ -f runRKO ]]; then
        echo "runRKO built: $(date -Is -r runRKO)"
        echo "runRKO sha256: $(sha256sum runRKO | cut -d' ' -f1)"
    fi

    echo
    echo "=== cpu policy ==="
    echo "governor: $(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || echo n/a)"
    echo "boost: $(cat /sys/devices/system/cpu/cpufreq/boost 2>/dev/null || echo n/a)"
    echo "smt: $(cat /sys/devices/system/cpu/smt/control 2>/dev/null || echo n/a)"
    echo "current MHz:"
    grep -i 'cpu mhz' /proc/cpuinfo | head -4 || true

    echo
    echo "=== openmp environment ==="
    for v in OMP_NUM_THREADS OMP_PROC_BIND OMP_PLACES OMP_WAIT_POLICY OMP_CANCELLATION; do
        echo "$v=${!v-<unset>}"
    done

    echo
    echo "=== thermal ==="
    sensors -u 'k10temp-*' 2>/dev/null | grep -E 'temp[0-9]_input' || echo "k10temp unavailable"

    echo
    echo "=== rapl ==="
    ls -l "$RAPL_PKG/energy_uj" 2>/dev/null || echo "package counter missing"
    echo "max_energy_range_uj: $(cat "$RAPL_PKG/max_energy_range_uj" 2>/dev/null || echo n/a)"
    if [[ -r $RAPL_PKG/energy_uj ]]; then
        echo "access: readable (opened this session with chmod a+r, not persisted)"
        a=$(cat "$RAPL_PKG/energy_uj"); sleep 2; b=$(cat "$RAPL_PKG/energy_uj")
        awk -v a="$a" -v b="$b" 'BEGIN{printf "idle package power: %.2f W (2 s sample)\n", (b-a)/2e6}'
    else
        echo "access: NOT readable - the campaign will abort. Run:"
        echo "  sudo chmod a+r $RAPL_PKG/energy_uj /sys/class/powercap/intel-rapl:0:0/energy_uj"
    fi

    echo
    echo "=== experiment inputs ==="
    echo "parameters: config/ParametersOffline.txt (stock RKO values, not tuned for alpha-NpMP)"
    echo "generations file: config/generations.txt"
    [[ -f config/generations.txt ]] && echo "  sha256: $(sha256sum config/generations.txt | cut -d' ' -f1)"
    [[ -f config/ParametersOffline.txt ]] && echo "  parameters sha256: $(sha256sum config/ParametersOffline.txt | cut -d' ' -f1)"

    echo
    echo "=== load at start ==="
    uptime
    ps -eo pcpu,comm --sort=-pcpu | head -6
} > "$OUT"

cat "$OUT"
echo
echo "written: $OUT"
