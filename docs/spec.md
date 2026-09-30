# USB-PD sink specification: FUSB302 on ESP32-C6

Status: v1. Owner: mr-spec. Audience: mr-engineer (implementation), mr-bench (acceptance).
This document is **normative**. If the firmware and this spec disagree, the firmware is wrong,
or the spec gets a new commit first. Numbers are chosen here so nobody has to guess.

## 0. Citation keys

| Key | Source |
|---|---|
| DS | `~/dev/repos/PCB-pi-r0/tmp/pdfs/fusb302mpx.txt` (FUSB302 datasheet text), `Lnnn` = line |
| PSE | `~/dev/repos/PCB-pi-r0/docs/spec/usb-pd/pd-sink-essentials.txt` (USB-PD R2.0 v1.3 extract) |
| R32 | `~/dev/repos/PCB-pi-r0/docs/spec/usb-pd/usb-pd-spec-r3.2-v1.2.txt` |
| LNX / LNXH | Linux `drivers/usb/typec/tcpm/fusb302.c` / `fusb302_reg.h` @55311a92 (as quoted in the knowledge files) |
| K:init Xn | `docs/knowledge/fusb302-sink-init.md`, step Xn (A2…C5), pitfall Pn |
| K:fifo | `docs/knowledge/fusb302-fifo.md` |
| K:reg | `docs/knowledge/fusb302-registers.md` |
| K:msg | `docs/knowledge/pd-messages.md` |
| K:pol | `docs/knowledge/pd-sink-policy.md` |
| ENG | Engineering choice made in this spec (no spec text forces it; see §10 where relevant) |

Register names and bit macros are those in `fw/src/fusb302_regs.h`.

---

## 1. Scope and goal

- **Sink only.** There is no source role, no DRP, no swaps, no VCONN, no SOP'/SOP'', no BIST, no extended messages, and no PPS.
- **Wire revision: PD R2.0.** Every header we send has SpecRev = 01b (K:msg L43). We accept R2.0 and R3.x headers from the source and decode the 5-bit type field (bit 4 is reserved in R2.0).
- **Fixed PDOs only.** Variable, battery and augmented (APDO/PPS) PDOs are decoded and logged, but they are never selected.
- **Target voltage.** `want_mv` is set by the console command `pd want <mV>`. The default is 5000 at boot, and the value survives detach.
- **Selection rule** (§4, EvalCap), applied in order:
  1. The fixed PDO whose voltage equals `want_mv`.
  2. Otherwise, the fixed PDO with the highest voltage ≤ `want_mv`.
  3. Otherwise, PDO1 (vSafe5V; PDO1 is always fixed 5 V per PSE).
- **Goal of a negotiation:** Request, then Accept, then PS_RDY, then the `contract` log line. Holding the contract after that is the steady state.
- **Out of scope:** battery or current management, USB data, dead-battery mode, and FUSB302 HW toggle (we use manual BC_LVL measurement; see K:init B-alt).

## 2. Layering

Three layers. Only the policy layer is table-driven. The lower two are plain C with pure functions where possible, so the host tests can call them directly.

| Layer | Suggested file | Owns | Must not |
|---|---|---|---|
| **L1 driver / poll** | `pd_drv.c` (pure C over `pd_port_ops`, runs in the host sim; `fusb302.c` stays the raw ESP-IDF I2C HAL) | All I2C. Init writes. The per-step poll (§2.1). TX burst write. RX FIFO read. TX result detection. VBUS edge detection. It posts events into the SM queue. It owns `tx_msgid`, `rx_last_id` and the duplicate filter (protocol-layer duties per PSE §6.7). | Decide policy. Keep timers other than the TX guard. |
| **L2 protocol / codec** | `pd_msg.c` | Pure encode/decode: header pack/unpack, PDO decode, RDO build, Sink_Caps PDO build, TX byte-burst build, message classification (§6). No I/O. | Touch registers or the SM. |
| **L3 policy + Type-C** | `pd_sm.c` table | States, transitions, guards and actions (§3, §4). PE timers via `pd_timer_*`. Target selection. | Touch the FIFO directly (it calls L1 `drv_tx()` / `drv_hard_reset()` / `drv_write()`). |

The skeleton semantics (`fw/src/pd_sm.c`) stay as they are:

- The first matching row wins. `PD_ST_ANY` rows are placed **after** all specific rows.
- The action runs first. Then, for a real state change, `on_exit` runs, timers without `keep_on_exit` are cancelled, and `on_entry` runs.
- `PD_ST_SAME` runs the action only. It runs no exit or entry, and it does not cancel timers.
- **Consequence:** a timer that must run in the *next* state is started in that state's `on_entry`, never in the action of the row that leads there. Use a flag if needed (see `sinkreq_pending`).
- Events are enum only. The payload of the last received message lives in `struct pd_sm` (`rx_hdr`, `rx_ndo`, `rx_obj[7]`, `rx_type`). Stored source caps live in `caps[7]` / `caps_n`.

### 2.1 Driver poll, once per `pd_sm_step` (every 1 ms)

The order is mandatory:

1. Read 0x3E–0x42 in **one 5-byte burst read**: register address 0x3E, then 5 data bytes. The result is Interrupta, Interruptb, Status0, Status1 and Interrupt, in that order.
   - The chip auto-increments the address on multi-byte reads: "Read Data K, K+1 … K+N-1" (DS L693–717, Figure 14).
   - The only register without auto-increment is FIFOS 0x43 (DS L1062, note 11 L1068). A burst must therefore never extend past 0x42.
   - The three interrupt registers are read-to-clear and are read exactly once per poll. Keep all 5 values in locals, and keep Status0 in `sm->st0` for the CC sampler.
   - Provide a compile-time fallback, `PD_DRV_BURST_POLL=0`, that uses 5 single reads in the same order, for an A/B test if Q16 fails.
   - If the read fails, abandon the step. Interrupt bits cleared by that failed read are lost, and `i2c.err` shows it.
   (K:reg; K:init A5/C5; DS interrupt tables.)
2. **If `FUSB_IA_HARDRST`:**
   - Post `EV_HR_RX` and log `pd.hr {dir:"rx"}`.
   - Write Control1 = 0x04 (RX flush). Clear `tx_pending`.
   - Skip steps 4 and 6 for this poll.
3. **If `FUSB_IA_HARDSENT`:** post `EV_HARD_SENT`.
4. **TX result.** Only while `tx_pending`, in this order:
   - `FUSB_IA_RETRYFAIL`: clear `tx_pending`. Post `EV_TX_FAIL`. Log `pd.txres {res:"fail"}`.
   - `FUSB_IA_TXSENT`: TX success, with `res:"sent"`.
   - `FUSB_I_COLLISION` (Interrupt 0x42):
     - First time for this message: rewrite the same burst (§6.4), including the TX flush. Log `pd.txres {res:"coll"}` and restart `tx_deadline` (+10 ms).
     - Second time: `EV_TX_FAIL`, logged `pd.txres {res:"fail", coll:true}`.
   - An I2C error while writing the burst (in `drv_tx` or in a collision resend) gives `EV_TX_FAIL` immediately. Log `pd.txres {res:"fail", i2c:true}` and clear `tx_pending`.
   - Otherwise, if `now ≥ tx_deadline` (write time + 10 ms): TX success with `res:"tmo"`. See §10 Q2.
   - **TX success** means:
     1. `tx_pending=0`
     2. `tx_msgid=(tx_msgid+1)&7`
     3. post `EV_TX_SENT`
     4. log `pd.txres`

   Success is posted **once per message**, whichever signal comes first. A GoodCRC seen in step 6 can also produce it.
5. **VBUS edge.** Take the level `Status0.VBUSOK` (`FUSB_ST0_VBUSOK`).
   - If `!vbus_known` or the level differs from `sm->vbus`: store it, set `vbus_known`, post `EV_VBUS_ON` or `EV_VBUS_OFF`, and log `vbus {on}`.
   - No debounce (ENG; §10 Q4).
