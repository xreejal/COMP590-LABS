import json
import os
import subprocess

NUM_RUNS = 100
EXE = ["./main"]
DATA_DIR = "data"

os.makedirs(DATA_DIR, exist_ok=True)

for run_id in range(NUM_RUNS):
    p = subprocess.run(EXE, check=True, capture_output=True, text=True)
    raw = p.stdout.strip()

    payload = json.loads(raw)
    with open(os.path.join(DATA_DIR, f"run{run_id}.json"), "w", encoding="utf-8") as fp:
        json.dump(payload, fp)

print(f"Collected {NUM_RUNS} run(s) in {DATA_DIR}/")
