#!/usr/bin/env bash
# Calibrates maxGenerations per instance so that one sequential run lasts about
# TARGET seconds. The stop criterion has to be generations (fixed work keeps the
# search deterministic), but the quantity we care about is wall-clock duration, so
# this converts one into the other: N = TARGET / (seconds per generation).
#
# Cost per generation spans ~240x across the pmed set, so a single N cannot put
# every instance in a usable measurement window. Hence one N per instance.
#
# Usage:  ./calibrate_generations.sh [OUTPUT_FILE]
#         TARGET=60 ./calibrate_generations.sh
#         POPS="100 987 2000" INSTANCES="1 11 30" ./calibrate_generations.sh config/generations_populacao.txt
set -euo pipefail

# The solver writes its CSV in the C locale (decimal point), so parse and print in
# the same one. Under pt_BR, printf rejects "3.735" as a malformed number.
export LC_ALL=C

TARGET=${TARGET:-60}          # desired duration of one sequential run, seconds
PROBE_TARGET=${PROBE_TARGET:-5}   # desired duration of the second probe, seconds
MAXTIME=${MAXTIME:-900}       # safety ceiling, must never be the binding limit
OUT=${1:-config/generations.txt}
INSTANCES=${INSTANCES:-$(seq 1 40)}   # pmed numbers to calibrate
POPS=${POPS:-}                # BRKGA population sizes; empty = keep the parameter file as is

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

INSTANCE_DIR=../Instances/aNpMP
RESULTS=../Results/Results_RKO.csv

if [[ ! -x ./runRKO ]]; then
    echo "runRKO not built. Run: make release" >&2
    exit 1
fi
# Without RAPL the solver refuses to run, and every probe would silently reread an
# old result line.
[[ -r /sys/class/powercap/intel-rapl:0/energy_uj ]] ||
    { echo "RAPL not readable. Run: sudo chmod a+r /sys/class/powercap/intel-rapl:0/energy_uj" >&2; exit 1; }

# RKO reads the population size from this fixed path (Method.h), so it is set here
# and the original file is always put back.
PARAMS=config/ParametersOffline.txt
CONF=$(mktemp)
PARAMS_BAK=$(mktemp)
cp "$PARAMS" "$PARAMS_BAK"
trap 'cp "$PARAMS_BAK" "$PARAMS"; rm -f "$CONF" "$PARAMS_BAK"' EXIT
trap 'exit 130' INT TERM

set_population() { sed -i "/^BRKGA$/,/^$/ s/^p *= {[0-9]*}/p    = {$1}/" "$PARAMS"; }

# Runs the solver with a given generation budget and echoes the search duration.
probe() {
    local instance=$1 generations=$2
    cat > "$CONF" <<EOF
BRKGA
MAXRUNS 1
debug 0
quiet 1
seed 1234
control 0
strategy 1
restart 1
sizePool 10
maxGenerations $generations
EOF
    ./runRKO "$instance" "$MAXTIME" "$CONF" >/dev/null 2>&1
    # columns end with: timeBest, timeTotal, decodes, energy
    tail -1 "$RESULTS" | awk -F'\t' '{print $(NF-2)}'
}

mkdir -p "$(dirname "$OUT")" ../Results
: > "$OUT"
if [[ -n $POPS ]]; then
    printf '# instance population maxGenerations   (target %ss per sequential run)\n' "$TARGET" >> "$OUT"
else
    printf '# instance maxGenerations   (target %ss per sequential run)\n' "$TARGET" >> "$OUT"
fi

for pop in ${POPS:--}; do
    [[ $pop == - ]] || set_population "$pop"
    for i in $INSTANCES; do
        instance="$INSTANCE_DIR/pmed${i}.txt"
        [[ -f $instance ]] || { echo "missing: $instance" >&2; exit 1; }

        # First probe is deliberately tiny. On the cheapest instances it lasts only
        # milliseconds, which is too short to extrapolate from, so it is used only to
        # size a second probe that runs long enough to measure properly.
        t1=$(probe "$instance" 10)
        n2=$(awk -v t="$t1" -v target="$PROBE_TARGET" 'BEGIN{
            per = (t > 0 ? t / 10 : 0.001);
            n = int(target / per);
            print (n < 10 ? 10 : n)
        }')

        t2=$(probe "$instance" "$n2")
        n=$(awk -v t="$t2" -v gens="$n2" -v target="$TARGET" 'BEGIN{
            per = t / gens;
            n = int(target / per + 0.5);
            print (n < 1 ? 1 : n)
        }')

        if [[ $pop == - ]]; then
            printf 'pmed%s.txt %s\n' "$i" "$n" >> "$OUT"
        else
            printf 'pmed%s.txt %s %s\n' "$i" "$pop" "$n" >> "$OUT"
        fi
        printf '%-12s pop=%-5s probe=%-6s %6.2fs  ->  maxGenerations=%s\n' \
            "pmed${i}.txt" "$pop" "$n2" "$t2" "$n"
    done
done

echo
echo "written: $OUT"
