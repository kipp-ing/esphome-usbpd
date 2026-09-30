#!/bin/zsh
# bench/reset.sh — stop capture, esptool hard reset, restart capture
# (same run if one was being captured, else a new run "reset").
set -u
B=${0:A:h}
PY=$(ls -d /opt/homebrew/Cellar/esphome/*/libexec/bin/python 2>/dev/null | tail -1)
$B/port.sh >/dev/null || { echo "NO_BOARD"; exit 3; }
RUN=$($PY $B/capture.py status 2>/dev/null | $PY -c 'import json,sys;d=json.loads(sys.stdin.read() or "{}");print(d.get("run") or "")')
[[ -z $RUN ]] && RUN=$($PY $B/run.py _newrun reset)
$PY $B/capture.py stop >/dev/null
$PY $B/run.py _event $RUN '{"ev":"bench.reset","via":"esptool"}'
$PY $B/run.py _hardreset; rc=$?
(( rc != 0 )) && $PY $B/run.py _event $RUN "{\"ev\":\"bench.reset\",\"state\":\"error\",\"rc\":$rc}"
$PY $B/capture.py start --run $RUN >/dev/null
echo "reset: rc=$rc, capture running for $RUN"
exit $rc
