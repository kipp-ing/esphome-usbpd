#!/usr/bin/env python3
"""bench/run.py — bench scenarios; each makes a new run dir runs/<UTC>-<scenario>/.

  run.py boot   [--secs 10]                    stop capture, esptool hard reset, capture N s
  run.py replug [--off 25] [--secs 45] [--cycles N] ["<cmd>"]
                                               (charger off >= 25 s [-> send cmd] -> on -> capture N s) x N
  run.py cmd "<console cmd>" [--secs 5]        send a console line, capture N s
  common: --keep  leave capture running after the scenario (default: stop, port free)

Writes runs/<id>/meta.json, points runs/last at the run, prints the run id + a 5-line summary.
Internal helpers used by flash.sh / reset.sh:
  run.py _newrun <scenario> [--build-id X]     create run dir + meta + last link, print id
  run.py _meta <id> key=value ...              merge keys into meta.json
  run.py _waitbuild <id> --build X [--timeout 15]   wait for an event with "build"; 0 ok, 4 none, 5 mismatch
  run.py _summary <id>                         print the 5-line summary
  run.py _hardreset                            esptool hard reset (port must be free)
"""
import argparse
import datetime
import glob
import json
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import capture  # noqa: E402  (re-execs under the esphome venv if pyserial is missing)

RUNS = capture.RUNS
LAST_FLASH = os.path.join(RUNS, ".last_flash.json")


def esp_python():
    c = sorted(glob.glob("/opt/homebrew/Cellar/esphome/*/libexec/bin/python"))
    return c[-1] if c else sys.executable


def utc_id():
    return datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%SZ")


def fw_sha():
    try:
        sha = subprocess.run(["git", "-C", ROOT, "log", "-1", "--format=%h", "--", "fw"],
                             capture_output=True, text=True, timeout=10).stdout.strip()
        dirty = subprocess.run(["git", "-C", ROOT, "status", "--porcelain", "--", "fw"],
                               capture_output=True, text=True, timeout=10).stdout.strip()
        return (sha or "nogit") + ("-dirty" if dirty else "")
    except Exception:
        return "unknown"


def meta_path(run):
    return os.path.join(RUNS, run, "meta.json")


def read_meta(run):
    try:
        with open(meta_path(run)) as f:
            return json.load(f)
    except (OSError, ValueError):
        return {}


def write_meta(_run, **kv):
    m = read_meta(_run)
    m.update(kv)
    tmp = meta_path(_run) + ".tmp"
    with open(tmp, "w") as f:
        json.dump(m, f, indent=1)
        f.write("\n")
    os.replace(tmp, meta_path(_run))
    return m


def last_build_id():
    try:
        with open(LAST_FLASH) as f:
            return json.load(f).get("build_id")
    except (OSError, ValueError):
        return None


def new_run(scenario, build_id=None, **extra):
    os.makedirs(RUNS, exist_ok=True)
    run = f"{utc_id()}-{scenario}"
    n = 1
    while os.path.exists(os.path.join(RUNS, run)):
        n += 1
        run = f"{utc_id()}-{scenario}-{n}"
    os.makedirs(os.path.join(RUNS, run))
    write_meta(run, run=run, scenario=scenario, created=capture.host_ts(), fw_git_sha=fw_sha(),
               build_id=build_id or last_build_id(), **extra)
    link = os.path.join(RUNS, "last")
    tmp = link + ".tmp"
    if os.path.lexists(tmp):
        os.unlink(tmp)
    os.symlink(run, tmp)
    os.replace(tmp, link)
    return run


def events(run):
    p = os.path.join(RUNS, run, "events.jsonl")
    out = []
    try:
        with open(p) as f:
            for line in f:
                try:
                    out.append(json.loads(line))
                except ValueError:
                    pass
    except OSError:
        pass
    return out


