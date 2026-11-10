#!/usr/bin/env python3
"""Turns the CSV files from dscount_measure into PNG graphs. Reporting only: no DSP lives here."""
import csv, sys, collections, pathlib
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

d = pathlib.Path(sys.argv[1])
ink, grid = "#222", "#ddd"
colors = ["#2a6f97", "#c8553d", "#588157", "#8e6c8a", "#b08900", "#444"]
plt.rcParams.update({"font.size": 10, "axes.grid": True, "grid.color": grid, "axes.spines.top": False, "axes.spines.right": False})

def read(name):
    with open(d / name) as f:
        return list(csv.DictReader(f))

# static curves
rows = read("static_curves.csv")
groups = collections.OrderedDict()
for r in rows:
    groups.setdefault((r["ratio"], r["knee"]), []).append(r)
fig, ax = plt.subplots(figsize=(7, 5))
for i, ((ratio, knee), g) in enumerate(groups.items()):
    x = [float(r["input_db"]) for r in g]
    ax.plot(x, [float(r["computed_db"]) for r in g], color=colors[i % 6], lw=1.2, label=f"{ratio}:1, knee {knee}")
    ax.plot(x, [float(r["measured_db"]) for r in g], "o", color=colors[i % 6], ms=3)
ax.set(xlabel="Input level, dBFS", ylabel="Output level, dBFS", title="Static transfer: curve function (lines) against measured audio (dots), threshold -24 dB", ylim=(-52, 2))
ax.legend(frameon=False, fontsize=8)
fig.tight_layout(); fig.savefig(d / "static_curves.png", dpi=150)
worst = max(abs(float(r["measured_db"]) - float(r["computed_db"])) for r in rows if float(r["computed_db"]) > -60)
print(f"static worst error {worst:.3f} dB")

# bursts
rows = read("burst_response.csv")
groups = collections.OrderedDict()
for r in rows:
    groups.setdefault((r["burst_ms"], r["average_ms"]), []).append(r)
fig, ax = plt.subplots(figsize=(7, 4))
for i, ((b, a), g) in enumerate(groups.items()):
    t = [float(r["t_ms"]) for r in g]
    y = [float(r["gain_db"]) for r in g]
    keep = [(p, q) for p, q in zip(t, y) if -20 <= p <= 1800]
    ax.plot([p for p, q in keep], [q for p, q in keep], color=colors[i], label=f"burst {b} ms, Average {a} ms")
ax.set(xlabel="Time after burst end, ms", ylabel="Gain, dB", title="Release behaviour: Fast 30 ms, Slow 700 ms, 30 dB burst")
ax.legend(frameon=False, fontsize=8)
fig.tight_layout(); fig.savefig(d / "burst_response.png", dpi=150)

# crossover
rows = read("crossover.csv")
groups = collections.OrderedDict()
for r in rows:
    groups.setdefault(r["mode"], []).append(r)
fig, axes = plt.subplots(2, 2, figsize=(9, 5.5), sharex=True, sharey=True)
for ax, (mode, g) in zip(axes.flat, groups.items()):
    f = [float(r["freq"]) for r in g]
    ax.semilogx(f, [max(-130, float(r["band_db"])) for r in g], color=colors[0], label="band")
    ax.semilogx(f, [max(-130, float(r["complement_db"])) for r in g], color=colors[1], label="complement")
    ax.set(title=mode, ylim=(-130, 6))
axes[0][0].legend(frameon=False, fontsize=8)
for ax in axes[1]: ax.set_xlabel("Frequency, Hz")
for ax in axes[:, 0]: ax.set_ylabel("dB")
fig.tight_layout(); fig.savefig(d / "crossover.png", dpi=150)
