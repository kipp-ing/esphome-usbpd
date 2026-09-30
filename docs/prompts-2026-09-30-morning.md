# Morning agent briefs, 2026-09-30 (verbatim)

These are the briefs the orchestrator (Claude, main session) sent to the role agents in phase 1,
before the board was available. Times are UTC (local time = UTC + 2). Paths refer to the operator's
machine; `PCB-pi-r0` and `mission-control` are private repositories (see `sources.md`).

`Agent` = a new subagent was started with this brief. `SendMessage <id>` = a follow-up message to an
already running agent (`a99f…` is mr-engineer, `a3c5…` is mr-spec).

The evening briefs are in [`report-2026-09-30.md`](report-2026-09-30.md).

```text
--- 2026-09-30T08:34:31.326Z Agent Map rig: board, flash, charger, capture
Medium thoroughness. In /Users/jan/dev/repos/mission-control read START-HERE.md and the relevant rig docs, then locate the Pi repo it references (likely something like ../PCB-pi-r0 or similar under /Users/jan/dev/repos). READ ONLY, never modify anything, never cd into the Pi repo with side effects.

Report concisely:
1. Absolute path of the Pi repo and the mission-control repo purpose.
2. Hardware: which MCU/board (ESP32 variant?), which USB-PD chip (FUSB302?), how it's connected (I2C pins, INT pin), serial port device name(s) used.
3. Toolchain: how firmware is built and flashed (ESP-IDF/PlatformIO/Arduino? exact commands/scripts, paths), how hard reset is done (esptool commands), how serial logs are captured (scripts, file locations, format).
4. Charger control: bin/charger.sh — full usage, what it calls (webhook URL shape — don't copy secrets, just say where they live), on/off semantics, timings.
5. Spec sources: path of FUSB302 datasheet text and USB-PD spec text files; any other reference docs (pd-ledger, state-machine docs).
6. Goal definition: what "success" means (requested voltage, e.g. 9V/12V/20V? how verified — e.g. a measurement, ADC, log line?).
7. Any safety rules for the rig (cable must stay, charger cycling limits, etc.).
Give file paths for everything.
--- 2026-09-30T08:39:11.308Z Agent mr-knowitall: build knowledge base
You are **mr-knowitall** in the repo /Users/jan/dev/repos/usbPD (read AGENTS.md first). Project: USB-PD sink firmware for FUSB302 (I2C 0x22) on ESP32-C6, polling, no INT line. Goal: request a fixed PDO > 5 V (e.g. 9 V), get Accept → PS_RDY. Your job: build `docs/knowledge/` — precise, implementation-ready facts, EVERY fact with a source reference (file:line or spec section/table number). You do NOT write firmware, you do NOT touch hardware.

Sources (read-only, never modify that repo):
- FUSB302 datasheet full text: /Users/jan/dev/repos/PCB-pi-r0/tmp/pdfs/fusb302mpx.txt (also .tmp-fusb302-full.txt in that repo root)
- USB-PD: /Users/jan/dev/repos/PCB-pi-r0/docs/spec/usb-pd/ (pd-sink-essentials.txt, usb-pd-spec-r2.0-v1.3.txt, usb-pd-spec-r3.2-v1.2.txt, outline-r2.txt). Use grep/sed -n with line ranges; the files are large.
- If a gap remains, fetch online: Linux kernel drivers/usb/typec/tcpm/fusb302.c + fusb302_reg.h (a known-working driver — great for the exact init/RX sequence), onsemi app notes. Cite URL + line.

Context: a previous attempt (other agent, PCB-pi-r0) never decoded a single RX frame: frames=0, no SRC_CAPS, even though the comparator saw CC activity. So the RX path is priority #1. Do NOT read that previous firmware for answers — it is known-broken; derive from datasheet + Linux driver.

Files to write (markdown, dense, tables welcome):
1. `docs/knowledge/fusb302-registers.md` — full register map (addr, bits, reset value, meaning) for all registers a sink needs.
2. `docs/knowledge/fusb302-sink-init.md` — THE exact step-by-step sequence: reset (SW_RES, PD_RESET), Power (PWR bits), Switches0 (PDWN1/2, MEAS_CC1/2), CC orientation detection as sink (measure BC_LVL on each CC with MEAS_CC), Switches1 (TXCC1/2, AUTO_CRC, SPECREV, POWERROLE/DATAROLE for sink/UFP), Control0-3 (AUTO_PRE, N_RETRIES, AUTO_RETRY, flush RX/TX), Mask/Maska/Maskb, how to enable PD RX, FIFO flush. Include what Linux does in which order, and any timing delays. Mark pitfalls that would lead to "no frames received" explicitly (e.g. wrong CC selected for MEAS/TX, AUTO_CRC off → source retries & hard resets, RX flush, PWR not full, SPECREV).
3. `docs/knowledge/fusb302-fifo.md` — RX FIFO token format (SOP tokens 0xE0 etc., reading header + data objects + 4 CRC bytes), how to detect RX empty (STATUS1 RX_EMPTY), GoodCRC handling (auto), TX FIFO token sequence for sending a Request (SOP1,SOP1,SOP1,SOP2, PACKSYM|len, data, JAM_CRC, EOP, TXOFF, TXON), I_TXSENT/I_RETRYFAIL/I_GCRCSENT, hard reset send/receive (Control3 SEND_HARD_RESET, I_HARDRST).
4. `docs/knowledge/pd-messages.md` — message header bit layout (R2.0 and R3.x differences, spec revision field, MessageID counter rules), control message types (GoodCRC, Accept, Reject, PS_RDY, Get_Source_Cap, Soft_Reset...), data message types (Source_Capabilities, Request), Fixed/Variable/Battery/APDO PDO bit layout, Fixed RDO bit layout (object position, operating/max current, capability mismatch, no USB suspend, USB comm capable), worked byte example of a 9V/3A Request.
5. `docs/knowledge/pd-sink-policy.md` — sink policy-engine states (PE_SNK_Startup, Discovery, Wait_for_Capabilities, Evaluate_Capability, Select_Capability, Transition_Sink, Ready, Hard_Reset, Transition_to_default, Soft_Reset) with transitions and all relevant timers with values (tTypeCSinkWaitCap, tSenderResponse, tPSTransition, tPSHardReset, tNoResponse, nHardResetCount, nCapsCount...), protocol layer rules for sink (MessageID, retries, GoodCRC within tReceive, what happens on repeated Source_Capabilities), and Type-C sink attach (Rd, vRd levels via BC_LVL, tCCDebounce).
6. `docs/knowledge/README.md` — index + a "top 10 things that must be right for RX to work" list.

Commit after each file (git -C /Users/jan/dev/repos/usbPD add ... && commit, message ends with a blank line then `Claude-Session: https://claude.ai/code/session_01GYKmvnfRn7dzPjksUB77xK`). Work in English. When done, reply with a short summary (≤15 lines): files, the top-10 RX list, and any open uncertainties. Other roles may later ask you questions.
--- 2026-09-30T08:39:31.409Z Agent mr-bench: build bench tooling
You are **mr-bench** in /Users/jan/dev/repos/usbPD (read AGENTS.md first). You are the ONLY role allowed to flash, reset, switch the charger and open the serial port. Right now the board is NOT enumerated on USB (no /dev/cu.usbmodem*) — the operator is being asked to replug. So: build and self-test all tooling WITHOUT the board now; do NOT call the charger yet.

