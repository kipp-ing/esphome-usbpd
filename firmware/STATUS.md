# usbPD — status (2026-09-30, evening)

Evening 2026-09-30 (20:30 local): fw `2f1a9d0` (build `2f1a9d0-0930T181814`) on the board.
**M0, M4, M5, M6 all PASS** on the bench. Stack fix verified (no panic, `stack_free` 6676 of 8192 min).
Charger left **ON**, board in Ready with a 5 V contract (`want` 5000).

## Milestones
| M | What | State |
|---|---|---|
| M0 | Rig: port, hello flash, capture, charger off/on, `bench/q` on real data | **done**: hello `runs/20260930T180449Z-flash` (build-id in hb, capture reopen no reset: hb n 10→14, `help` echo, esptool reset → n=1), replug `runs/20260930T180805Z-replug` (charger off/on rc=0) |
| M1 | Knowledge base `docs/knowledge/` (datasheet + Linux fusb302 + PD spec, cited) | done |
| M2 | Spec `docs/spec.md`: 16 states, 85 table rows, timers, codec vectors, §7 log contract, §9 bench acceptance, §10 open questions | done (+ Q1–Q8 resolved, `docs/spec-questions.md`) |
| M3 | Firmware implements spec; host sim green | done: 47/47 runs (burst + single-read poll), codec 73 checks; fw builds, no warnings |
| M4 | RX alive on board: first SRC_CAPS decoded | **PASS** `runs/20260930T181925Z-replug`: Source_Capabilities rev 1, 4 fixed PDOs 5 V/3 A, 9 V/3 A, 15 V/3 A, 20 V/4.7 A; pd.dup 0, pd.rxraw 0, sm.unhandled 0, no bench.panic. Flash + `regs` check: `runs/20260930T181814Z-flash` (boot `stack=8192`, `fusb.id ok 0x81`, `regs` no panic) |
| M5 | 5 V contract, 2× over charger replug | **PASS** `runs/20260930T182221Z-replug` (2 cycles): 2× `contract mv=5000 pos=1`, Request 2 ms after caps, txres `sent` ×2, pd.hr/pd.sr 0, 2× `contract.lost why=detach`, last hb Ready mv=5000. (Also first attach of M4 run reached 5 V contract.) |
| M6 | Requested voltage (`pd want 9000`): Accept + PS_RDY | **PASS** a) in Ready `runs/20260930T182454Z-cmd`: sel pos 2 `why=exact`, Request `0x2004B12C`, Accept +3 ms, PS_RDY +83 ms, contract 9000 held 70 s, no HR. b) want given with charger off, fresh attach `runs/20260930T182623Z-replug`: contract 9000 pos 2, held 70 s. Back down `runs/20260930T182817Z-cmd` → contract 5000. Q9 extra: want 12000 `runs/20260930T182857Z-cmd` → `why=below` 9000, Accepted; back to 5000 `runs/20260930T182912Z-cmd` |

## Resolved failure (history) — fw `be5ccf1`, build `be5ccf1-0930T180901`
**Closed 2026-09-30 by fw `2f1a9d0`**: bench verified boot `stack=8192`, hb `stack_free` min 6676 (after attach → caps → Request → Accept → PS_RDY, 9 V switch and `regs`), no `log_pool_miss`, 0 panics in all runs from `20260930T181814Z-flash` on.
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
- **Fix (mr-engineer, fw `f53c73a` + `2f1a9d0`) — ready for bench.** Root cause: `log_line_t` embedded a 768-B
  line buffer, so every logging frame put ~780 B on the stack; the RX path (`pd_drv_poll` → `rx_one` [several
  log lines] → `log_rx`/`store_caps` + newlib snprintf) and `regs` (704-B json + 2 log lines) blew the 3584-B
  main stack. Changes: line buffers now come from a 4-slot static pool in log.c (handle is ~32 B; exhaustion →
  line dropped + counted), `regs` json buffer static, main task stack 8192. Largest fw frame now 160 B
  (rx_one/console_exec, objdump); 9 V Request path (`drv_tx` → `fusb302_write_burst`) ≤ 96 B/frame. Host
  tests green incl. new pool test and "no pool miss/leak over all 47 scenarios".
- **Bench should verify:** `boot.stack` = 8192; `hb.stack_free` (min free bytes since boot) stays well above
  ~1000 through attach → SRC_CAPS → Request → Accept → PS_RDY and after a `regs` command; no
  `hb.log_pool_miss` field (present only if non-zero); `pd.rx` + `pd.caps` lines now appear; no `bench.panic`.

## Bench notes on spec §10 (runs 20260930T181814Z-flash … 20260930T182912Z-cmd, fw 2f1a9d0)
Settled by the data (all defaults hold):
- Q1 masks 0x00: RX works; `regs` shows 0x0A/0x0E/0x0F = 00 (`runs/20260930T182832Z-cmd`, in Ready). **Settled.**
- Q2 TX success: 7/7 `pd.txres res=sent`, each with a matching `pd.gcrc match=true`; `tmo` 0. **Settled.**
- Q3 Power: `regs` 0x0B = 0x0F and RX works. **Settled (0x0F).**
- Q4 VBUSOK: exactly one `vbus` on per attach and one off per detach, no chatter. Note: `vbus on=false` shows 9–12 s after `charger off` (charger output decays slowly), `on=true` ~0.7 s after `charger on`. **Settled (no debounce needed).**
- Q5 SW_RES delay: `fusb.id ok 0x81` every boot; `regs` values match. **Settled.**
- Q6 CC: every attach `cc=2 lvl1=0 lvl2=3`, no `cc.retry`, Attached 100 ms after `vbus on`. **Settled for this charger.**
- Q9 CapabilityMismatch=0: want 12000 → sel 9000 `why=below`, Accepted (`runs/20260930T182857Z-cmd`). **Settled.**
- Q10 collisions: `res=coll` 0. **Settled (none seen).**
- Q11 own GoodCRC in FIFO: `pd.gcrc match=false` 0. **Settled (never visible).**
- Q13 VBUS-gated attach: vbus on precedes cc on every attach (5/5 tonight). **Settled.**
- Q14 SpecRev: charger answers rev 1 (R2.0) on Source_Capabilities, Accept, PS_RDY. **Settled.**
- Q16 burst poll: M4–M6 pass with the burst, no missed TX results. **Settled (burst on).**
- Q17 poll time: steady state `poll_max_us` 247 (Ready/Unattached); VDM RX steps ~1040; the caps+Request step 2304–2522 µs → exactly 1 `poll.slow` per attach (allowed ≤ 1). **Settled** (caps step slightly > 2 ms, within the allowance).
Still open (no HR/SR happened on the happy path): Q7 (SEND_HARD_RESET self-clear), Q8 (hr_count), Q12 (SR/PD_RESET), Q15 (HR recovery timers).
Observation: after each contract the charger sends 10 structured VDMs `0xFF008001` (Discover Identity, SVID 0xFF00), ~133 ms apart; fw logs `cls=ignored`, the charger stops after 10 and no reset follows.

## Bench checklist (mr-bench, in order) — all items done 2026-09-30 (7: see §10 notes; Q7/Q8/Q12/Q15 need an HR/SR run)
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
