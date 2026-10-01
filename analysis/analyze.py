#!/usr/bin/env python3
import argparse
import glob
import os
import re
import sys
import tempfile

import numpy as np
import pandas as pd
from scipy.stats import gmean, wilcoxon

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RESULTS = os.path.join(ROOT, "Results")
OPTIMA = os.path.join(ROOT, "ExactSolver", "optimal_results.csv")

SLOTS = ["#2a78d6", "#eb6834", "#1baf7a", "#eda100", "#e87ba4", "#008300", "#4a3aa7", "#e34948"]
MARKERS = ["o", "s", "^", "D", "v", "P", "X", "h"]
KNOWN_THREADS = [1, 2, 4, 6, 8, 12]
KNOWN_POLICIES = ["active", "passive"]
INK, INK_2, MUTED, GRID, AXIS = "#0b0b0b", "#52514e", "#898781", "#e1e0d9", "#c3c2b7"
KEYS = ["threads", "wait_policy"]

def find_campaigns(directory):
    return sorted(p for p in glob.glob(os.path.join(directory, "campaign_*.csv")) if not p.endswith("_idle.csv"))

def instance_number(name):
    return int(re.search(r"\d+", name).group())

def setup_label(threads, policy):
    if threads == 0:
        return "seq"
    return f"{threads}t" if policy == "default" else f"{threads}t-{policy}"

def load(paths):
    frames = []
    for path in paths:
        runs = pd.read_csv(path)
        idle_path = path[:-len(".csv")] + "_idle.csv"
        if os.path.exists(idle_path):
            runs["idle_watts"] = pd.read_csv(idle_path)["watts"].mean()
        else:
            print(f"warning: {os.path.basename(idle_path)} missing, net energy left empty")
            runs["idle_watts"] = np.nan
        frames.append(runs)
    runs = pd.concat(frames, ignore_index=True)
    # campaigns recorded before these columns existed
    for column, default in [("wait_policy", "default"), ("ghz", np.nan), ("cpu_seconds", np.nan)]:
        if column not in runs:
            runs[column] = default
    runs["instance"] = runs["instance"].map(os.path.basename)
    runs["setup"] = [setup_label(t, w) for t, w in zip(runs["threads"], runs["wait_policy"])]
    return runs

def derive(runs):
    runs["watts"] = runs["joules"] / runs["time_total"]
    runs["net_joules"] = runs["joules"] - runs["idle_watts"] * runs["time_total"]
    runs["uj_per_decode"] = runs["joules"] / runs["decodes"] * 1e6
    runs["decodes_per_s"] = runs["decodes"] / runs["time_total"]
    runs["cpu_util"] = runs["cpu_seconds"] / runs["time_total"]
    return runs

def validate(runs):
    """Fixed work means every run of an instance, in every configuration and
    repetition, reports the same ofv and decodes. Anything else invalidates the
    comparison, so it is reported before any number."""
    lines, problems = [], 0

    work = runs.groupby("instance")[["ofv", "decodes"]].nunique()
    broken = work[(work["ofv"] > 1) | (work["decodes"] > 1)]
    if broken.empty:
        lines.append(f"fixed work: OK ({len(work)} instances, same ofv and decodes in every run)")
    else:
        problems += len(broken)
        lines.append(f"fixed work: FAIL in {len(broken)} instances")
        for name in sorted(broken.index, key=instance_number):
            rows = runs[runs["instance"] == name][["config", "setup", "repetition", "ofv", "decodes"]]
            lines.append(f"  {name}:\n" + rows.to_string(index=False))

    for (config, setup), group in runs.groupby(["config", "setup"], sort=False):
        reps = group.groupby("instance").size()
        idle = group["idle_watts"].iloc[0]
        temps = group[["temp_start", "temp_end"]].notna().all(axis=1).mean() * 100
        lines.append(
            f"{config} {setup}: {reps.size} instances, {reps.min()}-{reps.max()} reps each, "
            f"idle {idle:.2f} W, temperature recorded in {temps:.0f}% of runs")
        if reps.min() != reps.max():
            problems += 1
            lines.append("  WARNING: unequal repetitions across instances (campaign incomplete?)")

    return lines, problems

