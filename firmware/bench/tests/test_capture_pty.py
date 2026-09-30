#!/usr/bin/env python3
"""Offline self-test of bench/capture.py against a fake board on a pty.

Covers: start, JSON + raw line capture with host_ts/run, send (single port owner),
bench event injection, USB re-enumeration (pty closed -> gone, new pty -> back), stop.
Runs in a temp runs dir (BENCH_RUNS) with BENCH_PORT pointing at a symlink to the pty.
"""
import json
import os
import pty
import tty
import select
import subprocess
import sys
import tempfile
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
CAP = os.path.join(os.path.dirname(HERE), "capture.py")


class FakeBoard:
    def __init__(self, link):
        self.link = link
        self.t0 = time.monotonic()
        self.got = []
        self.new_pty()

    def new_pty(self):
        self.master, slave = pty.openpty()
        tty.setraw(slave)  # no line-discipline echo of the board's own output
        name = os.ttyname(slave)
        self.slave = slave  # keep slave open so the master does not see EOF
        tmp = self.link + ".tmp"
        if os.path.lexists(tmp):
            os.unlink(tmp)
        os.symlink(name, tmp)
        os.replace(tmp, self.link)
        self.alive = True
        self.rx = threading.Thread(target=self._rx, daemon=True)
        self.rx.start()

    def t(self):
        return int((time.monotonic() - self.t0) * 1000)

    def emit(self, s):
        os.write(self.master, (s + "\r\n").encode())

    def _rx(self):
        buf = b""
        m = self.master
        while self.alive:
            if not select.select([m], [], [], 0.1)[0]:
                continue
            try:
                c = os.read(m, 1024)
            except OSError:
                return
            if not c:
                return
            buf += c
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                line = line.decode().strip()
                self.got.append(line)
                self.emit(json.dumps({"t": self.t(), "ev": "cmd", "line": line}))

    def unplug(self):
        self.alive = False
        self.rx.join()
        os.unlink(self.link)
        os.close(self.master)
        os.close(self.slave)


def cap(env, *args):
    r = subprocess.run([sys.executable, CAP, *args], env=env, capture_output=True, text=True, timeout=20)
    return r.returncode, r.stdout.strip(), r.stderr.strip()


def main():
    tmp = tempfile.mkdtemp(prefix="bench-cap-")
    link = os.path.join(tmp, "fakeport")
    board = FakeBoard(link)
    env = dict(os.environ, BENCH_RUNS=os.path.join(tmp, "runs"), BENCH_PORT=link)
    run = "selftest-pty"
    fails = []

    def check(cond, msg):
        print(("ok   " if cond else "FAIL ") + msg)
        if not cond:
            fails.append(msg)

    rc, out, err = cap(env, "start", "--run", run)
    check(rc == 0, f"start rc={rc} {out} {err}")
    time.sleep(0.8)
    board.emit("ESP-ROM:esp32c6-20220919")          # boot ROM noise
    board.emit(json.dumps({"t": board.t(), "ev": "boot", "build": "selftest-1"}))
    for i in range(3):
        board.emit(json.dumps({"t": board.t(), "ev": "hb", "n": i}))
        time.sleep(0.2)
    rc, out, _ = cap(env, "send", "regs")
    check(rc == 0 and '"ok": true' in out, f"send -> {out}")
    rc, out, _ = cap(env, "event", '{"ev":"bench.charger","state":"off"}')
    check(rc == 0, f"event -> {out}")
    time.sleep(0.5)
    check(board.got == ["regs"], f"board received {board.got}")
    rc, out, _ = cap(env, "status")
    st = json.loads(out)
    check(rc == 0 and st.get("state") == "open" and st.get("lines", 0) >= 6, f"status {out}")
    rc, _, err = cap(env, "start", "--run", "other")
    check(rc == 1, f"second start for another run refused: {err}")

    board.unplug()
    time.sleep(1.5)
    rc, out, _ = cap(env, "status")
    check(json.loads(out).get("state") == "gone", f"status after unplug {out}")
    board.new_pty()
    time.sleep(1.5)
    board.emit(json.dumps({"t": 5, "ev": "boot", "build": "selftest-2"}))
    board.emit("partial-without-newline-then-newline")
    time.sleep(0.5)
    rc, out, _ = cap(env, "stop")
    check(rc == 0, f"stop {out}")
    rc, out, _ = cap(env, "status")
    check(rc == 1 and '"running": false' in out, f"status after stop {out}")

    evs = [json.loads(l) for l in open(os.path.join(env["BENCH_RUNS"], run, "events.jsonl"))]
    kinds = [(e["ev"], e.get("state")) for e in evs]
    print("events:", kinds)
    check(all("host_ts" in e and e["run"] == run for e in evs), "every event has host_ts + run")
    check(("raw", None) in kinds and any(e.get("line", "").startswith("ESP-ROM") for e in evs), "raw line captured")
    check(sum(1 for e in evs if e["ev"] == "boot") == 2, "both boot lines captured")
    check(("bench.port", "gone") in kinds and ("bench.port", "back") in kinds, "port gone/back logged")
    check(any(e["ev"] == "cmd" and e.get("line") == "regs" for e in evs), "echo of sent cmd captured")
    ch = [e for e in evs if e["ev"] == "bench.charger"]
    check(len(ch) == 1 and ch[0].get("t_est") is True and isinstance(ch[0].get("t"), int), f"charger event with estimated t {ch}")
    check(kinds[0] == ("bench.capture", "start") and kinds[-1] == ("bench.capture", "stop"), "capture start/stop bracket")
    raw = open(os.path.join(env["BENCH_RUNS"], run, "raw.log")).read()
    check("ESP-ROM" in raw and '"ev": "hb"' in raw, "raw.log written")
    print("RESULT:", "PASS" if not fails else f"FAIL ({len(fails)})")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
