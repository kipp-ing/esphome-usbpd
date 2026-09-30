#!/usr/bin/env python3
"""bench/capture.py — single owner of the board's serial port (mr-bench).

  capture.py start --run ID     spawn the daemon; writes runs/ID/raw.log + runs/ID/events.jsonl
  capture.py stop               stop the daemon (port is free afterwards)
  capture.py status             one JSON line: running, run, port, state, lines, ...
  capture.py send "<cmd>"       forward a console line to the board through the daemon
  capture.py event '<json>'     append a bench event (e.g. bench.charger) through the daemon

events.jsonl: one JSON object per line. Device JSON lines are kept as-is plus "host_ts"
(ISO ms, UTC) and "run"; non-JSON lines become {"ev":"raw","line":...}. Bench events use
ev "bench.*" and carry "t" estimated from the last device line (plus "t_est": true).
The daemon survives USB re-enumeration (logs {"ev":"bench.port","state":"gone|back"}).
Port: BENCH_PORT env overrides discovery of /dev/cu.usbmodem*.
"""
import datetime
import glob
import json
import os
import signal
import socket
import subprocess
import sys
import threading
import time

try:
    import serial  # pyserial
except ImportError:  # re-exec under the esphome venv, which ships pyserial
    cands = sorted(glob.glob("/opt/homebrew/Cellar/esphome/*/libexec/bin/python"))
    if cands and os.path.realpath(sys.executable) != os.path.realpath(cands[-1]) \
            and not os.environ.get("BENCH_REEXEC"):
        os.environ["BENCH_REEXEC"] = "1"
        os.execv(cands[-1], [cands[-1]] + sys.argv)
    serial = None

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
RUNS = os.environ.get("BENCH_RUNS", os.path.join(ROOT, "runs"))
PIDFILE = os.path.join(RUNS, ".capture.pid")
SOCK = os.path.join(RUNS, ".capture.sock")
if len(SOCK) > 100:  # AF_UNIX path limit (104 on macOS)
    import hashlib
    SOCK = "/tmp/bench-capture-%s.sock" % hashlib.sha1(RUNS.encode()).hexdigest()[:10]


def host_ts():
    return datetime.datetime.now(datetime.timezone.utc).isoformat(timespec="milliseconds").replace("+00:00", "Z")


def find_port():
    p = os.environ.get("BENCH_PORT")
    if p:
        return p if os.path.exists(p) else None
    ports = sorted(glob.glob("/dev/cu.usbmodem*"))
    return ports[0] if ports else None


def append_event(run, obj):
    """Direct append (only when no daemon owns the run's events.jsonl)."""
    d = os.path.join(RUNS, run)
    os.makedirs(d, exist_ok=True)
    obj = dict(obj)
    obj.setdefault("host_ts", host_ts())
    obj["run"] = run
    with open(os.path.join(d, "events.jsonl"), "a") as f:
        f.write(json.dumps(obj, separators=(",", ":")) + "\n")