def bench_event(run, obj):
    """Through the daemon if it owns this run, else append directly."""
    info = capture.running()
    if info and info.get("run") == run:
        try:
            capture.rpc({"op": "event", "obj": obj})
            return
        except OSError:
            pass
    capture.append_event(run, obj)


def cap_start(run):
    if capture.running():  # hand the port over: one owner at a time
        capture.main(["stop"])
    rc = capture.main(["start", "--run", run])
    if rc != 0:
        sys.exit(f"run: capture start failed rc={rc}")


def cap_stop():
    capture.main(["stop"])


def is_error(e):
    ev = str(e.get("ev", ""))
    return ("err" in ev or "fail" in ev or "panic" in ev or e.get("level") in ("E", "error")
            or "err" in e or "error" in e
            or (ev == "raw" and any(k in e.get("line", "") for k in ("Guru Meditation", "abort()", "panic", "Backtrace"))))


def summary(run):
    evs = events(run)
    counts = {}
    for e in evs:
        counts[str(e.get("ev"))] = counts.get(str(e.get("ev")), 0) + 1
    errs = [e for e in evs if is_error(e)]
    boots = [e for e in evs if e.get("ev") == "boot"]
    short = lambda e: json.dumps({k: v for k, v in e.items() if k not in ("run", "host_ts")}, separators=(",", ":"))[:160]
    top = ", ".join(f"{k} {v}" for k, v in sorted(counts.items(), key=lambda kv: -kv[1])[:10])
    lines = [
        f"run: {run}  ({len(evs)} events)",
        f"ev counts: {top or '-'}",
        f"boot: {len(boots)}x" + (f", build={boots[-1].get('build')}" if boots else ""),
        f"first error: {short(errs[0]) if errs else '-'}",
        f"last error: {short(errs[-1]) if len(errs) > 1 else ('same' if errs else '-')}",
    ]
    return "\n".join(lines)


def wait_build(run, want, timeout):
    """0 = event with build==want seen, 4 = no build-carrying event, 5 = mismatch."""
    deadline = time.time() + timeout
    seen = None
    while time.time() < deadline:
        for e in events(run):
            if e.get("ev") == "boot" or "build" in e:
                if "build" in e:
                    seen = e["build"]
                    if seen == want:
                        return 0, seen
        time.sleep(0.5)
    return (5 if seen is not None else 4), seen


def hard_reset():
    port = subprocess.run([os.path.join(HERE, "port.sh")], capture_output=True, text=True).stdout.strip()
    if not port or port == "NO_BOARD":
        print("NO_BOARD")
        return 3
    r = subprocess.run([esp_python(), "-m", "esptool", "--chip", "esp32c6", "--port", port,
                        "--connect-attempts", "3", "--after", "hard-reset", "chip-id"],
                       capture_output=True, text=True, timeout=60)
    out = (r.stdout + r.stderr).strip().splitlines()
    print("\n".join(l for l in out if any(k in l for k in ("MAC", "Hard resetting", "rror", "ailed")))[-600:])
    return r.returncode


def charger(run, state):
    script = os.environ.get("BENCH_CHARGER", os.path.join(HERE, "charger.sh"))  # override: self-test only
    r = subprocess.run([script, state], capture_output=True, text=True, timeout=40)
    ok = r.returncode == 0
    bench_event(run, {"ev": "bench.charger", "state": state if ok else "error", "want": state,
                      "rc": r.returncode, "out": (r.stdout + r.stderr).strip()[-120:]})
    return ok


def sleep_with_status(secs):
    time.sleep(secs)