6. **RX loop**, while `Status1.RX_EMPTY == 0` and no PE-bound message has been posted in this poll:
   1. Read the token byte from FIFOS (0x43). Then:
      - `tok & 0xE0 == 0xE0` (SOP): continue.
      - `tok & 0xE0` ∈ {0xC0, 0xA0, 0x80, 0x60} (SOP', SOP'', SOP'_Debug, SOP''_Debug): consume the frame as below, log `pd.rx {sop:"other", cls:"ignored"}`, and continue the loop.
      - Anything else: write Control1 = 0x04 (RX flush), log `pd.rxraw {tok}`, and stop the loop (K:fifo).
   2. The frame is read in **2 transactions**. FIFOS reads repeat on 0x43 without auto-increment, so a multi-byte read pops consecutive FIFO bytes (DS L1062, L1068):
      1. Token and 2 header bytes (3 bytes; step 6.1 above uses byte 0).
      2. `4·NDO` data bytes plus 4 CRC bytes, read together (the CRC is discarded). For a GoodCRC this is just the 4 CRC bytes.
      A bad token after the 3-byte read is handled as in 6.1: flush, then stop.
   3. Classify (§6.2):
      - **GoodCRC** (control type 1, NDO 0): never a PE event. If `tx_pending && id == tx_id`, it is TX success with `res:"gcrc"`. Log `pd.gcrc {id, match}`. Continue the loop.
      - **Duplicate:** `id == rx_last_id` and type ≠ Soft_Reset. Log `pd.dup {type,id}`. Continue the loop. No event is posted. The chip has already sent GoodCRC automatically, which is correct.
      - **Otherwise:**
        1. set `rx_last_id = id`
        2. copy the header and objects into `sm`
        3. log `pd.rx`
        4. post the event from §6.2
        5. stop the loop, because at most one PE message is handled per poll. The rest stay in the FIFO for the next poll.
   4. Re-read Status1 (single read of 0x41) before each iteration after the first. The first iteration uses Status1 from the step-1 burst.
   5. At most **8 frames** per poll. GoodCRC, duplicate and ignored frames do not stop the loop; the first frame that posts a PE event does.
7. The skeleton then services timers (`service_timers`) and drains the queue. Hardware events are posted before timer events in the same step (ENG).

The driver always does steps 1–6 in every state, including Unattached. That is safe because Switches1 has `AUTO_CRC` = 0 while unattached (§3). RX of noise while unattached is harmless: it is classified and dropped as unhandled.

### 2.2 Driver API used by L3 actions

- `drv_tx(type, ndo, objs)`:
  1. Build the header with `tx_msgid` (§6.1).
  2. Write Control0 = 0x64. This sets TX_FLUSH (self-clearing) and keeps INT_MASK and HOST_CUR at their reset values, with AUTO_PRE = 0 (K:init C2, P-list).
  3. Write the burst (§6.4) to FIFOS in **one** I2C write.
  4. Set `tx_pending=1`, `tx_id=tx_msgid`, `tx_coll=0`, `tx_deadline=now+10`.
  5. Log `pd.tx`.
- `drv_hard_reset()`: write Control3 = 0x47 (`0x07 | SEND_HARD_RESET`; K:init hard-reset notes, K:reg). Clear `tx_pending`.
- `proto_reset()`: `tx_msgid = 0`, `rx_last_id = -1`, `tx_pending = 0`.
  **Never** set `rx_last_id = 0`: the source's first message after a reset has ID 0 and would be dropped as a duplicate (K:msg; PSE §6.7.1 / §6.6.1).

---

## 3. States

16 states. The name column is exactly the string logged in `sm` lines (`from`/`to`) and in `hb.state`.

| # | Enum | Log name | PSE / Type-C equivalent |
|---|---|---|---|
| 0 | PD_ST_UNATTACHED | Unattached | Unattached.SNK |
| 1 | PD_ST_ATTACH_WAIT | AttachWait | AttachWait.SNK |
| 2 | PD_ST_ATTACHED | Attached | Attached.SNK (transient: configures the PHY) |
| 3 | PD_ST_STARTUP | Startup | PE_SNK_Startup (transient) |
| 4 | PD_ST_DISCOVERY | Discovery | PE_SNK_Discovery |
| 5 | PD_ST_WAIT_CAPS | WaitCaps | PE_SNK_Wait_for_Capabilities |
| 6 | PD_ST_EVAL_CAP | EvalCap | PE_SNK_Evaluate_Capability (transient) |
| 7 | PD_ST_SELECT_CAP | SelectCap | PE_SNK_Select_Capability |
| 8 | PD_ST_TRANS_SINK | TransSink | PE_SNK_Transition_Sink |
| 9 | PD_ST_READY | Ready | PE_SNK_Ready |
| 10 | PD_ST_GIVE_SINK_CAP | GiveSinkCap | PE_SNK_Give_Sink_Cap |
| 11 | PD_ST_SEND_SOFT_RESET | SendSoftReset | PE_SNK_Send_Soft_Reset |
| 12 | PD_ST_SOFT_RESET | SoftReset | PE_SNK_Soft_Reset (a Soft_Reset was received) |
| 13 | PD_ST_HARD_RESET | HardReset | PE_SNK_Hard_Reset |
| 14 | PD_ST_TRANS_DEFAULT | TransDefault | PE_SNK_Transition_to_default |
| 15 | PD_ST_DISABLED | Disabled | "source non-responsive" (PSE L4030–4036 note), ENG |

Transient states post `EV_DONE` from `on_entry`. The skeleton drains up to 16 events per step, so a transient chain completes inside the same `pd_sm_step`. This is required by the timing rule in §5.1.

### 3.0 Boot (`pd_sm_init`, once; not a state)

1. Write Reset = 0x01 (SW_RES). K:init A2; P11: SW_RES happens **only here**.
2. Write Power = 0x0F (all blocks on; K:init A3; §10 Q3).
3. Write Control3 = 0x07 (AUTO_RETRY, N_RETRIES = 3; K:init A4; nRetryCount 3 for R2.0, PSE L2172–2185).
4. Mask registers stay 0x00 (reset value). Read 0x3E, 0x3F and 0x42 to clear them (K:init A5).
5. Set `want_mv = 5000`, `hr_count = 0`, `vbus_known = 0`.
6. Log `sm.init {rows, states:16}`.
7. Enter Unattached (run its `on_entry`).

### 3.1 Per-state entry, exit and timers

"Writes" are I2C register writes, in the order listed. There are no exit actions beyond the skeleton's automatic timer cancel, unless a row below says otherwise.

