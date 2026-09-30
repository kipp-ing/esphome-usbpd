#!/usr/bin/env python3
"""bench/panic.py RUN [--elf PATH] [--dry] — decode ESP-IDF panic dumps in a run (mr-bench).

Scans the run's raw lines (Guru Meditation / Detected in task / MEPC+RA / Stack memory) and
appends one `bench.panic` event per dump:
  {"ev":"bench.panic","n":i,"why":"Stack protection fault","task":"main","pc":"0x..","pc_fn":"log_rx",
   "pc_src":"fw/src/pd_drv.c:102","ra_fn":..,"ra_src":..,"sp":..,"stack":"0x..-0x..",
   "frames":["rx_one fw/src/pd_drv.c:160", ...]}   # code addrs found in the stack memory dump
ELF: default fw/.pio/build/*/firmware.elf — only valid if it is the build that ran (compare build ids).
Query: bench/q --run RUN --ev bench.panic --full
"""
import argparse
import glob
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import run as runpy  # noqa: E402

A2L = os.path.expanduser("~/.platformio/packages/toolchain-riscv32-esp/bin/riscv32-esp-elf-addr2line")


def a2l(elf, addrs):
    if not addrs:
        return {}
    out = {}
    p = subprocess.run([A2L, "-fC", "-e", elf] + addrs, capture_output=True, text=True)
    lines = p.stdout.splitlines()
    for k, a in enumerate(addrs):
        fn, src = lines[2 * k], lines[2 * k + 1]
        src = src.split(" (discriminator")[0].replace(ROOT + "/", "")
        src = re.sub(r"^.*/packages/framework-espidf/", "idf/", src)
        out[a] = (fn, src)
    return out


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("run")
    ap.add_argument("--elf")
    ap.add_argument("--dry", action="store_true")
    a = ap.parse_args(argv)
    run = a.run
    if not os.path.isdir(os.path.join(runpy.RUNS, run)):
        c = [r for r in os.listdir(runpy.RUNS) if a.run in r]
        if len(c) != 1:
            sys.exit(f"panic: run '{a.run}' not unique/found: {c}")
        run = c[0]
    elf = a.elf or (sorted(glob.glob(os.path.join(ROOT, "fw/.pio/build/*/firmware.elf"))) or [None])[-1]
    if not elf:
        sys.exit("panic: no ELF")
    evs = runpy.events(run)
    if any(e.get("ev") == "bench.panic" for e in evs):
        sys.exit("panic: run already has bench.panic events")
    dumps, cur = [], None
    for e in evs:
        line = e.get("line", "") if e.get("ev") == "raw" else ""
        if "Guru Meditation Error" in line:
            m = re.search(r"\(([^)]*)\)", line)
            cur = {"why": m.group(1) if m else line.strip(), "stackmem": []}
            dumps.append(cur)
        elif cur is None:
            continue
        elif m := re.search(r'Detected in task "(\w+)" at (0x[0-9a-f]+)', line):
            cur["task"], cur["det"] = m.group(1), m.group(2)
        elif m := re.search(r"MEPC\s*:\s*(0x[0-9a-f]+)\s+RA\s*:\s*(0x[0-9a-f]+)\s+SP\s*:\s*(0x[0-9a-f]+)", line):
            cur["pc"], cur["ra"], cur["sp"] = m.groups()
        elif m := re.match(r"Stack bounds: (0x[0-9a-f]+) - (0x[0-9a-f]+)", line):
            cur["stack"] = f"{m.group(1)}-{m.group(2)}"
        elif re.match(r"[0-9a-f]{8}: 0x", line):
            cur["stackmem"] += re.findall(r"0x4200[0-9a-f]{4}|0x420[0-9a-f]{5}", line)
        elif e.get("ev") != "raw":
            cur = None
    addrs = sorted({x for d in dumps for x in [d.get("pc"), d.get("ra")] + d["stackmem"] if x})
    names = a2l(elf, addrs)
    elf_rel = os.path.relpath(elf, ROOT)
    for i, d in enumerate(dumps, 1):
        ev = {"ev": "bench.panic", "n": i, "why": d["why"], "task": d.get("task"), "pc": d.get("pc"),
              "ra": d.get("ra"), "sp": d.get("sp"), "stack": d.get("stack"), "elf": elf_rel}
        for k in ("pc", "ra"):
            if d.get(k) in names:
                ev[k + "_fn"], ev[k + "_src"] = names[d[k]]
        fr = []
        for x in d["stackmem"]:
            fn, src = names.get(x, ("??", ""))
            if fn != "??" and (not fr or fr[-1] != f"{fn} {src}"):
                fr.append(f"{fn} {src}")
        ev["frames"] = fr
        print(json.dumps(ev))
        if not a.dry:
            runpy.bench_event(run, ev)
    print(f"panic: {len(dumps)} dump(s) in {run}", file=sys.stderr)


if __name__ == "__main__":
    main(sys.argv[1:])
