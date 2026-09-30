# usbPD — status (2026-09-30, midday)

Evening 2026-09-30: board on USB (`/dev/cu.usbmodem1101`), mr-bench ran the checklist.
**M0 done. M4 BLOCKED by a firmware crash** (stack overflow in the `main` task on the first RX frame);
fix belongs to mr-engineer, see "Open failure" below. Charger left **OFF** (fw boot-loops on attach).

## Milestones
| M | What | State |
|---|---|---|
| M0 | Rig: port, hello flash, capture, charger off/on, `bench/q` on real data | **done**: hello `runs/20260930T180449Z-flash` (build-id in hb, capture reopen no reset: hb n 10→14, `help` echo, esptool reset → n=1), replug `runs/20260930T180805Z-replug` (charger off/on rc=0) |
| M1 | Knowledge base `docs/knowledge/` (datasheet + Linux fusb302 + PD spec, cited) | done |
| M2 | Spec `docs/spec.md`: 16 states, 85 table rows, timers, codec vectors, §7 log contract, §9 bench acceptance, §10 open questions | done (+ Q1–Q8 resolved, `docs/spec-questions.md`) |
| M3 | Firmware implements spec; host sim green | done: 47/47 runs (burst + single-read poll), codec 73 checks; fw builds, no warnings |
| M4 | RX alive on board: first SRC_CAPS decoded | **FAIL (fw crash)**: attach reaches WaitCaps, then panic in `log_rx` before any `pd.rx`/`pd.caps` line. `runs/20260930T181143Z-replug`, `runs/20260930T180901Z-flash` |
| M5 | 5 V contract, 2× over charger replug | blocked by M4 |
| M6 | Requested voltage (`pd want 9000`): Accept + PS_RDY | blocked by M4 |

## Open failure (for mr-engineer) — fw `be5ccf1`, build `be5ccf1-0930T180901`
- Every attach: `vbus on` → AttachWait → (100 ms) Attached → `cc` → Startup → Discovery → WaitCaps, then
  `Guru Meditation Error: Stack protection fault` in task `main`; reboot (`boot reset_reason=panic`), loops while charger is on.
- Decoded (bench/panic.py, fw ELF of that build): PC `log_rx` fw/src/pd_drv.c:102, RA `rx_one` fw/src/pd_drv.c:195;
  stack frames `fusb302_fifo_read` fusb302.c:72 → `fusb302_read_burst` :54 → `i2c_master_transmit_receive`, `p_frd` main.c:43.
  Main task stack bounds 0x408119cc–0x408129c0 (`CONFIG_ESP_MAIN_TASK_STACK_SIZE=3584`). Some dumps are a follow-on
  fault in `_interrupt_handler` (vectors.S:457, ISR stack 0x4080eed0–0x4080f4d0).
- Console `regs` also panics (stack protection, PC 0x40035bc8 / RA 0x400316c6 in ROM): `runs/20260930T181101Z-cmd`.
- Queries:
  - `bench/q --run 181143Z-replug --ev bench.panic --fields n,why,pc_fn,pc_src,ra_fn,ra_src,stack --table`
  - `bench/q --run 181143Z-replug --ev bench.panic --full --tail 1` (frames list)
  - `bench/q --run 181143Z-replug --no-raw --since 67000 --until 68000 --table` (attach → WaitCaps → crash)
  - `bench/q --run 181143Z-replug --grep 'Guru|Detected in task|MEPC' --fields line --full --all`
  - `bench/q --run 181101Z-cmd --ev bench.panic --full` (regs crash)
- Next bench step once fixed: `bench/flash.sh` then continue at checklist item 5.

## Bench notes on spec §10 (from the runs above)
- Q5 (delay after SW_RES): `fusb.id ok id=0x81 ver=8 rev=1` on all 11+22 boots with no extra delay → default holds. `regs` dump not yet available (crashes).
- Q6 (CC debounce/levels): 33 attaches, every `cc` line `cc=2 lvl1=0 lvl2=3`, no `cc.retry`, Attached exactly 100 ms after `vbus on` → default holds for this charger.
- Q13 (attach gated on VBUS): `vbus on=true` precedes `cc` on every attach; attach works on every cycle → confirmed.
- Q17 (poll ≤ 2 ms): idle Unattached hb `poll_max_us` 252–376; one Discovery hb 1752. Attached steady state not reached yet → open.
- Q1–Q4, Q7–Q12, Q14–Q16: need RX, still open.

## Bench checklist (mr-bench, in order) — items 1–4 done 2026-09-30; 5 flashed but crashes on attach
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
