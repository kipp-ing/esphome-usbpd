#!/bin/zsh
# bench/charger.sh on|off — switch the 100 W PD charger (smart plug) via mission-control's
# charger.sh; every call is appended to runs/charger.log. Fresh PD attach needs off >= 25 s.
set -u
here=${0:A:h}; root=${here:h}
MC=/Users/jan/dev/repos/mission-control/bin/charger.sh
case "${1:-}" in on|off) ;; *) echo "usage: bench/charger.sh on|off" >&2; exit 2;; esac
mkdir -p "$root/runs"
out=$("$MC" "$1" 2>&1); rc=$?
ts=$(python3 -c 'import datetime;print(datetime.datetime.now(datetime.timezone.utc).isoformat(timespec="milliseconds"))')
echo "$ts charger $1 rc=$rc ${out//$'\n'/ | }" >> "$root/runs/charger.log"
echo "$out"
exit $rc