Rig facts:
- ESP32-C6 devkit (esp32-c6-devkitc-1), USB-Serial-JTAG, port /dev/cu.usbmodem* (discover fresh every time).
- No ESP-IDF install. Use PlatformIO from ESPHome's venv: /opt/homebrew/Cellar/esphome/2026.8.0/libexec/bin/python -m platformio (packages in ~/.platformio/packages: framework-espidf, toolchain-riscv32-esp, tool-esptoolpy). esptool: same python -m esptool (check which works).
- Hard reset reference: /Users/jan/dev/repos/mission-control/bin/board-reset.sh (read it). Uses esptool --chip esp32c6 --after hard-reset chip-id.
- Charger: /Users/jan/dev/repos/mission-control/bin/charger.sh on|off (smart plug, ~1 s). Fresh PD attach needs off ≥ 25 s; attach ~1 s after on; source sends its own hard reset ~32 s after attach if no contract.
- Firmware project will be in fw/ (PlatformIO, framework=espidf) by mr-engineer. It logs JSONL on the USB serial console: one JSON object per line with at least "t" (ms since boot) and "ev"; boot line {"ev":"boot","build":"<id>",...}. Non-JSON lines (IDF boot ROM noise) must still be captured as {"ev":"raw","line":...}. The firmware also accepts line commands on the serial console (e.g. "regs", "pd want 9000").

