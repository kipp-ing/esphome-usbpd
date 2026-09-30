# bench/ — rig tooling (owner: mr-bench)

Only mr-bench runs the hardware commands (flash, reset, charger, capture). Everyone reads data
with `bench/q`. Python: the esphome venv (`/opt/homebrew/Cellar/esphome/*/libexec/bin/python`,
has pyserial, esptool 5.x, PlatformIO 6.1); scripts find it themselves.

## Commands
| command | what |
|---|---|
| `bench/port.sh` | prints `/dev/cu.usbmodem*` or `NO_BOARD` (exit 3). Discovers fresh every call. |
| `bench/charger.sh on\|off` | smart plug via mission-control's charger.sh; appends `runs/charger.log`. Fresh PD attach: off >= 25 s. |
| `bench/capture.py start --run ID` | daemon, single owner of the port -> `runs/ID/raw.log`, `runs/ID/events.jsonl` |
| `bench/capture.py stop \| status` | stop (port free afterwards) / one-line JSON status |
| `bench/capture.py send "regs"` | forward a console line to the board through the daemon |
| `bench/capture.py event '{"ev":"bench.x",...}'` | append a bench event through the daemon |
| `bench/flash.sh [--dir fw] [--build-only] [--no-verify]` | stop capture -> build -> esptool write-flash `firmware.factory.bin`@0x0 (retry once) -> capture new run -> verify boot build-id. Exit 0 ok, 1 build/flash fail, 3 NO_BOARD, 4 no build line, 5 mismatch. Capture keeps running. |
| `bench/reset.sh` | stop capture, esptool hard reset, restart capture (same run if one was active, else new run `reset`) |
| `bench/run.py boot [--secs 10]` | new run: esptool hard reset, capture N s |
| `bench/run.py replug [--off 25] [--secs 45] [--cycles N] ["<cmd>"]` | new run: capture, then N x (charger off (>= 25 s enforced) [-> send `<cmd>` while off] -> on -> capture N s) |
| `bench/run.py cmd "pd want 9000" [--secs 5]` | new run: send console line, capture N s |
| `bench/panic.py RUN [--elf P]` | decode ESP-IDF panic dumps (addr2line vs fw ELF) -> `bench.panic` events (`pc_fn/pc_src/ra_fn/ra_src/stack/frames`) |
| `bench/q ...` | query (below) |
| `bench/tests/run_all.sh` | offline self-tests (no board, no charger) |

`run.py` scenarios stop capture at the end (`--keep` leaves it running), write
`runs/<id>/meta.json` (run, scenario, created/ended, fw_git_sha, build_id = last flashed,
observed_build, rc, args), point `runs/last` at the run, and print a 5-line summary
(event counts, boot/build, first/last error). Run id = `<UTC YYYYmmddTHHMMSSZ>-<scenario>`.
Without a board every hardware command exits 3 `NO_BOARD` before touching anything
(the charger is never switched then).

## events.jsonl
One JSON object per line. Device JSON lines are kept verbatim plus `host_ts` (UTC ISO ms)
and `run`. Non-JSON lines (ROM/bootloader, panics) -> `{"ev":"raw","line":...}` (raw.log keeps
everything with a host timestamp; raw.log is gitignored, events.jsonl is committed).
Bench events (no device `t`; `t` is estimated from the last device line, marked `"t_est":true`):
- `bench.capture` state start|stop · `bench.port` state open|gone|back (USB re-enumeration)
- `bench.charger` state off|on|error (+rc) · `bench.send` cmd · `bench.reset` via esptool

## bench/q (read-only)
```
bench/q runs                                   # list runs: scenario, build, #events, #err-ish
bench/q                                        # last run, compact JSON (no run/host_ts), max 200 lines
bench/q --run 20260930T1012 --ev sm,i2c.err    # run by id or unique substring; ev list, 'bench.*' wildcard
bench/q --type X,Y --state on --where rc=0     # match fields type / state / any key=value
bench/q --since 30000 --until 32000 --table    # window on device t (ms), aligned table
bench/q --grep 'Guru|abort' --no-raw --tail 20 --fields t,ev,msg --count --full --all
```

## Build-id contract (fw <-> bench)
- flash.sh sets env `BUILD_ID` (default `<git sha of project dir>[-d if dirty]-<MMDDTHHMMSS UTC>`,
  chars `[A-Za-z0-9._:+-]`, <= 48) and builds with `python -m platformio run -d <dir>`.
- The firmware bakes it in as the C string macro `BUILD_ID` and prints it in the boot line
  `{"t":..,"ev":"boot","build":"<BUILD_ID>",...}`. flash.sh checks the string is in
  `firmware.bin` before flashing and after flashing waits (15 s) for an event whose `"build"`
  equals it; if none shows, it does one hard reset and looks again.
- Two ways to bake it in (both fine for flash.sh):
  - fw/ today: `fw/build_id.py` pre-script -> `-DBUILD_ID="..."`. A changed define rebuilds all
    of ESP-IDF (~65 s per flash).
  - bench/hello: `extra_scripts = pre:../build_id.py` (bench/build_id.py) writes
    `src/build_id.h` (`#define BUILD_ID "..."`, gitignored) only when the id changed; include it.
    Rebuild on a new id: ~5 s.
- Recommended for fw: also carry `"build"` in `hb` (or print the boot line only after
  `usb_serial_jtag_is_connected()`), because a line printed while the host has the port closed
  (right after flash/reset, before capture reopens) can be dropped.

## Serial port notes
- USB-Serial-JTAG maps RTS/DTR to chip reset (RTS=1 with DTR=0 resets). capture.py opens the
  port and deasserts RTS before DTR so opening never passes through the reset state.
- One owner at a time: flash/reset/boot stop capture first. `BENCH_PORT=<path>` overrides
  discovery and `BENCH_RUNS=<dir>` the runs dir (self-tests use a pty and a temp dir).

## Rig probe
`bench/hello/` — minimal ESP-IDF app: boot line with build-id, `{"ev":"hb","n":..,"build":..}`
every 1 s, echoes console lines as `{"ev":"cmd","line":..}`. `bench/flash.sh --dir bench/hello`.