| State | on_entry | on_exit | Timers (slot, ms, event) | Refs |
|---|---|---|---|---|
| **Unattached** | 1. Switches1 = 0x20 (SpecRev 01, AUTO_CRC off, no TXCC).<br>2. Switches0 = 0x03 (PDWN1\|PDWN2, no MEAS).<br>3. Control0 = 0x64.<br>4. Control1 = 0x04.<br>5. Reset = 0x02.<br>6. Clear `caps_n`, `have_contract`, `orient`, `reject_fallback`, `sinkreq_pending`.<br>7. `hr_count = 0` (PSE L4034: "reset on power cycle or Detach").<br>8. `proto_reset()`.<br>If `have_contract` was set before step 6, first log `contract.lost {why:"detach"}`. | – | none | K:init B1, C2–C4; PSE L4034 |
| **AttachWait** | 1. Switches0 = 0x07 (MEAS_CC1).<br>2. `meas = 1`; clear the samples (`lvl[1..2] = 0xFF`, `n[1..2] = 0`). | – | TMR_TC 100 → EV_DEBOUNCE; TMR_CC 5 → EV_CC_TICK | K:init B3–B4; tCCDebounce 100–200 ms (K:pol, not verified, §10 Q6) |
| **Attached** | 1. Switches0 = 0x07 (CC1) or 0x0B (CC2).<br>2. Switches1 = 0x25 (CC1) or 0x26 (CC2).<br>3. Control0 = 0x64.<br>4. Control1 = 0x04.<br>5. Reset = 0x02 (PD_RESET).<br>6. Read 0x3E, 0x3F, 0x42 (discard).<br>7. Log `cc {cc, lvl1, lvl2}` and `pd.cfg {sw0, sw1}`.<br>8. Post EV_DONE. | – | none | K:init B6, C1–C5 (the C sequence in that order) |
| **Startup** | 1. `proto_reset()`.<br>2. Clear `caps_n` and `have_contract`.<br>3. Post EV_DONE. | – | none | PSE §8.3.3.3.1 |
| **Discovery** | Start TMR_PE. If `sm->vbus`, post EV_DONE. | – | TMR_PE 2500 → EV_TIMEOUT (ENG: tPSHardReset 35 + tSafe0V 650 + tSrcRecover ≤1000 + tSrcTurnOn 275 ≈ 1960 ms, plus margin; R32 L5017, L5097, L5275) | PSE §8.3.3.3.2 |
| **WaitCaps** | Start TMR_PE. | – | TMR_PE 600 → EV_TIMEOUT (tTypeCSinkWaitCap 310–620, PSE L2039; 600 keeps a 20 ms margin below the max) | PSE §8.3.3.3.3, L1064–1066 |
| **EvalCap** | 1. Run `select()` (§4.2) into `sel_pos`, `sel_mv`, `sel_ma`, `sel_why`.<br>2. Log `pd.sel`.<br>3. Post EV_DONE. | – | none | PSE §8.3.3.3.4 |
| **SelectCap** | 1. Build the RDO (§6.3).<br>2. `drv_tx(Request, 1, &rdo)`. | – | TMR_PE 30 → EV_TIMEOUT, started by the **EV_TX_SENT** row, not by entry (tSenderResponse 24–30, PSE L2028) | PSE §8.3.3.3.5 |
| **TransSink** | Start TMR_PE. | – | TMR_PE 550 → EV_TIMEOUT (tPSTransition 450–550, PSE L2019) | PSE §8.3.3.3.6 |
| **Ready** | If `sinkreq_pending`: clear it and start TMR_SINKREQ. | – | TMR_SINKREQ 100 → EV_SINKREQ (tSinkRequest 100, K:pol) | PSE §8.3.3.3.7 |
| **GiveSinkCap** | Build the Sink_Caps PDOs (§6.3) and call `drv_tx(Sink_Capabilities, n, pdos)`. | – | none (the driver TX guard covers a hung TX) | PSE §8.3.3.3.10, L1064 |
| **SendSoftReset** | 1. `proto_reset()`.<br>2. `drv_tx(Soft_Reset, 0)`, which uses ID 0 (header 0x004D).<br>3. Log `pd.sr {dir:"tx"}`. | – | TMR_PE 30 → EV_TIMEOUT, started by the EV_TX_SENT row (tSenderResponse) | PSE §8.3.3.4.2 (soft reset, sink) |
| **SoftReset** | 1. `proto_reset()`.<br>2. `drv_tx(Accept, 0)`, which uses ID 0 (header 0x0043).<br>3. Log `pd.sr {dir:"rx"}`. | – | none | PSE §8.3.3.4.1 |
| **HardReset** | If `hr_count > 2` (nHardResetCount; PSE L3938, L4030): post EV_GIVE_UP and do nothing else.<br>Otherwise:<br>1. `hr_count++`.<br>2. `drv_hard_reset()` (Control3 = 0x47).<br>3. Log `pd.hr {dir:"tx", n:hr_count}`.<br>4. Start TMR_PE. | – | TMR_PE 10 → EV_TIMEOUT (ENG guard; the HR ordered set takes < 1 ms; tHardResetComplete 4–5 ms, K:pol) | PSE §8.3.3.3.8 |
| **TransDefault** | 1. Reset = 0x02.<br>2. Control1 = 0x04.<br>3. Control0 = 0x64.<br>4. Control3 = 0x07 (clears SEND_HARD_RESET if it is not self-clearing; §10 Q7).<br>5. `proto_reset()`.<br>6. If `have_contract`: log `contract.lost {why:"hardreset"}`. Clear `have_contract` and `caps_n`.<br>7. Start TMR_PE.<br>Switches0 and Switches1 are unchanged: orientation is kept. | – | TMR_PE 1000 → EV_TIMEOUT (ENG: VBUS must reach vSafe0V within tPSHardReset 35 + tSafe0V 650 = 685 ms; R32 L5275, PSE L2016) | PSE §8.3.3.3.9, L3905–3923 |
| **Disabled** | 1. Log `pd.giveup {n:hr_count}`.<br>2. If `have_contract`: log `contract.lost {why:"giveup"}` and clear it. | – | none | PSE L4030–4036 |

**CC sampler** (the AttachWait `EV_CC_TICK` action `a_cc_sample`):

1. Take `bc = sm->st0 & 0x03` (`BC_LVL`) from this step's poll. The sample is valid only if `(st0 & FUSB_ST0_ACTIVITY) == 0`. If it is invalid, keep MEAS and restart TMR_CC 5.
2. If `bc == lvl[meas]`, do `n[meas]++`. Otherwise set `lvl[meas] = bc` and `n[meas] = 1`.
3. Flip `meas`: write Switches0 = 0x0B if `meas` was 1, 0x07 if it was 2.
4. Restart TMR_CC 5.

Why 5 ms: the BC_LVL comparator needs settle time after a MEAS change (LNX defers reads while ACTIVITY is set; K:pol).

**Guard `g_cc_ok`:** exactly one of `lvl[1]`, `lvl[2]` is non-zero, and both `n[1] ≥ 2` and `n[2] ≥ 2`. The non-zero side sets `orient`.

---

## 4. Transition table

### 4.1 Events

| Event | Posted by |
|---|---|
| EV_VBUS_ON / EV_VBUS_OFF | driver step 5 |
| EV_CC_TICK | TMR_CC |
| EV_DEBOUNCE | TMR_TC |
| EV_DONE | on_entry of transient states |
| EV_TIMEOUT | TMR_PE (the existing `PD_EV_TIMEOUT`) |
| EV_SINKREQ | TMR_SINKREQ |
| EV_TX_SENT / EV_TX_FAIL | driver step 4 / 6 |
| EV_HARD_SENT | driver step 3 |
| EV_HR_RX | driver step 2 |
| EV_GIVE_UP | HardReset on_entry |
| EV_WANT | `pd_sm_set_want()` (existing) |
| EV_RX_SRC_CAPS, EV_RX_ACCEPT, EV_RX_REJECT, EV_RX_WAIT, EV_RX_PS_RDY, EV_RX_SOFT_RESET, EV_RX_GET_SINK_CAP, EV_RX_PING, EV_RX_GOTOMIN, EV_RX_UNEXPECTED, EV_RX_UNSUPPORTED | driver step 6, via §6.2 |

That is 24 events (plus the existing `PD_EV_NONE`). Timer slots: `PD_TMR_TC`, `PD_TMR_CC`, `PD_TMR_PE` and `PD_TMR_SINKREQ`. None of them is `keep_on_exit`.

**Unhandled events:** any (state, event) pair that has no row, including via ANY, is dropped by the skeleton with `sm.unhandled`. That **is** the specified "ignore" behaviour, for example a Get_Sink_Cap in WaitCaps or a stale TX_SENT. Events that §4.3 says to ignore *explicitly* get a `SAME` row with no action. This keeps `sm.unhandled` meaningful as a "spec gap" signal.

### 4.2 Guards and actions

| Name | Definition |
|---|---|
| g_vbus | `sm->vbus == 1` |
| g_cc_ok | see §3.1, CC sampler |
| g_contract | `have_contract == 1` |
| g_tx_pending | `tx_pending == 1`, meaning the request has not been GoodCRC'd yet |
| g_caps | `caps_n ≥ 1` |
| a_cc_sample | CC sampler (§3.1) |
| a_cc_retry | Log `cc.retry {lvl1,lvl2,n1,n2}`. Clear the samples. Restart TMR_TC 100. No retry limit. |
| a_orient | `orient = (lvl[1] != 0) ? 1 : 2` |
| a_start_sr | Start TMR_PE 30 (tSenderResponse) |
| a_reject_nc | If `sel_mv > 5000`, set `reject_fallback = 1`. Log `pd.rej {mv:sel_mv}`. |
| a_wait_c | `sinkreq_pending = 1` |
| a_contract | 1. `have_contract = 1`<br>2. `con_pos/mv/ma = sel_*`<br>3. `hr_count = 0` (ENG, §10 Q8)<br>4. `reject_fallback = 0`<br>5. log `contract {mv, ma, pos}` |
| a_tx_reject | `drv_tx(Reject, 0)` |
| a_lost_detach | If `have_contract`, log `contract.lost {why:"detach"}`. (Unattached entry then clears it.) |
| a_log_want | Log `pd.want {mv}`. `want_mv` itself is already stored by `pd_sm_set_want`. |

**`select()`**, used in EvalCap entry:

```
fixed = [(i, mv_i, ma_i) for i in 1..caps_n if caps[i] is Fixed (bits 31:30 == 00)]
if reject_fallback:                   pick PDO1, why="fallback"
elif any fixed mv == want_mv:         pick it, why="exact"
elif any fixed mv <= want_mv:         pick the highest mv <= want_mv, why="below"
else:                                 pick PDO1, why="min"
sel_ma = min(ma_of_pick, 3000)        # cable without e-marker: 3 A max (ENG)
```

