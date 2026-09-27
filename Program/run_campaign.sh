#!/usr/bin/env bash
# Usage:  ./run_campaign.sh [OUTPUT_CSV]
#         CONFIG_NAME=decoder REPS=10 ./run_campaign.sh
#         THREADS="0 1 4" WAIT_POLICIES=passive ./run_campaign.sh
#         GENS=/tmp/two_instances.txt CONFIG_NAME=check REPS=1 ./run_campaign.sh   (quick check)
set -euo pipefail
export LC_ALL=C

CONFIG_NAME=${CONFIG_NAME:-decoder}      # label of the campaign (file names, first CSV column)
REPS=${REPS:-10}
SEED=${SEED:-1234}
THREADS=${THREADS:-0 1 2 4 8 12}         # values of the "threads" config key; 0 = sequential
WAIT_POLICIES=${WAIT_POLICIES:-active passive}   # OMP_WAIT_POLICY values, for threads >= 2
WARMUP=${WARMUP:-1}                      # discarded runs at the start of every session
COOLDOWN=${COOLDOWN:-5}                  # minimum seconds between runs
COOLDOWN_MAX=${COOLDOWN_MAX:-120}        # never wait longer than this, seconds
TEMP_MARGIN=${TEMP_MARGIN:-3}            # cooldown ends within this many °C of idle
IDLE_SECONDS=${IDLE_SECONDS:-60}         # length of each idle power sample
MAXTIME=${MAXTIME:-600}                  # safety ceiling, must never bind

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

GENS=${GENS:-config/generations.txt}     # instances and budgets; point elsewhere for quick checks
INSTANCE_DIR=../Instances/aNpMP
RESULTS=../Results/Results_RKO.csv
OUT=${1:-../Results/campaign_${CONFIG_NAME}.csv}
IDLE_OUT=${OUT%.csv}_idle.csv
RAPL=/sys/class/powercap/intel-rapl:0

[[ -x ./runRKO ]] || { echo "runRKO not built. Run: make release" >&2; exit 1; }
[[ -f $GENS   ]] || { echo "missing $GENS. Run: ./calibrate_generations.sh" >&2; exit 1; }
[[ -r $RAPL/energy_uj ]] || { echo "RAPL not readable. Run: sudo chmod a+r $RAPL/energy_uj" >&2; exit 1; }
command -v perf >/dev/null || { echo "perf not installed" >&2; exit 1; }
(( $(cat /proc/sys/kernel/perf_event_paranoid) <= 2 )) ||
    { echo "perf blocked for users. Run: sudo sysctl kernel.perf_event_paranoid=2" >&2; exit 1; }

CONF=$(mktemp)
PERF_OUT=$(mktemp)
trap 'rm -f "$CONF" "$PERF_OUT"' EXIT

# Temperature is context, not data: never let a missing sensor abort a campaign
# that is already hours in.
cpu_temp() { sensors -u 'k10temp-*' 2>/dev/null | awk -F': ' '/temp1_input/{print $2; exit}' || true; }

perf_stats() {
    awk -F, '$3 ~ /^cycles/{c=$1} $3 ~ /^task-clock/{t=$1}
             END{if(c>0 && t>0) printf "%.3f %.3f\n", c/(t*1e6), t/1000; else print ""}' "$PERF_OUT"
}

# Idle package power over IDLE_SECONDS, for net energy (E - P_idle * T). Also sets
# IDLE_TEMP, the reference temperature the cooldown waits for.
measure_idle() {
    local label=$1 a b t0
    t0=$(cpu_temp)
    a=$(cat "$RAPL/energy_uj"); sleep "$IDLE_SECONDS"; b=$(cat "$RAPL/energy_uj")
    IDLE_TEMP=$(cpu_temp)
    [[ -s $IDLE_OUT ]] || echo "when,label,watts,temp_start,temp_end" > "$IDLE_OUT"
    awk -v a="$a" -v b="$b" -v r="$(cat "$RAPL/max_energy_range_uj")" -v s="$IDLE_SECONDS" \
        -v w="$(date -Is)" -v l="$label" -v t0="${t0:-}" -v t1="${IDLE_TEMP:-}" \
        'BEGIN{d=b-a; if(d<0)d+=r; printf "%s,%s,%.3f,%s,%s\n", w, l, d/1e6/s, t0, t1}' >> "$IDLE_OUT"
}

# Waits at least COOLDOWN seconds, then until the CPU is back within TEMP_MARGIN of
# the idle temperature, capped at COOLDOWN_MAX. Without a sensor it is a plain sleep.
cool_down() {
    local waited=$COOLDOWN t
    sleep "$COOLDOWN"
    while (( waited < COOLDOWN_MAX )) && t=$(cpu_temp) && [[ -n $t && -n ${IDLE_TEMP:-} ]] &&
          awk -v t="$t" -v i="$IDLE_TEMP" -v m="$TEMP_MARGIN" 'BEGIN{exit !(t > i + m)}'; do
        sleep 1; waited=$((waited + 1))
    done
}

