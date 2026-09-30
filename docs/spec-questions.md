# spec-questions — deviations and open questions against docs/spec.md

Owner: mr-engineer. Every entry states what the code does today, which is the spec default
wherever the spec states one. Answer by editing the spec; the code follows.

## Q1 — L1 driver lives in `fw/src/pd_drv.c`, not `fusb302.c` (§2 layer table)
`fusb302.c` stays the raw I2C HAL (ESP-IDF only; not built on the host). The poll/TX/RX
logic must run in the host sim, so it is in `pd_drv.{c,h}` (pure C, via `pd_port_ops`).
Responsibilities are exactly those in the L1 row. Question: update the table to `pd_drv.c`?

## Q2 — new ev `poll.slow` and hb field `poll_max_us` (§7 "no other ev names")
The phase-2 task asks for a warning event when a poll step exceeds 2 ms. `main.c` times every
`pd_sm_step` with `esp_timer`: `poll.slow {us,state,n}`, at most one line per second;
hb carries `poll_max_us` (max since the last hb). Please add both to §7.

## Q3 — ev `sm.qfull {on,drops}` is not in §7
The event queue is 16 deep (§3). If a post finds it full, the event is dropped and logged.
It was in the old README. Keep it, and add it to §7?

## Q4 — I2C budget per poll step at 400 kHz (§2.1)
Host sim, worst case over T01–T37: 14 transactions / 79 bus bytes in one step (Source_Caps RX
plus the Request TX in the same step). That is 1.8 ms on the wire plus the driver overhead per
transaction. Estimate: ~2.5 ms, so `poll.slow` may fire on that one step. An idle step is 5 single
register reads (~0.5–0.7 ms). Suggestion, not done: read 0x3E–0x42 in one burst (5→1 transactions).
The `poll.slow` line will give the real number on the board.

## Q5 — TX result details not spelled out in §2.1 step 4
- A GoodCRC read from the FIFO whose id matches `tx_id` while TX is still pending resolves it
  as `res:"gcrc"`, and logs `pd.gcrc match:true`. After TXSENT has already resolved it, the
  GoodCRC is only logged (`match` = id equals the last `tx_id`).
- Collision: the burst is re-written once, `pd.txres res:"coll"` is logged, and the 10 ms
  deadline restarts. A second collision gives `res:"fail",coll:true`.
- An I2C error while writing a burst gives `res:"fail",i2c:true`, and posts TX_FAIL.
- Deadline hit without any flag gives `res:"tmo"`, treated as sent (the spec default).

## Q6 — Battery PDO `ma` in `pd.caps` (§6.2 decode, §7 pd.caps)
The spec gives mV/mA decode for Fixed only. For Battery, `ma` = max power (mW) × 1000 / max mV.
Variable: 10 mA units. APDO: 50 mA units, mv from 100 mV units. These are log-only and never selected.

## Q7 — hex case
New fields use upper-case (`"0x4004B12C"`, `"sw0":"0x07"`) per §7. The old `log_hex8` fields
(`fusb.id`, `regs`, `cmd`, `i2c.err`) stay lower-case, because §7 says "unchanged". Unify?

## Q8 — small implementation choices (no behaviour question, listed for review)
- The RX loop reads at most 8 frames per poll. It stops after the first frame that posted a PE
  event. GoodCRC, dup and ignored frames continue. The first iteration reuses Status1 from the
  poll read.
- The `sm` line is logged before the row's action runs, so action logs (`pd.tx` …) follow the
  transition that caused them.
- `sm.init` has an extra `state` field (extra fields are allowed).
- `cc.retry` logs `-1` for a CC side that was never sampled (e.g. skipped while ACTIVITY is set).
- The VBUS_OFF detach action clears `have_contract` right after logging `contract.lost detach`.
  Without that, the Unattached entry would log it a second time.
- If one of the 5 poll reads fails, the step is abandoned. Interrupt bits already cleared by a
  read-to-clear in that step are lost. This is rare, and the `i2c.err` line shows it.
- Host fake: Status0 (VBUSOK/ACTIVITY = physical inputs) survives SW_RES. Everything else goes
  back to its reset value (F4).