A 20 V / 5 A PDO is therefore requested at 3 A.

### 4.3 Table

Rows are in table order. Specific rows come first, then the ANY rows at the end. A cell listing several events (`A \| B`) expands to one C row per event with identical other columns. "–" means no guard or no action.

| # | state | event | guard | action | next | ref |
|---|---|---|---|---|---|---|
| 1 | Unattached | VBUS_ON | – | – | AttachWait | Type-C Unattached.SNK; ENG: attach is gated on VBUS (§10 Q13) |
| 2 | Unattached | VBUS_OFF \| HR_RX | – | – | SAME | not attached, ignore |
| 3 | AttachWait | CC_TICK | – | a_cc_sample | SAME | K:init B3–B4 |
| 4 | AttachWait | DEBOUNCE | g_cc_ok | a_orient | Attached | tCCDebounce (K:pol) |
| 5 | AttachWait | DEBOUNCE | !g_cc_ok | a_cc_retry | SAME | ENG |
| 6 | AttachWait | VBUS_OFF | – | – | Unattached | Type-C |
| 7 | AttachWait | HR_RX | – | – | SAME | PE not active |
| 8 | Attached | DONE | – | – | Startup | PSE §8.3.3.3.1 |
| 9 | Attached | HR_RX | – | – | SAME | PE not active yet |
| 10 | Startup | DONE | – | – | Discovery | PSE §8.3.3.3.1 |
| 11 | Startup | VBUS_OFF | – | – | SAME | Discovery handles VBUS |
| 12 | Discovery | DONE | g_vbus | – | WaitCaps | PSE §8.3.3.3.2 |
| 13 | Discovery | VBUS_ON | – | – | WaitCaps | PSE §8.3.3.3.2 |
| 14 | Discovery | VBUS_OFF | – | – | SAME | VBUS still low after HR |
| 15 | Discovery | TIMEOUT | – | – | Unattached | ENG: VBUS never returned, so treat as detach |
| 16 | WaitCaps | RX_SRC_CAPS | – | – | EvalCap | PSE §8.3.3.3.3 |
| 17 | WaitCaps | TIMEOUT | – | – | HardReset | SinkWaitCapTimer, PSE L1064–1066 |
| 18 | WaitCaps | VBUS_ON | – | – | SAME | duplicate from Discovery |
| 19 | EvalCap | DONE | – | – | SelectCap | PSE §8.3.3.3.4 |
| 20 | SelectCap | TX_SENT | – | a_start_sr | SAME | PSE §8.3.3.3.5 (SenderResponseTimer starts on GoodCRC) |
| 21 | SelectCap | RX_ACCEPT | – | – | TransSink | PSE §8.3.3.3.5; no tx_pending guard: an Accept implies our request arrived |
| 22 | SelectCap | RX_REJECT | g_contract | – | Ready | PSE §8.3.3.3.5 |
| 23 | SelectCap | RX_REJECT | !g_contract | a_reject_nc | WaitCaps | PSE §8.3.3.3.5; the fallback is ENG |
| 24 | SelectCap | RX_WAIT | g_contract | a_wait_c | Ready | PSE §8.3.3.3.5 + SinkRequestTimer |
| 25 | SelectCap | RX_WAIT | !g_contract | – | WaitCaps | PSE §8.3.3.3.5 |
| 26 | SelectCap | TIMEOUT | – | – | HardReset | SenderResponseTimer, PSE §8.3.3.3.5 |
| 27 | SelectCap | TX_FAIL | – | – | SendSoftReset | PSE L2122–2126 (a transmit failure is a protocol error) |
| 28 | SelectCap | RX_SRC_CAPS | g_tx_pending | – | EvalCap | PSE L2195–2230: our message is not yet acknowledged, so process the incoming one |
| 29 | SelectCap | RX_SRC_CAPS | !g_tx_pending | – | SendSoftReset | PSE L3905–3923: unexpected message |
| 30 | SelectCap | RX_PS_RDY \| RX_GET_SINK_CAP \| RX_GOTOMIN \| RX_UNEXPECTED \| RX_UNSUPPORTED | – | – | SendSoftReset | PSE §6.7.1 L2195–2230 |
| 31 | SelectCap | RX_PING | – | – | SAME | Ping is always ignorable |
| 32 | TransSink | RX_PS_RDY | – | a_contract | Ready | PSE §8.3.3.3.6 |
| 33 | TransSink | TIMEOUT | – | – | HardReset | PSTransitionTimer, PSE L2019 |
| 34 | TransSink | RX_PING | – | – | SAME | – |
| 35 | TransSink | RX_SRC_CAPS \| RX_ACCEPT \| RX_REJECT \| RX_WAIT \| RX_SOFT_RESET \| RX_GET_SINK_CAP \| RX_GOTOMIN \| RX_UNEXPECTED \| RX_UNSUPPORTED | – | – | HardReset | PSE L2195–2230: a protocol error during a power transition → HR |
| 36 | TransSink | TX_FAIL | – | – | HardReset | same |
| 37 | Ready | RX_SRC_CAPS | – | – | EvalCap | PSE §8.3.3.3.7 |
| 38 | Ready | WANT | g_caps | a_log_want | EvalCap | ENG (DPM request) |
| 39 | Ready | SINKREQ | g_caps | – | EvalCap | SinkRequestTimer after Wait |
| 40 | Ready | RX_GET_SINK_CAP | – | – | GiveSinkCap | PSE §8.3.3.3.7, L1064 |
| 41 | Ready | RX_GOTOMIN | – | – | TransSink | PSE §8.3.3.3.7 |
| 42 | Ready | RX_PING | – | – | SAME | – |
| 43 | Ready | RX_ACCEPT \| RX_REJECT \| RX_WAIT \| RX_PS_RDY \| RX_UNEXPECTED | – | – | SendSoftReset | PSE L2195–2230: unexpected in Ready → Soft Reset |
| 44 | Ready | RX_UNSUPPORTED | – | a_tx_reject | SAME | PSE L2195–2230: unrecognized or unsupported → Reject |
| 45 | Ready | TX_SENT | – | – | SAME | result of a Reject |
| 46 | Ready | TX_FAIL | – | – | SendSoftReset | PSE L2122–2126 |
| 47 | GiveSinkCap | TX_SENT | – | – | Ready | PSE §8.3.3.3.10 |
| 48 | GiveSinkCap | TX_FAIL | – | – | SendSoftReset | PSE L2122–2126 |
| 49 | GiveSinkCap | RX_SRC_CAPS | – | – | EvalCap | PSE L2195–2230 (process the incoming message) |
| 50 | SendSoftReset | TX_SENT | – | a_start_sr | SAME | PSE §8.3.3.4.2 |
| 51 | SendSoftReset | RX_ACCEPT | – | – | WaitCaps | PSE §8.3.3.4.2 |
| 52 | SendSoftReset | TIMEOUT \| TX_FAIL | – | – | HardReset | PSE §8.3.3.4.2 |
| 53 | SoftReset | TX_SENT | – | – | WaitCaps | PSE §8.3.3.4.1 |
| 54 | SoftReset | TX_FAIL | – | – | HardReset | PSE §8.3.3.4.1 |
| 55 | HardReset | GIVE_UP | – | – | Disabled | PSE L4030–4036 |
| 56 | HardReset | HARD_SENT \| TIMEOUT \| VBUS_OFF | – | – | TransDefault | PSE §8.3.3.3.8 |
| 57 | TransDefault | VBUS_OFF | – | – | Startup | PSE L3905–3923 (VBUS dropped); Discovery then waits for VBUS_ON |
| 58 | TransDefault | TIMEOUT | – | – | Startup | ENG: VBUS never dropped |
| 59 | TransDefault | VBUS_ON \| HR_RX | – | – | SAME | ignore during recovery |
| 60 | Disabled | RX_SRC_CAPS | – | – | EvalCap | ENG: the source woke up |
| 61 | ANY | HR_RX | – | – | TransDefault | PSE §8.3.3.3.9 |
| 62 | ANY | VBUS_OFF | – | a_lost_detach | Unattached | Type-C detach (sink: VBUS removed) |
| 63 | ANY | RX_SOFT_RESET | – | – | SoftReset | PSE §8.3.3.4.1 |
| 64 | ANY | WANT | – | a_log_want | SAME | the new target is used at the next EvalCap |

64 table lines, which expand to **85 C rows**. The expansion: row 2 → 2, row 30 → 5, row 35 → 9, row 43 → 5, row 52 → 2, row 56 → 3, row 59 → 2, and every other line → 1.

