#!/usr/bin/env python3
"""Aggregate concurrent-queue benchmark CSVs and plot throughput / latency."""
from __future__ import annotations

import argparse
import re
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

HEADER_RE = re.compile(
    r"queue=(?P<queue>\S+)\s+"
    r"n_threads=(?P<n_threads>\d+)\s+"
    r"ops_per_thread=(?P<ops_per_thread>\d+)\s+"
    r"wall_s=(?P<wall_s>[\d.eE+-]+)\s+"
    r"work_iters=(?P<work_iters>\d+)\s+"
    r"max_samples=(?P<max_samples>\d+)"
)

FNAME_RE = re.compile(r"(?P<queue>[A-Za-z]+Queue)_threads(?P<threads>\d+)_trial(?P<trial>\d+)\.csv")

QUEUE_ORDER = [
    "MutexQueue",
    "TwoLockQueue",
    "MSQueue",
    "ValoisQueue",
    "PLJQueue",
    "LCRQueue",
    "LPRQueue",
]

QUEUE_COLOR = {
    "MutexQueue":   "#7f7f7f",
    "TwoLockQueue": "#bcbd22",
    "MSQueue":      "#1f77b4",
    "ValoisQueue":  "#17becf",
    "PLJQueue":     "#2ca02c",
    "LCRQueue":     "#d62728",
    "LPRQueue":     "#9467bd",
}

QUEUE_MARKER = {
    "MutexQueue":   "x",
    "TwoLockQueue": "+",
    "MSQueue":      "o",
    "ValoisQueue":  "s",
    "PLJQueue":     "^",
    "LCRQueue":     "D",
    "LPRQueue":     "v",
}

PHYS_CORES = 96
LOGICAL_CPUS = 192


def parse_one(path: Path) -> dict:
    with path.open() as f:
        header = f.readline().lstrip("# ").strip()
    m = HEADER_RE.search(header)
    if not m:
        raise RuntimeError(f"could not parse header in {path}: {header!r}")
    meta = m.groupdict()
    n_threads = int(meta["n_threads"])
    ops_per_thread = int(meta["ops_per_thread"])
    wall_s = float(meta["wall_s"])
    total_ops = ops_per_thread * n_threads * 2  # enq + deq
    throughput_mops = total_ops / wall_s / 1e6

    df = pd.read_csv(path, comment="#")
    e2e = df["e2e_ns"].to_numpy(dtype=np.int64)
    enq = df["enqueue_ns"].to_numpy(dtype=np.int64)
    deq = df["dequeue_ns"].to_numpy(dtype=np.int64)

    fname_m = FNAME_RE.match(path.name)
    trial = int(fname_m.group("trial")) if fname_m else 0

    return {
        "queue":            meta["queue"],
        "n_threads":        n_threads,
        "trial":            trial,
        "wall_s":           wall_s,
        "ops_per_thread":   ops_per_thread,
        "throughput_mops":  throughput_mops,
        "e2e_p50":          float(np.median(e2e)),
        "e2e_mean":         float(np.mean(e2e)),
        "e2e_p99":          float(np.percentile(e2e, 99)),
        "e2e_p999":         float(np.percentile(e2e, 99.9)),
        "enq_p50":          float(np.median(enq)),
        "deq_p50":          float(np.median(deq)),
    }


def load_results(results_dir: Path) -> pd.DataFrame:
    rows = []
    for path in sorted(results_dir.glob("*.csv")):
        if not FNAME_RE.match(path.name):
            continue
        rows.append(parse_one(path))
    df = pd.DataFrame(rows)
    df["queue"] = pd.Categorical(df["queue"], categories=QUEUE_ORDER, ordered=True)
    return df.sort_values(["queue", "n_threads", "trial"]).reset_index(drop=True)


def aggregate(df: pd.DataFrame) -> pd.DataFrame:
    metrics = ["throughput_mops", "e2e_p50", "e2e_mean", "e2e_p99", "e2e_p999",
               "enq_p50", "deq_p50"]
    g = df.groupby(["queue", "n_threads"], observed=True)[metrics]
    agg = g.agg(["mean", "std", "min", "max"]).reset_index()
    agg.columns = ["_".join(c).rstrip("_") for c in agg.columns]
    agg["n_trials"] = g.size().values
    return agg


def add_smt_markers(ax: plt.Axes) -> None:
    ax.axvline(PHYS_CORES, color="black", linestyle=":", linewidth=0.8, alpha=0.5)
    ax.axvline(LOGICAL_CPUS, color="black", linestyle=":", linewidth=0.8, alpha=0.5)