# ---------------------------------------------------------------- daemon
class Daemon:
    def __init__(self, run):
        self.run = run
        self.dir = os.path.join(RUNS, run)
        os.makedirs(self.dir, exist_ok=True)
        self.ev = open(os.path.join(self.dir, "events.jsonl"), "a", buffering=1)
        self.raw = open(os.path.join(self.dir, "raw.log"), "a", buffering=1)
        self.lock = threading.Lock()      # guards files + serial writes
        self.ser = None
        self.port = None
        self.state = "init"
        self.lines = 0
        self.last_line_mono = None
        self.last_t = None                # (device t, host monotonic) of last device line
        self.stopping = False

    # -- writing
    def _write(self, obj):
        self.ev.write(json.dumps(obj, separators=(",", ":")) + "\n")

    def bench_event(self, obj):
        obj = dict(obj)
        with self.lock:
            if "t" not in obj and self.last_t is not None:
                obj["t"] = int(self.last_t[0] + (time.monotonic() - self.last_t[1]) * 1000)
                obj["t_est"] = True
            obj["host_ts"] = host_ts()
            obj["run"] = self.run
            self._write(obj)

    def device_line(self, text):
        ts = host_ts()
        obj = None
        s = text.strip()
        if s.startswith("{"):
            try:
                obj = json.loads(s)
            except ValueError:
                obj = None
        if not isinstance(obj, dict):
            obj = {"ev": "raw", "line": text}
        obj["host_ts"] = ts
        obj["run"] = self.run
        with self.lock:
            self.raw.write(ts + " " + text + "\n")
            if isinstance(obj.get("t"), (int, float)):
                self.last_t = (obj["t"], time.monotonic())
            self._write(obj)
            self.lines += 1
            self.last_line_mono = time.monotonic()

    # -- serial
    def _open(self, port):
        s = serial.Serial()
        s.port = port
        s.baudrate = 115200
        s.timeout = 0.2
        # keep DTR/RTS deasserted: on USB-Serial-JTAG the RTS/DTR pair drives EN/BOOT
        s.dtr = False
        s.rts = False
        try:
            s.open()
        except (OSError, serial.SerialException):
            # ptys (self-test) refuse modem-control ioctls; plain open as fallback
            s = serial.Serial(port, 115200, timeout=0.2, dsrdtr=False, rtscts=False)
        return s

    def reader(self):
        buf = b""
        seen_open = False
        announced_gone = False
        while not self.stopping:
            if self.ser is None:
                port = find_port()
                if port is None:
                    if not announced_gone:
                        self.state = "gone"
                        self.bench_event({"ev": "bench.port", "state": "gone", "port": self.port})
                        announced_gone = True
                    time.sleep(0.3)
                    continue
                try:
                    ser = self._open(port)
                except Exception as e:  # node present but not openable yet
                    if not announced_gone:
                        self.state = "gone"
                        self.bench_event({"ev": "bench.port", "state": "gone", "port": port, "err": str(e)[:120]})
                        announced_gone = True
                    time.sleep(0.5)
                    continue
                with self.lock:
                    self.ser = ser
                self.port = port
                self.state = "open"
                self.bench_event({"ev": "bench.port", "state": "back" if seen_open or announced_gone else "open", "port": port})
                seen_open = True
                announced_gone = False
                buf = b""
            try:
                chunk = self.ser.read(4096)
                if not chunk and not os.path.exists(self.port):
                    raise serial.SerialException("port node vanished")
            except Exception as e:
                if buf:
                    self.device_line(buf.decode("utf-8", "replace"))
                    buf = b""
                with self.lock:
                    try:
                        self.ser.close()
                    except Exception:
                        pass
                    self.ser = None
                if not self.stopping:
                    self.state = "gone"
                    self.bench_event({"ev": "bench.port", "state": "gone", "port": self.port, "err": str(e)[:120]})
                    announced_gone = True
                time.sleep(0.3)
                continue
            if chunk:
                buf += chunk
                while b"\n" in buf:
                    line, buf = buf.split(b"\n", 1)
                    self.device_line(line.rstrip(b"\r").decode("utf-8", "replace"))

    def send(self, cmd):
        with self.lock:
            if self.ser is None:
                return {"ok": False, "err": "port gone"}
            try:
                self.ser.write((cmd + "\n").encode())
                self.ser.flush()
            except Exception as e:
                return {"ok": False, "err": str(e)[:120]}
        self.bench_event({"ev": "bench.send", "cmd": cmd})
        return {"ok": True}

    def status(self):
        age = None if self.last_line_mono is None else round(time.monotonic() - self.last_line_mono, 1)
        return {"ok": True, "running": True, "pid": os.getpid(), "run": self.run, "port": self.port,
                "state": self.state, "lines": self.lines, "last_line_age_s": age,
                "last_t": None if self.last_t is None else self.last_t[0]}

    def handle(self, conn):
        try:
            conn.settimeout(5)
            data = b""
            while not data.endswith(b"\n"):
                c = conn.recv(65536)
                if not c:
                    break
                data += c
            req = json.loads(data.decode() or "{}")
            op = req.get("op")
            if op == "send":
                rep = self.send(str(req.get("cmd", "")))
            elif op == "event":
                self.bench_event(req.get("obj") or {})
                rep = {"ok": True}
            elif op == "status":
                rep = self.status()
            elif op == "stop":
                rep = {"ok": True}
                self.stopping = True
            else:
                rep = {"ok": False, "err": "unknown op"}
            conn.sendall((json.dumps(rep) + "\n").encode())
        except Exception as e:
            try:
                conn.sendall((json.dumps({"ok": False, "err": str(e)[:120]}) + "\n").encode())
            except Exception:
                pass
        finally:
            conn.close()

    def serve(self):
        if os.path.exists(SOCK):
            os.unlink(SOCK)
        srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        srv.bind(SOCK)
        srv.listen(8)
        srv.settimeout(0.3)
        signal.signal(signal.SIGTERM, lambda *a: setattr(self, "stopping", True))
        signal.signal(signal.SIGINT, lambda *a: setattr(self, "stopping", True))
        self.bench_event({"ev": "bench.capture", "state": "start", "pid": os.getpid()})
        rt = threading.Thread(target=self.reader, daemon=True)
        rt.start()
        while not self.stopping:
            try:
                conn, _ = srv.accept()
            except socket.timeout:
                continue
            except OSError:
                continue
            self.handle(conn)
        rt.join(timeout=2)
        with self.lock:
            if self.ser is not None:
                try:
                    self.ser.close()
                except Exception:
                    pass
                self.ser = None
        self.bench_event({"ev": "bench.capture", "state": "stop", "lines": self.lines})
        srv.close()
        for p in (SOCK, PIDFILE):
            try:
                os.unlink(p)
            except FileNotFoundError:
                pass


