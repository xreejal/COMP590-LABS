#!/usr/bin/env python3

import argparse
import os
import re
import shutil
import subprocess
import time

FLAG_RE = re.compile(r"flag:\s*(\d+)")
PRED_RE = re.compile(r"Predicted flag:\s*(\d+)")


def parse_cpu_mk(path):
    sender = os.environ.get("SENDER_CPU")
    receiver = os.environ.get("RECEIVER_CPU")

    if sender is not None and receiver is not None:
        return int(sender), int(receiver)

    with open(path, "r", encoding="utf-8") as fp:
        for line in fp:
            if line.startswith("SENDER_CPU="):
                sender = line.split("=", 1)[1].strip()
            if line.startswith("RECEIVER_CPU="):
                receiver = line.split("=", 1)[1].strip()

    if sender is None or receiver is None:
        raise RuntimeError("Could not determine SENDER_CPU/RECEIVER_CPU from cpu.mk")

    return int(sender), int(receiver)


def wrap_with_taskset(core, cmd):
    if shutil.which("taskset") is None:
        return cmd
    return ["taskset", "-c", str(core)] + cmd


def read_until_pattern(proc, pattern, timeout):
    end = time.time() + timeout
    while time.time() < end:
        line = proc.stdout.readline()
        if not line:
            continue
        m = pattern.search(line)
        if m:
            return int(m.group(1))
    return None


def run_once(victim_bin, attacker_bin, sender_cpu, receiver_cpu, expected, rounds):
    taskset = shutil.which("taskset") is not None
    success = 0
    details = []

    for _ in range(rounds):
        victim_env = os.environ.copy()
        if expected is not None:
            victim_env["CTF_FLAG_SET"] = str(expected)

        victim_cmd = wrap_with_taskset(sender_cpu, ["./" + victim_bin]) if taskset else ["./" + victim_bin]
        attacker_cmd = wrap_with_taskset(receiver_cpu, ["./" + attacker_bin]) if taskset else ["./" + attacker_bin]

        victim = subprocess.Popen(victim_cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1, env=victim_env)
        true_flag = read_until_pattern(victim, FLAG_RE, 2.0)

        if true_flag is None and expected is not None:
            true_flag = expected

        attacker = subprocess.Popen(attacker_cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1)
        predicted = read_until_pattern(attacker, PRED_RE, 12.0)

        for proc in (attacker, victim):
            proc.terminate()
            try:
                proc.wait(timeout=1)
            except subprocess.TimeoutExpired:
                proc.kill()

        correct = true_flag is not None and predicted is not None and true_flag == predicted
        details.append((true_flag, predicted, correct))
        if correct:
            success += 1

    return success, rounds, details


def main():
    parser = argparse.ArgumentParser(description="Run Part 3 CTF reliability checks")
    parser.add_argument("--cpu-mk", default="../cpu.mk")
    parser.add_argument("--victim", choices=["victim-2", "victim-3", "victim-4"], default="victim-4")
    parser.add_argument("--rounds", type=int, default=10)
    parser.add_argument("--expected", type=int, default=-1)
    args = parser.parse_args()

    sender_cpu, receiver_cpu = parse_cpu_mk(args.cpu_mk)
    expected = args.expected if args.expected >= 0 else None

    success, total, details = run_once(
        victim_bin=args.victim,
        attacker_bin="attacker",
        sender_cpu=sender_cpu,
        receiver_cpu=receiver_cpu,
        expected=expected,
        rounds=args.rounds,
    )

    print(f"Part3 results: {success}/{total} correct")
    for i, (exp, pred, ok) in enumerate(details, 1):
        print(f"  run {i}: expected={exp} predicted={pred} {'OK' if ok else 'BAD'}")

    return 0 if success == total else 1


if __name__ == "__main__":
    raise SystemExit(main())
