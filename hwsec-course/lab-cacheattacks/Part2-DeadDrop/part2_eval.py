#!/usr/bin/env python3

import argparse
import os
import queue
import re
import shutil
import subprocess
import threading
import time


RECEIVED_RE = re.compile(r">>>\s*RECEIVED:\s*(\d+)\s*\(hits=\d+/\d+, avg_lat=\d+\)")


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


def start_process(cmd, cwd):
    return subprocess.Popen(
        cmd,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        cwd=cwd,
        bufsize=1,
    )


def drain_stdout(pipe, q, stop):
    while not stop.is_set():
        line = pipe.readline()
        if not line:
            break
        q.put(line)


def run_once(cwd, sender_cpu, receiver_cpu, values, timeout_per_value, gap, warmup):
    receiver_cmd = wrap_with_taskset(receiver_cpu, ["./receiver"])
    receiver = start_process(receiver_cmd, cwd)

    q = queue.Queue()
    stop = threading.Event()
    t = threading.Thread(target=drain_stdout, args=(receiver.stdout, q, stop), daemon=True)
    t.start()

    try:
        receiver.stdin.write("\n")
        receiver.stdin.flush()
    except Exception:
        pass

    if warmup:
        time.sleep(warmup)

    total = len(values)
    ok = 0
    fail = 0
    results = []

    for value in values:
        while not q.empty():
            try:
                q.get_nowait()
            except queue.Empty:
                break

        sender_cmd = wrap_with_taskset(sender_cpu, ["./sender"])
        sender = start_process(sender_cmd, cwd)

        try:
            sender.stdin.write(f"{value}\n")
            sender.stdin.flush()
        except Exception:
            pass

        got = None
        end_time = time.time() + timeout_per_value
        while time.time() < end_time:
            try:
                line = q.get(timeout=0.1)
            except queue.Empty:
                continue
            m = RECEIVED_RE.search(line)
            if m:
                got = int(m.group(1))
                break

        try:
            sender.terminate()
            sender.wait(timeout=1)
        except subprocess.TimeoutExpired:
            sender.kill()

        if got == value:
            ok += 1
        else:
            fail += 1
        results.append((value, got))

        time.sleep(gap)

    receiver.terminate()
    try:
        receiver.wait(timeout=1)
    except subprocess.TimeoutExpired:
        receiver.kill()
    stop.set()

    return ok, fail, total, results


def main():
    parser = argparse.ArgumentParser(description="Run Part 2 Dead Drop reliability checks")
    parser.add_argument("--cpu-mk", default="../cpu.mk")
    parser.add_argument("--values", default="0,1,42,100,150,200,255")
    parser.add_argument("--repeat", type=int, default=1)
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--gap", type=float, default=0.5)
    parser.add_argument("--warmup", type=float, default=0.2)
    args = parser.parse_args()

    sender_cpu, receiver_cpu = parse_cpu_mk(args.cpu_mk)

    values = []
    for tok in args.values.split(","):
        tok = tok.strip()
        if tok:
            values.append(int(tok))
    values = values * args.repeat

    ok, fail, total, results = run_once(
        cwd=os.getcwd(),
        sender_cpu=sender_cpu,
        receiver_cpu=receiver_cpu,
        values=values,
        timeout_per_value=args.timeout,
        gap=args.gap,
        warmup=args.warmup,
    )

    print(f"Part2 results: {ok}/{total} ok, {fail} fail")
    for sent, recv in results:
        status = "match" if recv is not None and recv == sent else ("timeout" if recv is None else "mismatch")
        recv_text = recv if recv is not None else "None"
        print(f"  sent={sent} recv={recv_text} ({status})")

    return 0 if fail == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