Notes:

- Row 38 fires when a `WANT` arrives in Ready. In every other state, row 64 only logs, and the new `want_mv` is used by the next EvalCap: the next SRC_CAPS, or the next return to Ready followed by `pd want`.
- The **9 V demo path** is `pd want 9000` in Ready, then EvalCap, SelectCap (Request pos 2), TransSink and Ready with `contract mv=9000`. `pd want` before attach also works (the default path through WaitCaps).
- Row 29 against row 43: once our Request is GoodCRC'd, a new SRC_CAPS in SelectCap is a protocol error and leads to SendSoftReset. Before that, it replaces the pending request.
- The ANY rows make these paths explicit:
  - HR received in *any* state, including Unattached (row 2) and AttachWait (row 7) → TransDefault. Rows 2, 7 and 9 exclude it before the PE runs.
  - VBUS loss → Unattached. Rows 2, 6, 11, 14, 56 and 57 take precedence.
  - Soft_Reset received → SoftReset. Row 35 takes precedence in TransSink.

---

## 5. Timers

| Name | Slot | Value (ms) | Allowed range / source | Started | Stopped |
|---|---|---|---|---|---|
| CC sample | TMR_CC | 5 | ENG (BC_LVL settle; LNX uses a 30 ms deferral, K:pol) | AttachWait entry, each CC_TICK | state exit |
| tCCDebounce | TMR_TC | 100 | 100–200, Type-C (not local; K:README GAP) | AttachWait entry, a_cc_retry | state exit |
| Discovery VBUS wait | TMR_PE | 2500 | ENG from R32 L5017 (tSrcRecover 0.66–1.0 s), L5097 (tSrcTurnOn 275), L5275 (tSafe0V 650) | Discovery entry | exit |
| tTypeCSinkWaitCap | TMR_PE | 600 | 310–620, PSE L2039 | WaitCaps entry | exit |
| tSenderResponse | TMR_PE | 30 | 24–30, PSE L2028 (we use the max: we are the slow side) | TX_SENT in SelectCap / SendSoftReset | exit |
| tPSTransition | TMR_PE | 550 | 450–550, PSE L2019 | TransSink entry | exit |
| tSinkRequest | TMR_SINKREQ | 100 | 100 min, K:pol | Ready entry if sinkreq_pending | exit |
| HR sent guard | TMR_PE | 10 | ENG (tHardResetComplete 4–5, K:pol) | HardReset entry | exit |
| TransDefault VBUS-drop wait | TMR_PE | 1000 | ENG ≥ tPSHardReset 35 (PSE L2016) + tSafe0V 650 (R32 L5275) | TransDefault entry | exit |
| TX guard | driver `tx_deadline` | 10 | ENG (3 retries × (tReceive 1.1 + tRetry) ≪ 10 ms; K:fifo L108) | `drv_tx` | TX result |
| tReceiverResponse | – | 15 | PSE L1609–1622. This is our reply latency to the source. It is met by construction (§5.1). | – | – |

Not implemented, on purpose:

- SinkActivityTimer: not run on Type-C (PSE L3905–3923).
- NoResponseTimer: ENG. The HardReset retry loop runs through WaitCaps 600 ms × 3, then Disabled. The ~32 s source HR (bench fact) covers the rest.
- tFirstSourceCap and tTypeCSendSourceCap: source-side only.

### 5.1 Latency rules (hard)

- **Request latency.** The Request TX burst is written in the **same `pd_sm_step`** that decodes the SRC_CAPS frame, via the chain WaitCaps → EvalCap → SelectCap in one drain. The host test checks that it lands ≤ 2 ms (ticks) after the RXPUSH. On the bench: `pd.tx(type=Request).t − pd.rx(type=Source_Capabilities).t ≤ 10`.
- **Soft_Reset reply.** The Accept to a received Soft_Reset is written in the same step as well (ANY row 63, then SoftReset entry).

---

## 6. Message encode/decode contract (L2)

### 6.1 Header (16 bit, little-endian on the wire)

| Bits | Field | TX value | RX use |
|---|---|---|---|
| 4:0 | Message Type | type (bit 4 = 0) | `type = hdr & 0x1F` |
| 5 | Port Data Role | 0 (UFP) | ignore |
| 7:6 | Spec Revision | 01b (R2.0) | log it as `rev` (0=R1, 1=R2, 2=R3) |
| 8 | Port Power Role | 0 (Sink) | ignore (log only) |
| 11:9 | MessageID | `tx_msgid` | dup filter |
| 14:12 | Number of Data Objects | NDO | frame length |
| 15 | Extended | 0 | if 1 → RX_UNSUPPORTED |

Our TX header is `0x0040 | type | (id<<9) | (ndo<<12)`:

| Message | Header |
|---|---|
| Request | `0x1042 \| id<<9` |
| Soft_Reset | `0x004D` (id 0) |
| Accept | `0x0043` (id 0 after SoftReset) |
| Reject | `0x0044 \| id<<9` |
| Sink_Capabilities | `0x1044 \| id<<9` (NDO 1) or `0x2044 \| id<<9` (NDO 2) |

### 6.2 RX classification (header → event)

Control messages (NDO = 0):

| type | name | event |
|---|---|---|
| 1 | GoodCRC | none (driver, §2.1 step 6) |
| 2 | GotoMin | RX_GOTOMIN |
| 3 | Accept | RX_ACCEPT |
| 4 | Reject | RX_REJECT |
| 5 | Ping | RX_PING |
| 6 | PS_RDY | RX_PS_RDY |
| 7 | Get_Source_Cap | RX_UNSUPPORTED (sink only) |
| 8 | Get_Sink_Cap | RX_GET_SINK_CAP |
| 9, 10, 11 | DR_Swap, PR_Swap, VCONN_Swap | RX_UNSUPPORTED |
| 12 | Wait | RX_WAIT |
| 13 | Soft_Reset | RX_SOFT_RESET (exempt from the dup filter) |
| 0, 14–31 | reserved / R3-only | RX_UNSUPPORTED |

Data messages (NDO ≥ 1):

| type | name | event |
|---|---|---|
| 1 | Source_Capabilities | RX_SRC_CAPS. Precondition: PDO1 is Fixed, meaning bits 31:30 == 00. Otherwise RX_UNEXPECTED plus `pd.caps.bad`. The objects are copied to `caps[]` and `caps_n`, and `pd.caps` is logged. |
| 2 | Request | RX_UNEXPECTED (we are not a source) |
| 3 | BIST | none. Log `pd.rx cls:"ignored"` and post nothing. ENG: BIST is test-only. |
| 4 | Sink_Capabilities | RX_UNEXPECTED |
| 15 | Vendor_Defined | none. Log `cls:"ignored"` (R2.0: an unsupported Structured VDM is ignored). |
| other | – | RX_UNSUPPORTED |

Any message with Extended = 1 → RX_UNSUPPORTED.

The `cls` values in `pd.rx` are `ok`, `unexpected`, `unsupported` and `ignored`. The `type` field is the name from these tables, for example `Source_Capabilities`, `Accept` or `Unknown_C14`.

**Source PDO decode.** For a Fixed PDO (bits 31:30 = 00): `mv = ((pdo>>10)&0x3FF)*50`, `ma = (pdo&0x3FF)*10`. Bit 27 is `unconstrained`, logged only. Kind letters used in logs: `F` fixed, `B` battery (01), `V` variable (10), `A` augmented (11).

Non-fixed PDOs are decoded for the log only and are never selected. In `pd.caps`, `mv` is the maximum voltage:

| Kind | mv | ma |
|---|---|---|
| Battery | `((pdo>>20)&0x3FF)*50` | `((pdo&0x3FF)*250) * 1000 / mv` (bits 9:0 are max power in 250 mW units, converted to mA at max voltage; 0 if mv = 0) |
| Variable | `((pdo>>20)&0x3FF)*50` | `(pdo&0x3FF)*10` |
| Augmented (PPS) | `((pdo>>17)&0xFF)*100` | `(pdo&0x7F)*50` |

### 6.3 Data objects we build

**Request RDO (Fixed):**

```
rdo = (pos<<28) | (ma/10 << 10) | (ma/10)
```

- `pos` is 1-based.
- Operating current = max current = `sel_ma`.
- GiveBack (b27), CapabilityMismatch (b26), USB Comm (b25) and No USB Suspend (b24) are all 0 (K:msg; §10 Q9).

**Sink_Capabilities:**

