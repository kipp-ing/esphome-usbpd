#!/usr/bin/env python3
"""Offline self-test of bench/run.py scenarios `cmd` and `replug` against a fake board (pty)
and a fake charger script (BENCH_CHARGER). No hardware, no smart plug. replug takes ~30 s."""
import json
import os
import subprocess
import sys
import tempfile
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from test_capture_pty import FakeBoard  # noqa: E402

RUNPY = os.path.join(os.path.dirname(HERE), "run.py")


def main():
    tmp = tempfile.mkdtemp(prefix="bench-run-")
    link = os.path.join(tmp, "fakeport")
    board = FakeBoard(link)
    stop = False

    def hb():
        n = 0
        while not stop:
            if board.alive:
                n += 1
                try:
                    board.emit(json.dumps({"t": board.t(), "ev": "hb", "n": n}))
                except OSError:
                    pass
            time.sleep(0.5)
    threading.Thread(target=hb, daemon=True).start()
    fake_ch = os.path.join(tmp, "charger.sh")
    with open(fake_ch, "w") as f:
        f.write(f'#!/bin/sh\necho "fake charger $1" >> {tmp}/charger.calls\necho "charger $1 -> HTTP 200"\n')
    os.chmod(fake_ch, 0o755)
    env = dict(os.environ, BENCH_RUNS=os.path.join(tmp, "runs"), BENCH_PORT=link, BENCH_CHARGER=fake_ch)
    fails = []

    def check(c, m):
        print(("ok   " if c else "FAIL ") + m)
        if not c:
            fails.append(m)

    r = subprocess.run([sys.executable, RUNPY, "cmd", "regs", "--secs", "2"], env=env, capture_output=True, text=True, timeout=60)
    print(r.stdout)
    check(r.returncode == 0, f"cmd rc={r.returncode} {r.stderr[-300:]}")
    run = os.readlink(os.path.join(env["BENCH_RUNS"], "last"))
    evs = [json.loads(l) for l in open(os.path.join(env["BENCH_RUNS"], run, "events.jsonl"))]
    check(run.endswith("-cmd"), f"run id {run}")
    check(any(e["ev"] == "bench.send" and e["cmd"] == "regs" for e in evs), "bench.send logged")
    check(any(e["ev"] == "cmd" and e.get("line") == "regs" for e in evs), "board echo captured")
    meta = json.load(open(os.path.join(env["BENCH_RUNS"], run, "meta.json")))
    check(meta.get("scenario") == "cmd" and "fw_git_sha" in meta and "ended" in meta, f"meta {meta}")

    t0 = time.time()
    r = subprocess.run([sys.executable, RUNPY, "replug", "--secs", "2"], env=env, capture_output=True, text=True, timeout=120)
    print(r.stdout)
    check(r.returncode == 0, f"replug rc={r.returncode} {r.stderr[-300:]}")
    check(time.time() - t0 >= 27, f"off time enforced >= 25 s (took {time.time() - t0:.1f})")
    run = os.readlink(os.path.join(env["BENCH_RUNS"], "last"))
    evs = [json.loads(l) for l in open(os.path.join(env["BENCH_RUNS"], run, "events.jsonl"))]
    ch = [e["state"] for e in evs if e["ev"] == "bench.charger"]
    check(ch == ["off", "on"], f"charger events {ch}")
    check(open(os.path.join(tmp, "charger.calls")).read().split() == ["fake", "charger", "off", "fake", "charger", "on"], "fake charger called off, on")
    r = subprocess.run([sys.executable, RUNPY.replace("run.py", "capture.py"), "status"], env=env, capture_output=True, text=True)
    check('"running": false' in r.stdout, "capture stopped after scenario")
    stop = True
    print("RESULT:", "PASS" if not fails else f"FAIL ({len(fails)})")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
