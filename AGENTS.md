# usbPD — FUSB302 USB-PD sink on ESP32-C6 (direct build, Claude + 4 role agents)

Goal: the board requests a voltage (e.g. 9 V fixed PDO) and the source answers Accept → PS_RDY,
proven by bench data in `runs/`.

Layout: code paths below (`fw/`, `bench/`, `test/host/`, `runs/`, `STATUS.md`) live under
`firmware/`; `docs/` is at the top level. The docs use paths relative to `firmware/`.

## Roles
| Role | Owns | May NOT |
|---|---|---|
| mr-knowitall | `docs/knowledge/` — facts with source refs (file:line / spec §) | write firmware, touch the rig |
| mr-spec | `docs/spec.md` — state machine, transition table, timers, test cases | write firmware, touch the rig |
| mr-engineer | `fw/`, `test/host/` — table-driven state machine (mandatory) + host sim | flash, reset, open the serial port, switch the charger |
| mr-bench | `bench/`, `runs/` — flash, reset, charger, capture, `bench/q` | change `fw/` logic |

Only mr-bench touches hardware. Engineer reads board data only via `bench/q`.

## Rig (hard rules)
- Board: ESP32-C6, USB-Serial-JTAG `/dev/cu.usbmodem*` (discover fresh). FUSB302 @ I2C 0x22,
  SDA=GPIO21, SCL=GPIO22, no INT line (poll). LED GPIO8.
- The Mac USB cable stays plugged. Charger (100 W, PD source) only via `bench/charger.sh on|off`
  (wraps `~/dev/repos/mission-control/bin/charger.sh`). Fresh attach needs off ≥ 25 s.
- One owner of the serial port at a time: stop capture before flash/reset.
- Board node gone and no USB event → stop, leave charger ON, report.
- Never touch `~/dev/repos/PCB-pi-r0` except reading (spec texts, datasheet).

## Sources
See `docs/sources.md`. The spec texts and the datasheet are not in this repository; locally they
were read from a private copy (`~/dev/repos/PCB-pi-r0`).

## Process
Commit after every step (small commits beat lost work). Firmware logs JSONL, one object per line,
with `t` (ms) and `ev`; boot line carries the build-id.
