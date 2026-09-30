#!/bin/zsh
# bench/port.sh — print the board's serial port, or exit 3 with "NO_BOARD".
# Discovers fresh every call. BENCH_PORT=<path> overrides (used by self-tests with a pty).
setopt null_glob
if [[ -n "${BENCH_PORT:-}" ]]; then
  if [[ -e "$BENCH_PORT" ]]; then echo "$BENCH_PORT"; exit 0; fi
  echo "NO_BOARD"; exit 3
fi
ports=(/dev/cu.usbmodem*)
if (( ${#ports} == 0 )); then echo "NO_BOARD"; exit 3; fi
(( ${#ports} > 1 )) && echo "port.sh: warning: ${#ports} usbmodem nodes, using ${ports[1]}" >&2
echo "${ports[1]}"