```
PDO1 = 0x0001912C | (want_mv > 5000 ? 1<<28 : 0)        // 5 V, 3 A; b28 = Higher Capability
PDO2 = ((want_mv/50) << 10) | 300   only if want_mv > 5000  // want V, 3 A
```

- `want_mv` is rounded down to 50 mV.
- DRP, Unconstrained, USB Comm and DRD are all 0. PSE Table 6-10, L1303.

### 6.4 TX FIFO burst (one I2C write to 0x43)

```
12 12 12 13  (0x80|len)  hdr_lo hdr_hi  obj0_b0..b3 ... objN_b3  FF 14 FE A1
```

| Bytes | Meaning |
|---|---|
| `12 12 12 13` | SYNC1 ×3, SYNC2 |
| `0x80\|len` | PACKSYM, `len = 2 + 4·NDO` |
| `FF` | JAM_CRC |
| `14` | EOP |
| `FE` | TXOFF |
| `A1` | TXON |

Objects are little-endian. K:fifo, K:msg; DS token table.

The burst is preceded by `Control0 = 0x64` (TX flush). Control messages use `len = 2`, so the burst is `12 12 12 13 82 lo hi FF 14 FE A1` (11 bytes).

### 6.5 Normative test vectors

The encode vectors are byte-exact, so host tests compare the whole burst.

| # | Case | Expected |
|---|---|---|
| V1 | 9 V request: caps = [5 V/3 A, 9 V/3 A, …], want 9000, tx_msgid 0 | RDO `0x2004B12C`, hdr `0x1042`, burst `12 12 12 13 86 42 10 2C B1 04 20 FF 14 FE A1` (K:msg) |
| V2 | 5 V request, pos 1, id 0 | RDO `0x1004B12C`, burst `12 12 12 13 86 42 10 2C B1 04 10 FF 14 FE A1` |
| V3 | 9 V request with id 1 (e.g. `pd want 9000` in Ready after a 5 V contract with id 0) | hdr `0x1242`, burst `12 12 12 13 86 42 12 2C B1 04 20 FF 14 FE A1` |
| V4 | Soft_Reset (id 0) | `12 12 12 13 82 4D 00 FF 14 FE A1` |
| V5 | Accept reply to Soft_Reset (id 0) | `12 12 12 13 82 43 00 FF 14 FE A1` |
| V6 | Reject, id 2 | hdr `0x0444`, `12 12 12 13 82 44 04 FF 14 FE A1` |
| V7 | Sink_Caps, want 5000, id 0 | hdr `0x1044`, PDO `0x0001912C`, `12 12 12 13 86 44 10 2C 91 01 00 FF 14 FE A1` |
| V8 | Sink_Caps, want 9000, id 0 | hdr `0x2044`, PDOs `0x1001912C 0x0002D12C`, `12 12 12 13 8A 44 20 2C 91 01 10 2C D1 02 00 FF 14 FE A1` |

**Decode vectors.** These are the RX frames pushed in the sim: `E0 hdr_lo hdr_hi objs… crc×4`. The CRC bytes are arbitrary, e.g. `00 00 00 00`.

Source PDOs:

| PDO | Value |
|---|---|
| 5 V / 3 A, unconstrained | `0x0801912C` |
| 9 V / 3 A | `0x0002D12C` |
| 12 V / 3 A | `0x0003C12C` |
| 15 V / 3 A | `0x0004B12C` |
| 20 V / 5 A | `0x000641F4` |
| PPS APDO 3.3–21 V / 5 A | `0xC1A42164` |

Source headers:

| Message | Header |
|---|---|
| SRC_CAPS R3, NDO 4, id 0 | `0x41A1` (wire `A1 41`) |
| SRC_CAPS R3, NDO 5, id 0 | `0x51A1` |
| SRC_CAPS R2, NDO 2, id 0 | `0x2161` |
| Accept, R3, id 1 | `0x03A3` |
| PS_RDY, R3, id 2 | `0x05A6` |
| GoodCRC, id n | `0x01A1 \| n<<9` (R3) or `0x0161 \| n<<9` (R2) |
| Soft_Reset, R3, id 0 | `0x01AD` |
| Get_Sink_Cap, R3, id 3 | `0x07A8` |
| Ping, R3, id 3 | `0x07A5` |
| DR_Swap, R3, id 3 | `0x07A9` |

**D1.** The frame `E0 A1 41 2C 91 01 08 2C D1 02 00 2C B1 04 00 F4 41 06 00 00 00 00 00` must decode to the following, with `rev=2`:

| # | Kind | Voltage | Current |
|---|---|---|---|
| 1 | Fixed | 5000 mV | 3000 mA (unconstrained) |
| 2 | Fixed | 9000 mV | 3000 mA |
| 3 | Fixed | 15000 mV | 3000 mA |
| 4 | Fixed | 20000 mV | 5000 mA |

**Selection vectors (caps = D1):**

| want | Picked PDO | why |
|---|---|---|
| 5000 | pos 1 | exact |
| 9000 | pos 2 | exact |
| 12000 | pos 2 | below |
| 20000 | pos 4 at 3000 mA | exact; RDO `0x4004B12C` |
| 3000 | pos 1 | min |
| 9000 with reject_fallback | pos 1 | fallback |

With D1 plus the APDO as PDO5 and want 21000: pos 4 (why below); the APDO is ignored.

---

## 7. JSONL logging contract

Every line is one JSON object, built with `log.h`. `t` is milliseconds since boot and `ev` is the event name. Hex values are strings: lower-case `0x` prefix, **upper-case digits**, zero-padded to the natural width (2 for registers and bytes, 4 for headers, 8 for objects), e.g. `"0x07"`, `"0x1042"`, `"0x2004B12C"`. This applies to **every** line, including the older `fusb.id`, `regs`, `cmd` and `i2c.err` lines, which must switch to it (`log_hex8`). Byte strings are space-separated upper-case hex. Field names are exact, because `bench/q --where k=v` matches on them. `bench/q --type` filters on `type` and `--state` filters on `state`.

| ev | Fields | When |
|---|---|---|
| boot, fusb.id, cmd, regs, i2c.err | as `fw/README.md` today | unchanged |
| sm.init | rows (expanded C rows), states | init |
| hb | state, vbus (bool), want, mv (contract mV or 0), poll_max_us (the longest `pd_sm_step` since the last hb) | every 1 s |
| poll.slow | us, state, n (slow steps since the last line) | a `pd_sm_step` took > 2000 µs; at most 1 line per second |
| sm.qfull | on (the dropped event), drops (total so far) | a post found the 16-entry queue full; the event is dropped |
| sm | from, to, on (event name without `EV_`, e.g. `RX_SRC_CAPS`) | every real state change (not SAME). It is logged **before** the row's action runs, so action logs (`pd.tx` …) follow the transition that caused them. |
| sm.unhandled | state, on | no row matched |
| vbus | on (bool) | driver VBUS edge |
| cc | cc (1\|2), lvl1, lvl2 | Attached entry |
| cc.retry | lvl1, lvl2, n1, n2 | debounce did not qualify |
| pd.cfg | sw0, sw1 (hex) | Attached entry |
| pd.rx | type, id, ndo, rev, hdr, cls, obj (array of hex strings, omitted if NDO 0), sop (only when ≠ SOP) | every non-GoodCRC, non-dup frame |
| pd.caps | n, kind (e.g. `"FFFFA"`), mv (array), ma (array) | SRC_CAPS decoded; for non-fixed PDOs, mv is the max voltage and ma the max current |
| pd.caps.bad | hdr, obj0 | PDO1 not fixed |
| pd.sel | want, pos, mv, ma, why (`exact\|below\|min\|fallback`) | EvalCap entry |
| pd.tx | type, id, hdr, obj (array, omitted if NDO 0), bytes (the whole burst) | `drv_tx` |
| pd.txres | res (`sent\|gcrc\|tmo\|fail\|coll`), id, plus `coll:true` or `i2c:true` on those fail causes | TX result |
| pd.gcrc | id, match (bool) | every GoodCRC frame read from the FIFO |
| pd.dup | type, id | duplicate dropped |
| pd.rxraw | tok | bad token, RX flushed |
| pd.rej | mv | Reject without a contract |
| pd.sr | dir (`tx\|rx`) | soft reset path |
| pd.hr | dir (`tx\|rx`), n (hr_count, tx only) | HR sent or received |
| pd.giveup | n | Disabled entry |
| pd.want | mv | a WANT event was processed |
| contract | mv, ma, pos | PS_RDY in TransSink |
| contract.lost | why (`detach\|hardreset\|giveup`) | contract cleared |

