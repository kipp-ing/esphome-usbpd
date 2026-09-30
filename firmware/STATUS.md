# usbPD — status (2026-09-30, midday)

Board not reachable today (Mac sees no USB device at all) → everything below is host-side only.
Next session: operator connects USB, then mr-bench starts at "Bench checklist".

## Milestones
| M | What | State |
|---|---|---|
| M0 | Rig: port, hello flash, capture, charger off/on, `bench/q` on real data | **pending board** (all tooling self-tested offline vs. pty fake board) |
| M1 | Knowledge base `docs/knowledge/` (datasheet + Linux fusb302 + PD spec, cited) | done |
| M2 | Spec `docs/spec.md`: 16 states, 85 table rows, timers, codec vectors, §7 log contract, §9 bench acceptance, §10 open questions | done (+ Q1–Q8 resolved, `docs/spec-questions.md`) |
| M3 | Firmware implements spec; host sim green | done: 47/47 runs (burst + single-read poll), codec 73 checks; fw builds, no warnings |
| M4 | RX alive on board: first SRC_CAPS decoded | pending board |
| M5 | 5 V contract, 2× over charger replug | pending board |
| M6 | Requested voltage (`pd want 9000`): Accept + PS_RDY | pending board |

## Bench checklist (mr-bench, in order)
1. `bench/port.sh` → port present.
2. `bench/flash.sh --dir bench/hello` → build-id seen (boot or hb line); capture opening does not reset the chip.
3. `bench/capture.py send help` echo; `bench/reset.sh` → reconnect.
4. `bench/run.py replug` with hello → charger events in events.jsonl.
5. `bench/flash.sh` (fw/) → `regs`, `fusb.id`, hb with `poll_max_us` (spec Q17: ≤ 2000).
6. `bench/run.py replug` → check spec §9 M4, then M5; then `cmd "pd want 9000"` + replug → M6.
7. Open questions §10 (Power 0x0F vs 0x07, VBUSOK threshold, I_TXSENT vs GoodCRC, burst A/B Q16, …) settled from run data.

## Roles / how to resume
See `AGENTS.md`. Only mr-bench touches hardware; mr-engineer reads runs via `bench/q`.
Host tests: `make -C test/host test`. Build: `BUILD_ID=x <esphome-venv-python> -m platformio run -d fw`.