Build in bench/ (python3 stdlib + pyserial from the esphome venv if needed; keep it simple, robust):
1. `bench/port.sh` — prints the port or exits 3 "NO_BOARD".
2. `bench/charger.sh on|off` — wraps mission-control's charger.sh, appends to runs/charger.log.
3. `bench/capture.py start|stop|status --run <id>` — daemon that owns the port, writes runs/<id>/raw.log and runs/<id>/events.jsonl where every line is enriched with "host_ts" (ISO ms) and "run"; survives USB re-enumeration (reconnect loop, log {"ev":"bench.port","state":"gone|back"}); pid file; also `bench/capture.py send "<cmd>"` to forward a console command to the board (via a small FIFO/socket to the daemon, so the port stays single-owner).
4. `bench/flash.sh` — stop capture, build fw/ (pio run -d fw), flash (pio run -t upload or esptool write_flash), start capture for a new run, wait for the boot line and verify build-id matches the built one; retry flash once on failure. Build-id: pass via build flag (coordinate: env var BUILD_ID → -DBUILD_ID=\"...\" in fw/platformio.ini build_flags; mr-engineer will read BUILD_ID macro). Document the contract in bench/README.md.
5. `bench/reset.sh` — stop capture, esptool hard reset, restart capture.
6. `bench/run.py <scenario>` — scenarios: `boot` (reset + capture 10 s), `replug` (charger off 25 s → on → capture 45 s, events for charger actions logged into events.jsonl as {"ev":"bench.charger","state":...}), `cmd "<console cmd>"` (send + capture N s). Writes runs/<id>/meta.json (run id = UTC timestamp + scenario, git sha of fw, build id, scenario). Updates runs/last symlink. Prints run id + 5-line summary (count per ev, first/last error).
7. `bench/q` — read-only query tool for mr-engineer: `bench/q [--run ID|last] [--ev a,b] [--type X,Y] [--state S] [--since ms] [--until ms] [--grep RE] [--fields t,ev,...] [--count] [--table]` plus `bench/q runs` listing runs. Fast, filterable, no raw dumps by default (cap output at 200 lines unless --all).
8. `bench/README.md` — usage.

Self-test everything that doesn't need the board: q against a synthetic events.jsonl (put a fixture in bench/fixtures/), port.sh returns NO_BOARD, capture daemon start/stop against a pty (python pty) if feasible, flash.sh fails cleanly with NO_BOARD. Verify pio can build a trivial esp32-c6 espidf project (put a tiny hello firmware in bench/hello/ that prints the JSONL boot line with BUILD_ID and a heartbeat {"ev":"hb"} every 1 s; this is your rig probe) — the first build downloads nothing hopefully; if it needs downloads, allow it.

Commit after each working piece (git -C /Users/jan/dev/repos/usbPD; message ends with blank line + `Claude-Session: https://claude.ai/code/session_01GYKmvnfRn7dzPjksUB77xK`). Never modify /Users/jan/dev/repos/PCB-pi-r0 or mission-control. Reply with a ≤15-line summary: commands, what was self-tested, what's pending on the board.
--- 2026-09-30T08:39:47.586Z Agent mr-engineer: firmware skeleton
You are **mr-engineer** in /Users/jan/dev/repos/usbPD (read AGENTS.md first). You write firmware and the host sim. You NEVER flash, reset, open the serial port or switch the charger — that's mr-bench. The PD knowledge base (docs/knowledge/, by mr-knowitall) and the state-machine spec (docs/spec.md, by mr-spec) are being written right now; do NOT implement PD protocol logic yet. Phase 1 = the infrastructure the protocol will plug into:

Target: ESP32-C6 (esp32-c6-devkitc-1), ESP-IDF via PlatformIO (ESPHome venv: /opt/homebrew/Cellar/esphome/2026.8.0/libexec/bin/python -m platformio; packages in ~/.platformio/packages). FUSB302 at I2C 7-bit 0x22, SDA=GPIO21, SCL=GPIO22, no INT line (poll), WS2812 LED GPIO8 (optional). Console = USB-Serial-JTAG.

Build:
1. `fw/platformio.ini` (env esp32c6, framework espidf). Build-id contract with mr-bench: env var BUILD_ID → build_flags -DBUILD_ID=\"...\" (use an extra_script or `${sysenv.BUILD_ID}`; default "dev" when unset). Read bench/README.md if it exists already; coordinate by writing the contract into fw/README.md.
2. `fw/src/log.{c,h}` — JSONL logger: one object per line, always "t" (ms since boot) and "ev". Helpers for ints, strings, hex byte arrays. Thread-safe enough for a single poll task. Boot line {"t":..,"ev":"boot","build":BUILD_ID,"reset_reason":..}. Heartbeat {"ev":"hb","state":"<sm state>"} every 1 s.
3. `fw/src/fusb302.{c,h}` — thin I2C HAL only: read/write reg, read/write burst (FIFO), register address #defines from the datasheet (/Users/jan/dev/repos/PCB-pi-r0/tmp/pdfs/fusb302mpx.txt — read-only), device-id read. No init policy yet (that comes from the spec). Log I2C errors as {"ev":"i2c.err",...}.
4. `fw/src/console.{c,h}` — line command reader on the console: `regs` (dump all FUSB302 regs as {"ev":"reg","a":"0x..","v":"0x.."} lines or one {"ev":"regs","v":{...}}), `rd <reg>`, `wr <reg> <val>`, `pd want <mV>` (stores the target, sm hook), `reboot`. Replies as JSONL {"ev":"cmd",...}.
5. `fw/src/pd_sm.{c,h}` — table-driven state machine SKELETON: state enum, event enum, transition table struct {state, event, guard fn, action fn, next}, dispatch function, timer service (ms timers per state), every transition logged {"ev":"sm","from":..,"to":..,"on":..}. The SM must be pure C with no ESP-IDF includes; it talks to hardware only through a function-pointer interface (struct pd_port_ops) so the host sim can provide a fake. Put only placeholder states now (e.g. Unattached, Attached) to prove the mechanism.
6. `fw/src/main.c` — init I2C (400 kHz or 100 kHz — pick 400 and note it), log boot + device id, poll loop (e.g. every 1 ms or as tight as reasonable) calling the SM, console handling.
7. `test/host/` — Makefile (`make -C test/host test`, cc/clang on macOS), compiles pd_sm.c (+ future pd_msg.c) against a fake port (fake FUSB302 register model) and runs a tiny test proving table dispatch + timers + logging. Structure it so scenario tests (sequence of register states / RX FIFO contents over time) are easy to add later.
8. Build the firmware with pio to make sure it compiles (`BUILD_ID=test python -m platformio run -d fw`). Compiling is fine; flashing is forbidden.

Commit after each working piece (git -C /Users/jan/dev/repos/usbPD; message ends with blank line + `Claude-Session: https://claude.ai/code/session_01GYKmvnfRn7dzPjksUB77xK`). Reply with a ≤15-line summary: files, build status, host test status, the pd_port_ops interface.
--- 2026-09-30T08:54:08.740Z Agent mr-spec: write state-machine spec
You are **mr-spec** in /Users/jan/dev/repos/usbPD (read AGENTS.md, then docs/knowledge/ fully — README.md first). You write `docs/spec.md`: exactly how the sink firmware must behave, precise enough that mr-engineer can implement a table-driven state machine from it without guessing. You do not write firmware, you do not touch hardware.

Existing firmware skeleton (read, don't modify): fw/src/pd_sm.{c,h} (table rows {state|ANY, event, guard, action, next|SAME}, per-state entry/exit, timer slots posting events, 16-slot event queue, pd_port_ops {reg_read, reg_write, fifo_read, fifo_write, now_ms}), fw/src/main.c (1 ms poll loop), test/host/ (scenario runner: timeline of register bit changes / RX FIFO pushes / events / expectations, 1 ms ticks, fake FUSB302 with datasheet reset values, clear-on-read IRQs, FIFO pop). Design the spec to fit this architecture.

docs/spec.md must contain:
1. Scope & goal: sink only, PD R2.0 SpecRev on the wire (per knowledge base decision; state it), fixed PDOs, target voltage from `pd want <mV>` (default 5000 until set; if target PDO not offered → pick highest fixed PDO ≤ target, else vSafe5V), goal Accept → PS_RDY.
2. Layering: (a) poll/driver layer: what is read from the chip each poll (interrupt regs once each, status regs), how raw facts become SM events (VBUS on/off, CC level, I_HARDRST, I_TXSENT, I_RETRYFAIL, RX message available → decoded message event); (b) protocol layer (MessageID tx counter, rx duplicate filter, GoodCRC handled by chip); (c) policy/Type-C state machine.
3. The complete state list (Type-C: Unattached.SNK, AttachWait.SNK, Attached.SNK incl. orientation detection; PE: Startup, Discovery, Wait_for_Capabilities, Evaluate_Capability, Select_Capability, Transition_Sink, Ready, Hard_Reset / Transition_to_default, Soft_Reset handling — minimal but spec-correct), each with entry actions (exact register writes with values, citing knowledge files), exit actions, timers.
4. The full transition table as a markdown table: state | event | guard | action | next | spec/knowledge ref. Include error paths: SenderResponse timeout, Reject/Wait, PSTransition timeout, unexpected messages, hard reset received, VBUS loss/detach, repeated Source_Capabilities in Ready (re-evaluate), Get_Sink_Cap → Sink_Capabilities or Reject (decide; minimal), Soft_Reset received → Accept.
5. Timers with values and source.
6. Message encode/decode contract for pd_msg: header fields, SOURCE_CAPS parse into PDO list, REQUEST build (with the worked 9V example as a test vector), TX FIFO byte sequence.
7. Logging contract (JSONL events the firmware must emit so bench/q can prove each step): e.g. {"ev":"cc","cc1":..,"cc2":..,"orient":..}, {"ev":"pd.rx","type":"SRC_CAPS","id":..,"pdos":[...]}, {"ev":"pd.tx","type":"REQUEST","pos":..,"mv":..,"ma":..}, {"ev":"pd.txres","res":"sent|fail"}, {"ev":"sm",...}, {"ev":"contract","mv":..,"ma":..}, raw FIFO bytes on decode failure {"ev":"pd.rxraw","hex":...}, periodic reg snapshot option.
8. Host test cases list (ID, name, timeline sketch, expected states/log/register writes) covering: happy path 5 V, happy path 9 V, CC2 orientation, no caps → hard reset after tTypeCSinkWaitCap, Reject, PSTransition timeout, source hard reset during Ready, detach, duplicate MessageID, SRC_CAPS with 5 PDOs incl. an APDO (ignored), poll timing (Request TX within 15 ms of RX), GoodCRC-from-source in RX FIFO is not a new message, etc.
9. Bench acceptance per milestone (M4 RX alive, M5 5 V contract 2x over replug, M6 requested voltage) phrased as bench/q-checkable log patterns. Bench facts: charger off ≥25 s → on → attach ~1 s later; the source issues its own hard reset ~32 s after attach if no contract.
10. Open questions list (take over knowitall's uncertainties: masked event latching, I_TXSENT vs GoodCRC MessageID, Power 0x07 vs 0x0F, VBUSOK threshold, SW reset delay, Type-C debounce/vRd unverified) with the chosen default for each and how the bench will confirm it.

If you need a fact the knowledge base lacks, check the sources listed in AGENTS.md directly and cite them. Commit docs/spec.md (you may commit in pieces) with git -C /Users/jan/dev/repos/usbPD; only stage your own file; message ends with blank line + `Claude-Session: https://claude.ai/code/session_01GYKmvnfRn7dzPjksUB77xK`. Reply with a ≤12-line summary: state count, transition count, test-case count, key decisions.
--- 2026-09-30T09:02:08.977Z SendMessage a99f9ffc0e4492a8e
Two small infra fixes from mr-bench (see bench/README.md), please do them now while mr-spec writes docs/spec.md: (1) add "build" to every hb line (boot line can be dropped before capture reopens the port); (2) switch fw/ to the fast build-id scheme bench/hello uses (bench/build_id.py → gitignored generated header, so a new id rebuilds in ~5 s instead of ~65 s); keep the BUILD_ID env contract. Verify `bench/flash.sh --build-only` (build only, no flash) and `make -C test/host test`, commit, reply ≤5 lines. Still no PD logic until docs/spec.md lands.
--- 2026-09-30T09:07:18.850Z SendMessage a99f9ffc0e4492a8e
Phase 2: docs/spec.md is in (bad0306). Implement it completely: fake-port extensions F1–F7, pd_msg.{c,h} (encode/decode; vectors V1–V8 + D1 byte-exact as host tests), driver poll layer, protocol layer, all 16 states / full transition table in pd_sm, logging per §7, and host tests T01–T38. Target: `make -C test/host test` all green, `bench/flash.sh --build-only` builds. The spec is the contract — if something is ambiguous or wrong, write the question/deviation into docs/spec-questions.md and pick the spec's stated default; don't silently diverge. Keep the 1 ms poll; check the per-poll I2C budget at 400 kHz and log a warning event if a poll step exceeds 2 ms. Commit in logical pieces (msg codec, driver, SM, tests). Don't create runs/ dirs (build via `BUILD_ID=... python -m platformio run -d fw` directly). Reply ≤12 lines: test count/status, build status, deviations/questions.
--- 2026-09-30T09:35:27.283Z SendMessage a3c51e388474e2e04
mr-engineer implemented your spec (host tests 43/43 green) and wrote docs/spec-questions.md (Q1–Q8). Please decide each one: update docs/spec.md accordingly (e.g. §7 gets poll.slow, poll_max_us, sm.qfull; decide the Q4 burst read of the 5 poll registers — check the datasheet for whether the chip auto-increments over 0x3C–0x42 and cite it; Q5 TX result details; Q6; Q7 unify hex case), and append a "Resolution" line under each question in spec-questions.md. Commit only those two files. Reply ≤8 lines: what changed in the spec that the engineer must implement.
--- 2026-09-30T09:37:45.150Z SendMessage a99f9ffc0e4492a8e
mr-spec resolved Q1–Q8 (commit 9652a53, see Resolution lines in docs/spec-questions.md and updated spec §2.1/§6.2/§7/§10). Implement: burst poll 0x3E–0x42 (5 bytes, stop before 0x43) with PD_DRV_BURST_POLL=0 fallback; RX frame read in 2 transactions, max 8 frames per loop, reuse Status1 from burst; all log hex "0x"+uppercase zero-padded (log_hex8 + existing lines); poll.slow {us,state,n} ≤1/s, sm.qfull {on,drops}, hb poll_max_us, pd.txres coll/i2c flags; collision resend restarts the 10 ms TX deadline; log-only decode for battery/variable/APDO PDOs. Add/adjust host tests (both burst modes), keep all green, build fw (no runs/ dirs). Commit. Reply ≤6 lines incl. new worst-case I2C estimate.
```