def scenario(a):
    sc = a.scenario
    extra = {"args": {k: v for k, v in vars(a).items() if k not in ("scenario",) and v is not None}}
    if a.port_check:
        p = subprocess.run([os.path.join(HERE, "port.sh")], capture_output=True, text=True)
        if p.returncode != 0:
            print("NO_BOARD")
            return 3
    run = new_run(sc, **extra)
    rc = 0
    try:
        if sc == "boot":
            cap_stop()
            capture.append_event(run, {"ev": "bench.reset", "via": "esptool"})
            rrc = hard_reset()
            if rrc != 0:
                capture.append_event(run, {"ev": "bench.reset", "state": "error", "rc": rrc})
                rc = 1
            cap_start(run)
            time.sleep(a.secs or 10)
        elif sc == "replug":
            off = max(a.off or 25, 25)
            cap_start(run)
            time.sleep(1)
            if not charger(run, "off"):
                rc = 1
                print("run: charger off FAILED — aborting, charger state unknown", file=sys.stderr)
            else:
                for cyc in range(max(a.cycles or 1, 1)):
                    if cyc:
                        if not charger(run, "off"):
                            rc = 1
                            print("run: charger off FAILED — aborting, charger state unknown", file=sys.stderr)
                            break
                    time.sleep(off)
                    if a.cmd:   # console line while the charger is off (e.g. "pd want 9000" before attach)
                        rep = capture.rpc({"op": "send", "cmd": a.cmd})
                        if not rep.get("ok"):
                            print(f"run: send failed: {rep}", file=sys.stderr)
                            rc = 1
                        time.sleep(1)
                    if not charger(run, "on") and not charger(run, "on"):
                        rc = 1
                        print("run: charger on FAILED twice — operator needed", file=sys.stderr)
                        break
                    time.sleep(a.secs or 45)
        elif sc == "cmd":
            if not a.cmd:
                sys.exit("run: cmd needs the console command")
            cap_start(run)
            time.sleep(1)
            rep = capture.rpc({"op": "send", "cmd": a.cmd})
            if not rep.get("ok"):
                print(f"run: send failed: {rep}", file=sys.stderr)
                rc = 1
            time.sleep(a.secs or 5)
    finally:
        if not a.keep:
            cap_stop()
        boots = [e for e in events(run) if e.get("ev") in ("boot", "hb") and e.get("build")]
        write_meta(run, ended=capture.host_ts(), rc=rc,
                   observed_build=(boots[-1].get("build") if boots else None))
    print(summary(run))
    return rc


def main(argv):
    if argv and argv[0].startswith("_"):
        op, rest = argv[0], argv[1:]
        if op == "_newrun":
            ap = argparse.ArgumentParser()
            ap.add_argument("scenario")
            ap.add_argument("--build-id")
            ap.add_argument("--project")
            x = ap.parse_args(rest)
            print(new_run(x.scenario, x.build_id, **({"project": x.project} if x.project else {})))
            return 0
        if op == "_meta":
            kv = {}
            for s in rest[1:]:
                k, _, v = s.partition("=")
                kv[k] = v
            write_meta(rest[0], **kv)
            return 0
        if op == "_waitbuild":
            ap = argparse.ArgumentParser()
            ap.add_argument("run")
            ap.add_argument("--build", required=True)
            ap.add_argument("--timeout", type=float, default=15)
            x = ap.parse_args(rest)
            rc, seen = wait_build(x.run, x.build, x.timeout)
            print({0: "BUILD_OK", 4: "NO_BUILD_LINE", 5: "BUILD_MISMATCH"}[rc] + f" want={x.build} seen={seen}")
            return rc
        if op == "_summary":
            print(summary(rest[0]))
            return 0
        if op == "_hardreset":
            return hard_reset()
        if op == "_event":
            bench_event(rest[0], json.loads(rest[1]))
            return 0
        sys.exit(f"unknown helper {op}")
    ap = argparse.ArgumentParser(prog="bench/run.py", description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("scenario", choices=["boot", "replug", "cmd"])
    ap.add_argument("cmd", nargs="?")
    ap.add_argument("--secs", type=float)
    ap.add_argument("--off", type=float)
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--cycles", type=int, help="replug: number of off/on cycles in one run")
    ap.add_argument("--no-port-check", dest="port_check", action="store_false")
    return scenario(ap.parse_args(argv))


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
