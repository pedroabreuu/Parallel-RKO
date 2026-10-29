#!/usr/bin/env python3
import argparse
import os
import sys

import numpy as np
import pandas as pd
from scipy.stats import kruskal, mannwhitneyu

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from analyze import RESULTS, compare, derive, instance_number, load, summarise, validate

def per_run_ratios(runs):
    """Speedup and energy ratio of each parallel run against the mean of its sequential
    runs (same |P| and instance): five values per cell, for the tests across |P|."""
    seq = runs[runs["threads"] == 0].groupby(["population", "instance"])[["time_total", "joules"]].mean()
    par = runs[runs["threads"] > 0].copy()
    key = list(zip(par["population"], par["instance"]))
    par["speedup"] = seq.loc[key, "time_total"].to_numpy() / par["time_total"].to_numpy()
    par["energy_ratio"] = par["joules"].to_numpy() / seq.loc[key, "joules"].to_numpy()
    return par

def analyse(path, out):
    os.makedirs(out, exist_ok=True)
    runs = derive(load([path]))
    runs["population"] = runs["config"].str.extract(r"_p(\d+)$", expand=False).astype(int)

    report, tables = [], []
    for pop, group in runs.groupby("population"):
        lines, problems = validate(group)
        report += [f"|P| = {pop}: {'OK' if problems == 0 else f'{problems} PROBLEMS'}"] + ["  " + l for l in lines]
        by_instance, _, _ = compare(summarise(group), "seq")
        tables.append(by_instance.assign(population=pop))

    table = pd.concat(tables)[["instance", "vertices", "p", "threads", "population",
                               "speedup", "energy_ratio", "net_energy_ratio", "karp_flatt"]]
    table["np"] = table["vertices"] * table["p"]
    table["_n"] = table["instance"].map(instance_number)
    table = table.sort_values(["_n", "threads", "population"]).drop(columns="_n")
    table.to_csv(os.path.join(out, "populacao_razoes.csv"), index=False, float_format="%.4g")

    par = per_run_ratios(runs)
    tests = []
    for (instance, threads), cell in par.groupby(["instance", "threads"]):
        for metric in ["speedup", "energy_ratio"]:
            samples = [g[metric].to_numpy() for _, g in cell.groupby("population")]
            ref = cell[cell["population"] == 987][metric].to_numpy()
            means = cell.groupby("population")[metric].mean()
            row = {"instance": instance, "threads": threads, "metric": metric,
                   **{f"mean_p{p}": v for p, v in means.items()},
                   "spread_pct": (means.max() - means.min()) / means[987] * 100,
                   "kruskal_p": kruskal(*samples).pvalue}
            for p in means.index.drop(987):
                other = cell[cell["population"] == p][metric].to_numpy()
                row[f"mw_p{p}_vs_987"] = mannwhitneyu(other, ref).pvalue
            tests.append(row)
    tests = pd.DataFrame(tests)
    tests["_n"] = tests["instance"].map(instance_number)
    tests = tests.sort_values(["metric", "_n", "threads"]).drop(columns="_n")
    tests.to_csv(os.path.join(out, "populacao_testes.csv"), index=False, float_format="%.4g")

    with open(os.path.join(out, "validation.txt"), "w") as f:
        f.write("\n".join(report) + "\n")
    print("\n".join(report))
    print("\n" + table.to_string(index=False, float_format="%.3f"))
    print("\n" + tests.to_string(index=False, float_format="%.3g"))
    return table, tests

def selftest():
    rows = []
    for pop, par_time in [(100, 20.0), (987, 20.0), (2000, 30.0)]:
        for rep in range(1, 6):
            for threads, time in [(0, 60.0), (6, par_time + rep * 0.01)]:
                rows.append(dict(config=f"x_p{pop}", population=pop, threads=threads, instance="pmed1.txt",
                                 time_total=time, joules=time * 30))
    par = per_run_ratios(pd.DataFrame(rows))
    means = par.groupby("population")["speedup"].mean()
    assert np.isclose(means[100], means[987]) and means[2000] < means[987]
    assert np.allclose(par["energy_ratio"] * par["speedup"], 1)   # constant power: E ratio = 1/S
    print("selftest: OK")

def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("campaign", nargs="?", default=os.path.join(RESULTS, "campaign_populacao.csv"))
    parser.add_argument("--out", default=os.path.join(RESULTS, "analysis", "populacao"))
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()
    if args.selftest:
        return selftest()
    analyse(args.campaign, args.out)

if __name__ == "__main__":
    main()