def summarise(runs):
    summary = runs.groupby(["setup", *KEYS, "instance"]).agg(
        vertices=("vertices", "first"), p=("p", "first"), reps=("repetition", "count"),
        ofv=("ofv", "first"), decodes=("decodes", "first"),
        time_mean=("time_total", "mean"), time_sd=("time_total", "std"),
        joules_mean=("joules", "mean"), joules_sd=("joules", "std"),
        net_joules_mean=("net_joules", "mean"), watts_mean=("watts", "mean"), ghz_mean=("ghz", "mean"),
        cpu_util_mean=("cpu_util", "mean"),
        uj_per_decode=("uj_per_decode", "mean"), decodes_per_s=("decodes_per_s", "mean"),
        temp_start=("temp_start", "mean"), temp_end=("temp_end", "mean"),
    ).reset_index()
    summary["time_cv_pct"] = summary["time_sd"] / summary["time_mean"] * 100
    summary["joules_cv_pct"] = summary["joules_sd"] / summary["joules_mean"] * 100

    optima = pd.read_csv(OPTIMA).set_index("instance")["optimum"]
    summary["gap_pct"] = (summary["ofv"] - summary["instance"].map(optima)) / summary["instance"].map(optima) * 100

    summary["_n"] = summary["instance"].map(instance_number)
    return summary.sort_values([*KEYS, "_n"]).drop(columns="_n")

def pick_baseline(summary, requested):
    setups = set(summary["setup"])
    baseline = requested or "seq"
    if baseline not in setups:
        sys.exit(f"baseline '{baseline}' not among configurations {sorted(setups)}; choose one with --baseline")
    return baseline

def compare(summary, baseline):
    base = summary[summary["setup"] == baseline].set_index("instance")
    others = summary[summary["setup"] != baseline].copy()
    b = others["instance"].map
    others["speedup"] = b(base["time_mean"]) / others["time_mean"]
    others["efficiency"] = others["speedup"] / others["threads"]
    # Karp-Flatt: experimentally determined serial fraction (Amdahl), defined for 2+ threads.
    p = others["threads"].where(others["threads"] >= 2)
    others["karp_flatt"] = (1 / others["speedup"] - 1 / p) / (1 - 1 / p)
    others["energy_ratio"] = others["joules_mean"] / b(base["joules_mean"])
    others["net_energy_ratio"] = others["net_joules_mean"] / b(base["net_joules_mean"])
    others["edp_ratio"] = (others["joules_mean"] * others["time_mean"]) / (
        b(base["joules_mean"]) * b(base["time_mean"]))
    ratios = ["speedup", "efficiency", "karp_flatt", "energy_ratio", "net_energy_ratio", "edp_ratio"]
    by_instance = others[["setup", *KEYS, "instance", "vertices", "p", *ratios, "cpu_util_mean", "ghz_mean"]]

    # Ratios average geometrically: a 2x gain and a 2x loss must cancel out.
    geo = lambda s: gmean(s.dropna()) if (s.dropna() > 0).any() else np.nan
    group = ["setup", *KEYS]
    by_size = by_instance.groupby([*group, "vertices"])[ratios].agg(geo).reset_index()

    overall = by_instance.groupby(group)[ratios].agg(geo).reset_index().sort_values(KEYS)
    pvalues = []
    for setup in overall["setup"]:
        paired = others[others["setup"] == setup]
        x, y = paired["joules_mean"], paired["instance"].map(base["joules_mean"])
        pvalues.append(wilcoxon(x, y).pvalue if len(paired) >= 6 and (x != y).any() else np.nan)
    overall["wilcoxon_p_energy"] = pvalues
    overall["instances"] = [len(by_instance[by_instance["setup"] == s]) for s in overall["setup"]]
    return by_instance, by_size, overall

def style(index, known, order):
    slot = known.index(index) if index in known else len(known) + order
    slot %= len(SLOTS)
    return SLOTS[slot], MARKERS[slot]

def setup_matplotlib():
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    plt.rcParams.update({
        "font.size": 9, "axes.titlesize": 10, "axes.labelsize": 9,
        "text.color": INK, "axes.labelcolor": INK_2, "xtick.color": MUTED, "ytick.color": MUTED,
        "axes.edgecolor": AXIS, "axes.spines.top": False, "axes.spines.right": False,
        "axes.grid": True, "grid.color": GRID, "grid.linewidth": 0.6, "axes.axisbelow": True,
        "legend.frameon": False, "figure.facecolor": "white", "axes.facecolor": "white",
    })
    return plt

def save(fig, out, name):
    fig.tight_layout()
    fig.savefig(os.path.join(out, name + ".pdf"))
    fig.savefig(os.path.join(out, name + ".png"), dpi=200)

POLICY_NAMES = {"active": "espera ativa", "passive": "espera bloqueante", "default": "padrão"}
X_LABEL = "trabalho por decodificação (n·p)"
SPEEDUP_LABEL = r"speedup ($T_{seq}/T_k$)"
ENERGY_LABEL = r"energia relativa ($E_k/E_{seq}$)"

def decimal_comma(ax, axis="y"):
    from matplotlib.ticker import FuncFormatter
    fmt = FuncFormatter(lambda v, _: f"{v:g}".replace(".", ","))
    (ax.yaxis if axis == "y" else ax.xaxis).set_major_formatter(fmt)

