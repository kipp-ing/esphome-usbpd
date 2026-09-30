#!/bin/zsh
# Offline bench self-tests (no board, no charger). ~45 s.
B=${0:A:h:h}; PY=$(ls -d /opt/homebrew/Cellar/esphome/*/libexec/bin/python | tail -1); rc=0
echo "== port.sh NO_BOARD";  BENCH_PORT=/dev/cu.none $B/port.sh; [[ $? == 3 ]] && echo "ok" || rc=1
echo "== flash.sh NO_BOARD"; BENCH_PORT=/dev/cu.none $B/flash.sh --dir $B/hello | tail -1; [[ ${pipestatus[1]} == 3 ]] && echo "ok" || rc=1
echo "== q";                 $B/tests/test_q.sh | tail -1 | grep -q PASS && echo ok || rc=1
echo "== capture (pty)";     $PY -u $B/tests/test_capture_pty.py | tail -1 | grep -q PASS && echo ok || rc=1
echo "== run.py cmd+replug (pty, fake charger)"; $PY -u $B/tests/test_run_pty.py | tail -1 | grep -q PASS && echo ok || rc=1
(( rc )) && echo "SELFTEST: FAIL" || echo "SELFTEST: PASS"; exit $rc