# Every (instance, budget, threads, wait policy) pair of one repetition.
list_jobs() {
    local name gens t w
    while read -r name gens; do
        if [[ $name == \#* || -z ${gens:-} ]]; then
            continue
        fi
        for t in $THREADS; do
            if (( t < 2 )); then
                echo "$name $gens $t default"
            else
                for w in $WAIT_POLICIES; do echo "$name $gens $t $w"; done
            fi
        done
    done < "$GENS"
}

# Runs one job. Sets t_start, t_end, ghz, cpu_s and row (the solver's last result).
run_job() {
    local name=$1 gens=$2 threads=$3 policy=$4 wait_env
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
threads $threads
maxGenerations $gens
EOF
    if [[ $policy == default ]]; then wait_env=(-u OMP_WAIT_POLICY); else wait_env=("OMP_WAIT_POLICY=$policy"); fi

    t_start=$(cpu_temp)
    env "${wait_env[@]}" perf stat -x, -e cycles,task-clock -o "$PERF_OUT" -- \
        ./runRKO "$INSTANCE_DIR/$name" "$MAXTIME" "$CONF" >/dev/null 2>&1
    t_end=$(cpu_temp)
    read -r ghz cpu_s < <(perf_stats)

    # Results_RKO.csv columns end with: ofv, ofvAverage, timeBest, timeTotal, decodes, energy
    row=$(tail -1 "$RESULTS" | awk -F'\t' -v OFS=, '{print $(NF-5), $(NF-3), $(NF-2), $(NF-1), $NF}')
}

while read -r name _; do
    [[ -f $INSTANCE_DIR/$name ]] || { echo "missing: $INSTANCE_DIR/$name" >&2; exit 1; }
done < <(list_jobs)

mkdir -p "$(dirname "$OUT")" ../Results
if [[ ! -s $OUT ]]; then
    echo "config,threads,wait_policy,instance,vertices,p,seed,max_generations,repetition,ofv,time_best,time_total,decodes,joules,ghz,cpu_seconds,temp_start,temp_end" > "$OUT"
fi

per_rep=$(list_jobs | wc -l)
done_count=0

# Pin threads to physical cores: 1-6 threads get one core each, 8 and 12 add SMT
# siblings. Pinned also for the sequential runs, so every configuration runs under
# the same placement.
export OMP_PLACES=${OMP_PLACES:-cores} OMP_PROC_BIND=${OMP_PROC_BIND:-close}

# record_environment.sh runs before this script and cannot see these values, so log
# what this session actually used next to its record.
echo "campaign session $(date -Is): THREADS=\"$THREADS\" WAIT_POLICIES=\"$WAIT_POLICIES\" OMP_PLACES=$OMP_PLACES OMP_PROC_BIND=$OMP_PROC_BIND" \
    >> "../Results/campanha_env_${CONFIG_NAME}.txt"

measure_idle start

# Warm-up, discarded: the first instance with the most threads, so the whole CPU,
# page cache and clock governor reach a steady state before anything is measured.
read -r warm_name warm_gens _ < <(list_jobs)
warm_threads=$(printf '%s\n' $THREADS | sort -n | tail -1)
warm_policy=default
if (( warm_threads >= 2 )); then read -r warm_policy _ <<< "$WAIT_POLICIES"; fi
for i in $(seq 1 "$WARMUP"); do
    run_job "$warm_name" "$warm_gens" "$warm_threads" "$warm_policy"
    printf '[warm-up %s/%s] %-12s threads=%s %s  (discarded)\n' "$i" "$WARMUP" "$warm_name" "$warm_threads" "$warm_policy"
    cool_down
done

for rep in $(seq 1 "$REPS"); do
    while read -r name gens threads policy; do
        instance="$INSTANCE_DIR/$name"
        if grep -q "^${CONFIG_NAME},${threads},${policy},${instance},.*,${SEED},${gens},${rep}," "$OUT" 2>/dev/null; then
            continue   # already measured, resume without redoing it
        fi

        run_job "$name" "$gens" "$threads" "$policy"
        read -r vertices _edges p _rest < "$instance"
        echo "$CONFIG_NAME,$threads,$policy,$instance,$vertices,$p,$SEED,$gens,$rep,$row,$ghz,$cpu_s,${t_start:-},${t_end:-}" >> "$OUT"

        done_count=$((done_count + 1))
        printf '[rep %s/%s] %-12s threads=%-2s %-8s %s  %s GHz\n' "$rep" "$REPS" "$name" "$threads" "$policy" "$row" "$ghz"

        cool_down
    done < <(list_jobs | shuf)

    measure_idle "rep_$rep"
done

echo
echo "written: $OUT  ($done_count runs this session, $per_rep jobs x $REPS reps)"