def plot_metric(
    agg: pd.DataFrame,
    ax: plt.Axes,
    metric: str,
    *,
    title: str,
    ylabel: str,
    yscale: str = "linear",
    show_errorbars: bool = True,
    queues: list[str] | None = None,
) -> None:
    mean_col = f"{metric}_mean"
    min_col, max_col = f"{metric}_min", f"{metric}_max"
    for q in (queues if queues is not None else QUEUE_ORDER):
        sub = agg[agg["queue"] == q].sort_values("n_threads")
        if sub.empty:
            continue
        x = sub["n_threads"].to_numpy()
        y = sub[mean_col].to_numpy()
        if show_errorbars:
            yerr_lo = y - sub[min_col].to_numpy()
            yerr_hi = sub[max_col].to_numpy() - y
            ax.errorbar(x, y,
                        yerr=[yerr_lo, yerr_hi],
                        marker=QUEUE_MARKER[q], color=QUEUE_COLOR[q],
                        linewidth=1.4, markersize=5.5,
                        elinewidth=0.9, capsize=3, capthick=0.9,
                        label=q)
        else:
            ax.plot(x, y,
                    marker=QUEUE_MARKER[q], color=QUEUE_COLOR[q],
                    linewidth=1.4, markersize=5.5, label=q)
    ax.set_xscale("log", base=2)
    ax.set_yscale(yscale)
    ax.set_xticks([1, 2, 4, 8, 16, 32, 48, 64, 96, 144, 192])
    ax.set_xticklabels([1, 2, 4, 8, 16, 32, 48, 64, 96, 144, 192], fontsize=8)
    ax.set_xlim(0.9, 220)
    ax.set_xlabel("threads")
    ax.set_ylabel(ylabel)
    ax.set_title(title, fontsize=11)
    ax.grid(True, which="major", alpha=0.3)
    ax.grid(True, which="minor", alpha=0.1)
    add_smt_markers(ax)


PLOTS = [
    ("throughput",          "throughput_mops", "Throughput vs. threads",                   "throughput (Mops/s, enq+deq)", "linear", None),
    ("throughput_classical","throughput_mops", "Throughput vs. threads (excl. LCR/LPR)",   "throughput (Mops/s, enq+deq)", "linear", [q for q in QUEUE_ORDER if q not in ("LCRQueue", "LPRQueue")]),
    ("latency_p50",         "e2e_p50",         "Median end-to-end latency",                "latency (ns)",                 "log",    None),
    ("latency_p99",         "e2e_p99",         "p99 end-to-end latency",                   "latency (ns)",                 "log",    None),
    ("latency_mean",        "e2e_mean",        "Mean end-to-end latency",                  "latency (ns)",                 "log",    None),
    ("enq_p50",             "enq_p50",         "Median enqueue latency",                   "latency (ns)",                 "log",    None),
    ("deq_p50",             "deq_p50",         "Median dequeue latency",                   "latency (ns)",                 "log",    None),
]

SUPTITLE = ("AWS m8i.metal-48xl (Xeon 6975P-C, 96C/192T, 3 NUMA nodes)")


def make_single_plot(agg: pd.DataFrame, out_path: Path,
                     metric: str, title: str, ylabel: str, yscale: str,
                     queues: list[str] | None = None) -> None:
    fig, ax = plt.subplots(figsize=(8, 5.5))
    plot_metric(agg, ax, metric, title=title, ylabel=ylabel, yscale=yscale,
                queues=queues)
    ax.legend(loc="best", fontsize=9, frameon=True, framealpha=0.85)
    fig.suptitle(SUPTITLE, fontsize=10, y=0.995)
    fig.tight_layout(rect=(0, 0, 1, 0.97))
    fig.savefig(out_path, dpi=140, bbox_inches="tight")
    plt.close(fig)
    print(f"wrote {out_path}")


def make_plots(agg: pd.DataFrame, out_dir: Path) -> None:
    for name, metric, title, ylabel, yscale, queues in PLOTS:
        make_single_plot(agg, out_dir / f"plot_{name}.png",
                         metric, title, ylabel, yscale, queues)


def print_summary(agg: pd.DataFrame) -> None:
    pivot_thr = agg.pivot(index="n_threads", columns="queue",
                          values="throughput_mops_mean").round(2)
    pivot_p50 = agg.pivot(index="n_threads", columns="queue",
                          values="e2e_p50_mean").round(0).astype("Int64")
    pivot_p99 = agg.pivot(index="n_threads", columns="queue",
                          values="e2e_p99_mean").round(0).astype("Int64")
    print("\n=== Throughput (Mops/s, mean of 5 trials) ===")
    print(pivot_thr.to_string())
    print("\n=== Median e2e latency (ns) ===")
    print(pivot_p50.to_string())
    print("\n=== p99 e2e latency (ns) ===")
    print(pivot_p99.to_string())


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--results", type=Path, default=Path("results"))
    ap.add_argument("--out-dir", type=Path, default=Path("plots"))
    args = ap.parse_args()

    df = load_results(args.results)
    print(f"loaded {len(df)} runs across {df['queue'].nunique()} queues "
          f"and {df['n_threads'].nunique()} thread counts")
    agg = aggregate(df)
    args.out_dir.mkdir(parents=True, exist_ok=True)
    agg.to_csv(args.out_dir / "summary.csv", index=False)
    print(f"wrote {args.out_dir / 'summary.csv'}")
    make_plots(agg, args.out_dir)
    print_summary(agg)


if __name__ == "__main__":
    main()
