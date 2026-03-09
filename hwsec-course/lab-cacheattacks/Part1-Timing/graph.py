import glob
import json
import os

import matplotlib.pyplot as plt
import numpy as np

NUM_RUNS = 100
DATA_DIR = "data"
OUT_DIR = "graphs"

def main():
    os.makedirs(OUT_DIR, exist_ok=True)

    l1_all = []
    l2_all = []
    l3_all = []
    dram_all = []

    files = sorted(glob.glob(os.path.join(DATA_DIR, "run*.json")))
    for path in files[:NUM_RUNS]:
        with open(path, "r", encoding="utf-8") as fp:
            payload = json.load(fp)
            l1_all.extend(payload["l1"])
            l2_all.extend(payload["l2"])
            l3_all.extend(payload["l3"])
            dram_all.extend(payload["dram"])

    plt.figure(figsize=(10, 6))
    bins = np.arange(0, 400)

    plt.hist(l1_all, bins=bins, alpha=0.5, label="L1")
    plt.hist(l2_all, bins=bins, alpha=0.5, label="L2")
    plt.hist(l3_all, bins=bins, alpha=0.5, label="L3")
    plt.hist(dram_all, bins=bins, alpha=0.5, label="DRAM")

    plt.title("Cache Access Latency Distribution")
    plt.xlabel("Cycles")
    plt.ylabel("Samples")
    plt.legend()

    out = os.path.join(OUT_DIR, "histogram.pdf")
    plt.savefig(out)
    print(f"saved {out}")


if __name__ == "__main__":
    main()