Rules:

- No other `ev` names without a spec update.
- `pd.gcrc match`: while TX is pending, the id equals `tx_id`. Once TX_SENT has already resolved the message, the id equals the last `tx_id`, and the line is logged only.
- Extra fields are allowed.
- Log after the I2C action, so a failed write shows as `i2c.err` before it.
- `LOG_LINE_MAX` is 768. `pd.rx`, with 7 objects, is well under that.

---

## 8. Host test cases (`test/host`)

### 8.1 Required fake-port extensions (before the tests below can pass)

| # | Extension |
|---|---|
| F1 | **Per-CC BC_LVL.** `fake.cc_lvl[1..2]`. Status0 bits 1:0 reads return `cc_lvl[1]` if Switches0 has MEAS_CC1 set, `cc_lvl[2]` if MEAS_CC2, and 0 otherwise. New op `SCN_SETCC(lvl1, lvl2)`. |
| F2 | **RX_EMPTY tracking.** Status1.RX_EMPTY is set when the RX buffer is empty, both after reads drain it and at reset. Today it only clears on push. |
| F3 | **Flush bits.** A Control0 write with TX_FLUSH clears the TX FIFO; the bit reads back as 0. A Control1 write with RX_FLUSH clears the RX buffer and sets RX_EMPTY; the bit reads back as 0. |
| F4 | **Reset register.** SW_RES restores all reset values. PD_RESET is recorded only. Both read back as 0. |
| F5 | **TX capture.** Each burst (a contiguous write to 0x43) is stored with its tick. New ops: `SCN_EXPECT_TX(bytes, n)`, which compares the last burst, and `SCN_EXPECT_NTX(count)`. |
| F6 | **Auto-ack mode.** New op `SCN_ACK(mode, delay_ticks)`. Mode `none` does nothing. `txsent` sets Interrupta.I_TXSENT. `gcrc` pushes a GoodCRC frame with the burst's ID. `both` does both. `retryfail` sets I_RETRYFAIL. `coll` sets I_COLLISION once, then acts as `both`. The default is `both` with delay 1. |
| F7 | **Tick-stamped log capture** so a test can assert order and timing: `SCN_EXPECT_LOG_ORDER(a, b)` and `SCN_EXPECT_TX_WITHIN(ticks_since_last_rxpush)`. |

The existing ops cover everything else: SETREG/SETBITS for Status0.VBUSOK and the interrupt bits, RXPUSH, WANT, EXPECT_STATE, EXPECT_LOG and EXPECT_REG. The `bytes[64]` limit is enough, since the largest frame (NDO 7) is 35 bytes.

### 8.2 Common prologue: "attach CC1"

1. Boot.
2. `SETCC(2,0)`, then set VBUSOK.
3. Run 120 ticks.
4. Expect WaitCaps, Switches0 = 0x07 and Switches1 = 0x25.

### 8.3 Cases

| # | Name | Stimulus | Expect |
|---|---|---|---|
| T01 | boot_init | boot, no VBUS | Power 0x0F, Control3 0x07, Switches1 0x20, Switches0 0x03; state Unattached; log `sm.init` |
| T02 | attach_cc1 | prologue | `cc` cc=1; `pd.cfg` sw0 0x07 sw1 0x25; path Unattached→AttachWait→Attached→Startup→Discovery→WaitCaps |
| T03 | attach_cc2 | SETCC(0,3) | Switches0 0x0B, Switches1 0x26, `cc` cc=2 |
| T04 | cc_both_retry | SETCC(2,2) for 250 ticks, then SETCC(2,0) | ≥2 `cc.retry`, no Attached while both are set; attaches after the fix |
| T05 | cc_activity_skip | ACTIVITY set in Status0 during debounce | no sample counted while it is set; attach is delayed accordingly |
| T06 | detach_in_attachwait | clear VBUSOK at tick 50 | Unattached, no `cc` |
| T07 | happy_5v | prologue, push D1 | Request burst V2 in the same step (EXPECT_TX_WITHIN 2); after GoodCRC → `pd.txres res=gcrc` (or sent); push Accept `0x03A3` → TransSink; push PS_RDY `0x05A6` → Ready; `contract mv=5000 pos=1`; hb mv=5000 |
| T08 | want_9v_preattach | WANT 9000 before the prologue, push D1 | burst V1 exactly; contract mv=9000 |
| T09 | want_9v_in_ready | T07, then WANT 9000 | EvalCap→SelectCap, burst V3 (id 1), Accept/PS_RDY → contract mv=9000 |
| T10 | select_below_min | caps D1; WANT 12000 → pos 2; WANT 3000 → pos 1 | `pd.sel why=below` / `why=min` |
| T11 | apdo_ignored | caps D1 plus APDO, WANT 21000 | pos 4, ma 3000, RDO 0x4004B12C, `pd.caps kind=FFFFA` |
| T12 | caps_r2 | push the R2 header 0x2161 with 2 PDOs | decoded, rev=1, Request sent |
| T13 | reject_no_contract | Reject to a 9 V request | WaitCaps, `pd.rej mv=9000`; next caps → `pd.sel why=fallback` pos 1 |
| T14 | reject_with_contract | T07 contract, WANT 9000, Reject | Ready, contract unchanged, no new `contract` line |
| T15 | wait_with_contract | T07, WANT 9000, Wait | Ready; after 100 ticks a new Request (SINKREQ → EvalCap) |
| T16 | sender_response_tmo | Request GoodCRC'd, no Accept | at +30 ticks: HardReset, Control3 written 0x47, `pd.hr dir=tx n=1` |
| T17 | ps_transition_tmo | Accept, no PS_RDY | at +550: HardReset |
| T18 | waitcaps_tmo_hr_cycle | prologue, no caps | at +600: HardReset; set I_HARDSENT → TransDefault (Control3 back to 0x07, Reset 0x02); clear VBUSOK → Startup→Discovery; set VBUSOK → WaitCaps |
| T19 | hr_exhaust | repeat T18 without caps | exactly 3 `pd.hr dir=tx` (n=1,2,3), then `pd.giveup n=3`, state Disabled; then clear VBUSOK → Unattached; reattach → the next HR has n=1 |
| T20 | hr_received | T07 contract, set I_HARDRST | TransDefault, `pd.hr dir=rx`, `contract.lost why=hardreset`; hr_count unchanged |
| T21 | soft_reset_rx | T07, push Soft_Reset `0x01AD` | burst V5 in the same step; after TX_SENT → WaitCaps; then push D1 **with id 0** → accepted (no `pd.dup`), Request id 1 |
| T22 | dup_dropped | push D1 twice (same id 0) in WaitCaps | 1 Request, 1 `pd.dup` |
| T23 | tx_fail_soft_reset | ACK retryfail on the Request | SendSoftReset, burst V4; push Accept → WaitCaps |
| T24 | soft_reset_fail_hr | as T23, but no Accept to the Soft_Reset | +30 → HardReset |
| T25 | get_sink_cap | T07, push Get_Sink_Cap `0x07A8` | burst of Sink_Caps with id 1 (V7 bytes with hdr `0x1244`); → Ready |
| T26 | unsupported_reject | T07, push DR_Swap `0x07A9` | Reject burst, state stays Ready |
| T27 | unexpected_soft_reset | T07, push an Accept in Ready | SendSoftReset |
| T28 | caps_in_transsink | Accept, then push D1 | HardReset |
| T29 | detach_in_ready | T07, clear VBUSOK | Unattached, `contract.lost why=detach`, Switches1 0x20 |
| T30 | txres_variants | ACK modes `txsent`, `gcrc`, `none` | `pd.txres res=sent` / `gcrc` / `tmo` (at +10 ticks); exactly one TX_SENT each; tx_msgid increments once |
| T31 | gcrc_mismatch | GoodCRC with the wrong id | `pd.gcrc match=false`, no TX_SENT from it |
| T32 | fifo_two_msgs | push GoodCRC + Accept + PS_RDY in one tick | processed over consecutive steps, in order; ends in Ready |
| T33 | bad_token | push `55 …` | Control1 0x04 written, `pd.rxraw tok="0x55"`, no state change |
| T34 | collision_resend | ACK coll | burst written twice, `pd.txres res=coll`, then success |
| T35 | ping_ignored | Ping in Ready and in TransSink | no state change, no `sm.unhandled` |
| T36 | transdefault_no_vbus_drop | HR sent, VBUS stays | +1000 → Startup→Discovery→WaitCaps |
| T37 | discovery_timeout | HR sent, VBUS drops and never returns | +2500 in Discovery → Unattached |
| T38 | codec_vectors | pure L2 unit test | V1–V8 byte-exact, the D1 decode, and the selection vectors in §6.5 |