def policies_of(frame):
    found = sorted(set(frame["wait_policy"]) - {"default"})
    return found or ["default"]

def with_policy(frame, policy):
    """Runs of one wait policy plus the ones where the policy does not apply (0 and 1 thread)."""
    return frame[(frame["wait_policy"] == policy) | (frame["wait_policy"] == "default")]

def scatter_by_threads(ax, frame, column):
    """One series per thread count against n·p, with the no-change line at 1."""
    ax.axhline(1, color=MUTED, linewidth=1, linestyle="--", zorder=1)
    for order, (threads, group) in enumerate(frame.groupby("threads")):
        color, marker = style(threads, KNOWN_THREADS, order)
        ax.scatter(group["vertices"] * group["p"], group[column], s=22, color=color, marker=marker,
                   edgecolor="white", linewidth=0.6, zorder=3, label=f"{threads} thread{'s' * (threads > 1)}")
    ax.set_xscale("log")
    ax.set_xlabel(X_LABEL)
    decimal_comma(ax)

def plot_main(plt, by_instance, out):
    """Speedup and relative energy side by side, one policy: the main figure of the paper."""
    fig, (left, right) = plt.subplots(1, 2, figsize=(6.3, 2.9))
    scatter_by_threads(left, by_instance, "speedup")
    scatter_by_threads(right, by_instance, "energy_ratio")
    left.set_ylabel(SPEEDUP_LABEL)
    right.set_ylabel(ENERGY_LABEL)
    left.set_title("(a) speedup", loc="left", fontsize=9, color=INK_2)
    right.set_title("(b) energia", loc="left", fontsize=9, color=INK_2)
    handles, labels = left.get_legend_handles_labels()
    fig.legend(handles, labels, loc="lower center", ncol=len(labels), fontsize=8, handletextpad=0.2,
               columnspacing=1.0, bbox_to_anchor=(0.5, 0))
    fig.tight_layout(rect=(0, 0.08, 1, 1))
    fig.savefig(os.path.join(out, "fig_speedup_energia.pdf"))
    fig.savefig(os.path.join(out, "fig_speedup_energia.png"), dpi=200)
    plt.close(fig)

def plot_ratio(plt, by_instance, column, ylabel, out, name):
    """One panel per wait policy, for the wait-policy experiment."""
    policies = policies_of(by_instance)
    fig, axes = plt.subplots(1, len(policies), figsize=(3.2 * len(policies), 2.9), sharey=True, squeeze=False)
    for ax, policy in zip(axes[0], policies):
        scatter_by_threads(ax, with_policy(by_instance, policy), column)
        ax.set_title(POLICY_NAMES.get(policy, policy), loc="left", fontsize=9, color=INK_2)
    axes[0][0].set_ylabel(ylabel)
    axes[0][-1].legend(loc="best", fontsize=8)
    save(fig, out, name)
    plt.close(fig)

def plot_by_threads(plt, summary, column, fmt, ylabel, out, name):
    """One value per thread count averaged over instances, one line per wait policy."""
    if summary[column].isna().all():
        return
    policies = policies_of(summary)
    fig, ax = plt.subplots(figsize=(3.6, 2.6))
    for order, policy in enumerate(policies):
        values = with_policy(summary, policy).groupby("threads")[column].mean().dropna()
        color, marker = style(policy, KNOWN_POLICIES, order)
        ax.plot(values.index, values.values, color=color, linewidth=1.5, marker=marker, markersize=5,
                markeredgecolor="white", label=POLICY_NAMES.get(policy, policy))
        last = values.index[-1]
        ax.annotate(fmt.format(values[last]).replace(".", ","), (last, values[last]),
                    textcoords="offset points", xytext=(6, 0), va="center", fontsize=8, color=INK_2)
    ticks = sorted(summary["threads"].unique())
    ax.set_xticks(ticks, ["seq" if t == 0 else str(t) for t in ticks])
    ax.set_xlabel("threads")
    ax.set_ylabel(ylabel)
    decimal_comma(ax)
    if len(policies) > 1:
        ax.legend(fontsize=8)
    save(fig, out, name)
    plt.close(fig)

