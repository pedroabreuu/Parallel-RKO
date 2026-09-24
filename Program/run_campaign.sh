#!/usr/bin/env bash
# Runs one measurement campaign: every instance in config/generations.txt, REPS
# times each, at the per-instance generation budget produced by the calibration.
#
# The seed is fixed on purpose. Every repetition then decodes the same candidates
# in the same order, so what varies between them is machine noise, not the search.
# That is what makes the sequential and parallel numbers comparable later, and it
# is also the dispersion reported for each measurement.
#
# Results_RKO.csv does not record which run produced which row, so this script
# writes its own CSV, pairing what it knows (instance, seed, budget, config label)
# with what the solver reported on its last line.
#
# Usage:  ./run_campaign.sh [OUTPUT_CSV]
#         CONFIG_NAME=sequential REPS=10 ./run_campaign.sh
set -euo pipefail
export LC_ALL=C

CONFIG_NAME=${CONFIG_NAME:-sequential}   # label for the configuration under test
REPS=${REPS:-10}
SEED=${SEED:-1234}
COOLDOWN=${COOLDOWN:-5}                  # seconds between runs, to settle thermals
MAXTIME=${MAXTIME:-600}                  # safety ceiling, must never bind

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

GENS=config/generations.txt
INSTANCE_DIR=../Instances/aNpMP
RESULTS=../Results/Results_RKO.csv
OUT=${1:-../Results/campaign_${CONFIG_NAME}.csv}

[[ -x ./runRKO ]] || { echo "runRKO not built. Run: make release" >&2; exit 1; }
[[ -f $GENS   ]] || { echo "missing $GENS. Run: ./calibrate_generations.sh" >&2; exit 1; }

CONF=$(mktemp)
trap 'rm -f "$CONF"' EXIT

# Temperature is context, not data: never let a missing sensor abort a campaign
# that is already hours in.
cpu_temp() { sensors -u k10temp 2>/dev/null | awk -F': ' '/temp1_input/{print $2; exit}' || true; }

mkdir -p "$(dirname "$OUT")" ../Results
if [[ ! -s $OUT ]]; then
    echo "config,instance,vertices,p,seed,max_generations,repetition,ofv,time_best,time_total,decodes,joules,temp_start,temp_end" > "$OUT"
fi

total=$(grep -vc '^#' "$GENS" || true)
done_count=0

for rep in $(seq 1 "$REPS"); do
    # Each repetition sweeps the instances in a fresh random order, so thermal
    # drift over the hours spreads across instances instead of piling onto the
    # ones that happen to run last.
    while read -r name gens; do
        # An "&& continue" here would be the last command of an AND-OR list, so a
        # false condition returns 1 and set -e kills the script on the first valid
        # line. Keep it as an if, which set -e exempts.
        if [[ $name == \#* || -z ${gens:-} ]]; then
            continue
        fi
        instance="$INSTANCE_DIR/$name"
        [[ -f $instance ]] || { echo "missing: $instance" >&2; exit 1; }

        if grep -q "^${CONFIG_NAME},${instance},.*,${SEED},${gens},${rep}," "$OUT" 2>/dev/null; then
            continue   # already measured, resume without redoing it
        fi

        read -r vertices _edges p _rest < "$instance"

        cat > "$CONF" <<EOF
BRKGA
MAXRUNS 1
debug 0
quiet 1
seed $SEED
control 0
strategy 1
restart 1
sizePool 10
maxGenerations $gens
EOF

        t_start=$(cpu_temp)
        ./runRKO "$instance" "$MAXTIME" "$CONF" >/dev/null 2>&1
        t_end=$(cpu_temp)

        # Results_RKO.csv columns end with: ofv, ofvAverage, timeBest, timeTotal, decodes, energy
        row=$(tail -1 "$RESULTS" | awk -F'\t' -v OFS=, '{print $(NF-4), $(NF-3), $(NF-2), $(NF-1), $NF}')

        echo "$CONFIG_NAME,$instance,$vertices,$p,$SEED,$gens,$rep,$row,${t_start:-},${t_end:-}" >> "$OUT"

        done_count=$((done_count + 1))
        printf '[rep %s/%s] %-12s gens=%-6s %s\n' "$rep" "$REPS" "$name" "$gens" "$row"

        sleep "$COOLDOWN"
    done < <(grep -v '^#' "$GENS" | shuf)
done

echo
echo "written: $OUT  ($done_count runs this session, $total instances x $REPS reps)"