# ---------------------------------------------------------------- client
def rpc(req, timeout=5):
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.settimeout(timeout)
    s.connect(SOCK)
    s.sendall((json.dumps(req) + "\n").encode())
    data = b""
    while not data.endswith(b"\n"):
        c = s.recv(65536)
        if not c:
            break
        data += c
    s.close()
    return json.loads(data.decode())


def read_pid():
    try:
        with open(PIDFILE) as f:
            info = json.load(f)
        os.kill(info["pid"], 0)
        return info
    except (FileNotFoundError, ValueError, KeyError, ProcessLookupError, PermissionError):
        return None


def running():
    return read_pid()


def cmd_start(run):
    info = read_pid()
    if info:
        if info.get("run") == run:
            print(json.dumps({"ok": True, "already": True, **info}))
            return 0
        print(f"capture: already running for run {info.get('run')} (pid {info['pid']}); stop first", file=sys.stderr)
        return 1
    if serial is None:
        print("capture: pyserial not available", file=sys.stderr)
        return 2
    os.makedirs(os.path.join(RUNS, run), exist_ok=True)
    err = open(os.path.join(RUNS, run, "capture.err"), "a")
    p = subprocess.Popen([sys.executable, os.path.abspath(__file__), "_daemon", "--run", run],
                         stdin=subprocess.DEVNULL, stdout=err, stderr=err, start_new_session=True)
    with open(PIDFILE, "w") as f:
        json.dump({"pid": p.pid, "run": run, "started": host_ts()}, f)
    deadline = time.time() + 5
    while time.time() < deadline:
        if p.poll() is not None:
            print(f"capture: daemon exited rc={p.returncode}; see runs/{run}/capture.err", file=sys.stderr)
            return 1
        try:
            st = rpc({"op": "status"}, timeout=1)
            print(json.dumps(st))
            return 0
        except OSError:
            time.sleep(0.1)
    print("capture: daemon did not answer within 5 s", file=sys.stderr)
    return 1


def cmd_stop():
    info = read_pid()
    if not info:
        for p in (SOCK, PIDFILE):
            if os.path.exists(p):
                os.unlink(p)
        print(json.dumps({"ok": True, "running": False}))
        return 0
    try:
        rpc({"op": "stop"}, timeout=2)
    except OSError:
        os.kill(info["pid"], signal.SIGTERM)
    deadline = time.time() + 5
    while time.time() < deadline:
        try:
            os.kill(info["pid"], 0)
        except ProcessLookupError:
            break
        time.sleep(0.1)
    else:
        os.kill(info["pid"], signal.SIGKILL)
        time.sleep(0.3)
    for p in (SOCK, PIDFILE):
        if os.path.exists(p):
            os.unlink(p)
    print(json.dumps({"ok": True, "stopped": info["pid"], "run": info.get("run")}))
    return 0


def cmd_status():
    info = read_pid()
    if not info:
        print(json.dumps({"ok": True, "running": False}))
        return 1
    try:
        print(json.dumps(rpc({"op": "status"}, timeout=2)))
        return 0
    except OSError as e:
        print(json.dumps({"ok": False, "running": True, "pid": info["pid"], "err": f"no answer: {e}"}))
        return 1


def main(argv):
    import argparse
    ap = argparse.ArgumentParser(prog="capture.py")
    ap.add_argument("op", choices=["start", "stop", "status", "send", "event", "_daemon"])
    ap.add_argument("arg", nargs="?")
    ap.add_argument("--run")
    a = ap.parse_args(argv)
    if a.op == "_daemon":
        Daemon(a.run).serve()
        return 0
    if a.op == "start":
        if not a.run:
            ap.error("start needs --run ID")
        return cmd_start(a.run)
    if a.op == "stop":
        return cmd_stop()
    if a.op == "status":
        return cmd_status()
    if a.op in ("send", "event"):
        if a.arg is None:
            ap.error(f"{a.op} needs an argument")
        if not running():
            print("capture: not running", file=sys.stderr)
            return 1
        req = {"op": "send", "cmd": a.arg} if a.op == "send" else {"op": "event", "obj": json.loads(a.arg)}
        rep = rpc(req)
        print(json.dumps(rep))
        return 0 if rep.get("ok") else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