**38 test cases.** T38 is a pure codec test. T01–T37 are scenario tests.

---

## 9. Bench acceptance (M4 / M5 / M6)

The bench procedure (mr-bench):

1. Charger off for ≥ 25 s (via the smart plug).
2. Start the capture.
3. Charger on. Attach follows about 1 s later.
4. Commands go through the console (`pd want …`).

Without a contract, the source issues its own HR after about 32 s (bench fact), so every milestone must reach its goal well before that. Every check below is a `bench/q` command against the run (`--run last` by default).

### M4: RX alive

The first SRC_CAPS is decoded on the board.

| Check | Command | Pass |
|---|---|---|
| attached | `bench/q --ev sm --where to=WaitCaps --count` | ≥ 1 |
| caps decoded | `bench/q --ev pd.caps --fields t,n,kind,mv,ma` | ≥ 1 line, `mv[0] = 5000` |
| our auto-GoodCRC works (the source does not retry the same caps) | `bench/q --ev pd.dup --count` | 0 within the first 2 s after the first `pd.caps` |
| no RX garbage | `bench/q --ev pd.rxraw --count` | 0 |
| no spec gap hit | `bench/q --ev sm.unhandled --table` | no `RX_SRC_CAPS` rows |

### M5: 5 V contract, twice, over a replug

`want` is 5000 (the default). The run contains 2 charger off/on cycles.

| Check | Command | Pass |
|---|---|---|
| contract per attach | `bench/q --ev contract --where mv=5000 --count` | ≥ 2 |
| request latency | `bench/q --ev pd.rx --type Source_Capabilities --fields t` against `bench/q --ev pd.tx --type Request --fields t` | each Request ≤ 10 ms after its caps |
| clean handshake | `bench/q --ev pd.txres --where res=fail --count` / `--where res=tmo` | both 0 |
| no resets on the happy path | `bench/q --ev pd.hr,pd.sr --count` | 0 (a source-side `pd.hr dir=rx` more than 32 s after the last `contract.lost` is allowed) |
| detach seen between contracts | `bench/q --ev contract.lost --where why=detach --count` | ≥ 1 |
| steady | `bench/q --ev hb --where state=Ready --tail 3` | last hb lines are in Ready with mv=5000 |

### M6: requested voltage

The run must include both variants below: `pd want 9000` after the 5 V contract, and a fresh attach with `pd want 9000` given **before** the charger is switched on. The same checks apply with 12000 or 15000 against the charger's actual PDO list.

| Check | Command | Pass |
|---|---|---|
| selection | `bench/q --ev pd.sel --where want=9000 --fields t,pos,mv,why` | `mv=9000 why=exact` (or `why=below` with the highest offered fixed voltage ≤ want) |
| request sent | `bench/q --ev pd.tx --where type=Request --fields t,obj,bytes` | obj matches §6.3 for the selected pos (e.g. `0x2004B12C` if 9 V is pos 2) |
| contract | `bench/q --ev contract --fields t,mv,ma,pos` | last line has `mv` = the selected mv |
| holds | `bench/q --ev hb --tail 1 --fields state,mv` and `bench/q --ev pd.hr --since <contract t>` | Ready with mv = selected; no HR in the 60 s after the contract |
| back down | `pd want 5000` → `bench/q --ev contract --tail 1` | mv=5000 |

---

## 10. Open questions

Each question has a default, which is what the firmware implements now, and a bench check that confirms or refutes it.

| # | Question | Default (implemented) | Bench confirmation |
|---|---|---|---|
| Q1 | Do masked interrupts still latch? (DS is ambiguous) | Irrelevant: all masks stay 0x00; RX is driven by Status1.RX_EMPTY, not I_CRC_CHK | M4 passes with masks 0x00 (`regs` dump shows 0x0A/0x0E/0x0F = 00) |
| Q2 | Does I_TXSENT fire only after the partner's GoodCRC with the matching ID? | TX success = the first of I_TXSENT or a matching GoodCRC in the FIFO; after 10 ms with neither, assume success (`res=tmo`) | `bench/q --ev pd.txres --count` by res: `sent`/`gcrc` present, `tmo` = 0. If `tmo` > 0, revisit (and see Q11) |
| Q3 | Power 0x0F or 0x07 (K:init A3; the internal oscillator is only needed for toggle)? | 0x0F | M4 passes; `regs` shows Power=0x0F. If RX is dead, try 0x07 in a single A/B run |
| Q4 | VBUSOK threshold, 4.0 V (DS L868) vs 4.5 V (DS L1595); is a debounce needed? | Use VBUSOK as it is, with no debounce | `bench/q --ev vbus` shows exactly one on per attach and one off per detach/HR, with no chatter (no on/off pairs < 50 ms apart) |
| Q5 | Is a delay needed after SW_RES? | None beyond the next I2C transaction | `fusb.id` is read correctly after the reset; the first `regs` dump matches §3.0 values |
| Q6 | tCCDebounce, vRd and BC_LVL settle timing (Type-C spec not local) | Debounce 100 ms, settle 5 ms, ≥ 2 equal samples per CC | `cc` line lvl values are consistent across attaches; `cc.retry` count ≤ 1 per attach; attach ≤ 300 ms after `vbus on` |
| Q7 | Does Control3.SEND_HARD_RESET self-clear? | Rewrite Control3 = 0x07 in TransDefault entry anyway | `regs` after an HR shows Control3=0x07; `pd.hr dir=tx` is followed by `vbus on=false` within 700 ms |
| Q8 | Reset hr_count on contract (PSE only says power cycle or detach)? | Yes (ENG), so one bad negotiation can't lock out a later one | Only visible under repeated failures: `pd.hr` n never exceeds 3 per attach; `pd.giveup` count = 0 in M5/M6 |
| Q9 | Capability Mismatch bit when want is not offered? | Always 0 (we accept the pick) | M6 with a want the charger doesn't offer (e.g. 12000 on a 5/9/15/20 charger): the Accept arrives and `contract mv=9000` |
| Q10 | Collision handling / tRetry (75 µs R2.0 vs 195 µs R3.2, HW-controlled) | One software resend, then TX_FAIL | `bench/q --ev pd.txres --where res=coll --count` ≈ 0; if > 0, each is followed by success |
| Q11 | Is our own auto-GoodCRC ever visible in our RX FIFO? (K:fifo says no) | No: every GoodCRC in the FIFO is the partner's | `pd.gcrc match=false` count = 0 in M4–M6 |
| Q12 | Is PD_RESET needed on soft reset (for the chip's internal MessageID)? | No: software `proto_reset()` only | After `pd.sr`, the next Source_Capabilities is decoded (no `pd.dup`) and the Request is accepted |
| Q13 | Attach gated on VBUS before CC debounce (a deviation from Type-C Unattached.SNK, which starts on Rp) | Yes: VBUS_ON triggers AttachWait | `vbus on=true` precedes the `cc` line on every attach; attach works on every M5 cycle |
| Q14 | Does an R3 source fall back to R2.0 after our R2.0 Request? | We always send SpecRev 01 and accept both | `bench/q --ev pd.rx --where type=Accept --fields rev` shows 1 (R2.0); 2 is also acceptable as long as M5 passes |
| Q15 | Is TransDefault 1000 ms / Discovery 2500 ms enough for the real charger's HR recovery? | Values from §5 (ENG from R32 timings) | After any `pd.hr`: `vbus on=false` then `on=true` then `sm to=WaitCaps`, all without `sm to=Unattached` via `on=TIMEOUT` |
| Q16 | Does the 5-byte burst read of 0x3E–0x42 clear the R/C interrupt registers the same way single reads do? | Yes (DS Figure 14, L693–717); burst on | M4/M5 pass with the burst. On failure (missed `pd.hr dir=rx` or TX results, `res=tmo` > 0), rebuild with `PD_DRV_BURST_POLL=0` and repeat the same run: the difference is the answer |
| Q17 | Does one poll step fit in 2 ms at 400 kHz I2C? | Burst poll plus 2-transaction RX. Idle step = 1 transaction | `bench/q --ev hb --fields poll_max_us --tail 20` is ≤ 2000 in the steady state; `bench/q --ev poll.slow --count` ≤ 1 per attach (the caps + Request step) |
