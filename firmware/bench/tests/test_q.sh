#!/bin/zsh
# Offline self-test of bench/q against bench/fixtures/runs.
set -u
B=${0:A:h:h}; export BENCH_RUNS=$B/fixtures/runs
fail=0
t() { local want="$1"; shift; local got; got=$("$@" 2>/dev/null | tr '\n' '|'); if [[ "$got" == *"$want"* ]]; then echo "ok   $*"; else echo "FAIL $*  want~[$want] got [$got]"; fail=1; fi; }
t "250  hb"                                   $B/q --count
t "261  total"                                $B/q --count
t '{"t":57010,"type":"X"}|{"t":57030,"type":"Z"}|' $B/q --type X,Z --fields t,type
t '"state":"on"'                              $B/q --state on
t 'fixture error two'                         $B/q --grep 'error two'
t '2  bench.charger'                          $B/q --ev 'bench.*' --count
t '5  total'                                  $B/q --since 57000 --until 57040 --count
t '{"t":250000,"ev":"hb","n":250}|'           $B/q --ev hb --tail 1
t 'pd.rx  X'                                  $B/q --ev pd.rx --table --fields t,ev,type
t '1  total'                                  $B/q --where n=1 --ev pd.rx --count
t '20260930T000000Z-fixture'                  $B/q runs
n=$($B/q --ev hb 2>/dev/null | wc -l | tr -d ' '); [[ $n == 200 ]] && echo "ok   cap 200" || { echo "FAIL cap: $n"; fail=1; }
n=$($B/q --ev hb --all | wc -l | tr -d ' '); [[ $n == 250 ]] && echo "ok   --all 250" || { echo "FAIL --all: $n"; fail=1; }
$B/q --run nope >/dev/null 2>&1 && { echo "FAIL unknown run accepted"; fail=1; } || echo "ok   unknown run rejected"
(( fail )) && echo "RESULT: FAIL" || echo "RESULT: PASS"
exit $fail