def analyse(paths, out, baseline_name=None, plots=True):
    os.makedirs(out, exist_ok=True)
    runs = derive(load(paths))

    report, problems = validate(runs)
    with open(os.path.join(out, "validation.txt"), "w") as f:
        f.write("\n".join(report) + "\n")
    print("\n".join(report))

    runs.to_csv(os.path.join(out, "runs.csv"), index=False)
    summary = summarise(runs)
    summary.to_csv(os.path.join(out, "summary_by_instance.csv"), index=False, float_format="%.6g")

    result = {"summary": summary, "problems": problems}
    if summary["setup"].nunique() < 2:
        print("\nonly one configuration: comparisons and figures skipped")
        return result

    baseline = pick_baseline(summary, baseline_name)
    by_instance, by_size, overall = compare(summary, baseline)
    by_instance.to_csv(os.path.join(out, "comparison_by_instance.csv"), index=False, float_format="%.6g")
    by_size.to_csv(os.path.join(out, "comparison_by_size.csv"), index=False, float_format="%.6g")
    overall.to_csv(os.path.join(out, "comparison_overall.csv"), index=False, float_format="%.6g")
    print(f"\nbaseline: {baseline}\n" + overall.to_string(index=False, float_format="%.3f"))

    if plots:
        plt = setup_matplotlib()
        if len(policies_of(by_instance)) == 1:
            plot_main(plt, by_instance, out)
        else:
            plot_ratio(plt, by_instance, "speedup", SPEEDUP_LABEL, out, "fig_speedup")
            plot_ratio(plt, by_instance, "energy_ratio", ENERGY_LABEL, out, "fig_energia")
            # Only informative when policies differ: active keeps every thread busy.
            plot_by_threads(plt, summary, "cpu_util_mean", "{:.1f}", "núcleos ocupados (CPU / tempo real)",
                            out, "fig_utilizacao")
        plot_by_threads(plt, summary, "watts_mean", "{:.1f} W", "potência média do pacote (W)",
                        out, "fig_potencia")

    result.update(by_instance=by_instance, overall=overall)
    return result


def selftest():
    """Known answers: 4 threads (active) take a third of the sequential time and half
    its energy, so speedup 3, efficiency 0.75, Karp-Flatt 1/9, energy ratio 0.5,
    EDP ratio 1/6. Then a changed decode count must be caught by validation."""
    header = ("config,threads,wait_policy,instance,vertices,p,seed,max_generations,repetition,ofv,time_best,"
              "time_total,decodes,joules,ghz,cpu_seconds,temp_start,temp_end\n")
    setups = [(0, "default", 60.0, 1800.0, 60.0), (1, "default", 62.0, 1850.0, 62.0), (4, "active", 20.0, 900.0, 80.0)]
    with tempfile.TemporaryDirectory() as tmp:
        def write(bad_instance=None):
            with open(os.path.join(tmp, "campaign_x.csv"), "w") as f:
                f.write(header)
                for threads, policy, time, joules, cpu in setups:
                    for i in range(1, 8):
                        decodes = 1000 * i + (threads == 4 and i == bad_instance)
                        for rep in (1, 2):
                            f.write(f"x,{threads},{policy},../Instances/aNpMP/pmed{i}.txt,100,5,1234,10,{rep},"
                                    f"{15000 + i},1,{time},{decodes},{joules},4.1,{cpu},40,41\n")
            with open(os.path.join(tmp, "campaign_x_idle.csv"), "w") as f:
                f.write("when,label,watts,temp_start,temp_end\nx,start,20,40,40\n")

        write()
        result = analyse(find_campaigns(tmp), os.path.join(tmp, "out"), plots=False)
        row = result["overall"].set_index("setup").loc["4t-active"]
        assert result["problems"] == 0
        assert np.isclose(row["speedup"], 3) and np.isclose(row["efficiency"], 0.75)
        assert np.isclose(row["karp_flatt"], 1 / 9)
        assert np.isclose(row["energy_ratio"], 0.5) and np.isclose(row["edp_ratio"], 1 / 6)
        # net energy: (900 - 20*20) / (1800 - 20*60) = 500 / 600
        assert np.isclose(row["net_energy_ratio"], 500 / 600)
        assert np.isclose(result["summary"].set_index("setup").loc["4t-active", "cpu_util_mean"].iloc[0], 4)

        write(bad_instance=3)
        assert analyse(find_campaigns(tmp), os.path.join(tmp, "out"), plots=False)["problems"] == 1
    print("\nselftest: OK")

def main():
    parser = argparse.ArgumentParser(description="Summarise measurement campaigns into tables and figures.")
    parser.add_argument("campaigns", nargs="*", help="campaign CSVs (default: Results/campaign_*.csv)")
    parser.add_argument("--baseline", help="configuration used as reference, e.g. seq or 1t (default: seq)")
    parser.add_argument("--out", default=os.path.join(RESULTS, "analysis"))
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()

    if args.selftest:
        return selftest()
    paths = args.campaigns or find_campaigns(RESULTS)
    if not paths:
        sys.exit("no campaign CSVs found in Results/")
    analyse(paths, args.out, args.baseline)

if __name__ == "__main__":
    main()
