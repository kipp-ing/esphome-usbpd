# FUSB302 FIFO: RX/TX tokens, GoodCRC, retries, hard reset

Citation keys (DS/LNX/PSE/R32) are defined in [fusb302-registers.md](fusb302-registers.md) and [fusb302-sink-init.md](fusb302-sink-init.md).

## FIFO basics

| Fact | Value | Source |
|---|---|---|
| Register | 0x43. A write goes to the TxFIFO, a read comes from the RxFIFO. | DS T28 L1620-1629 |
| Address auto-increment | **none** on 0x43. A burst read or write stays on the FIFO. | DS L1068 |
| Sizes | TX 48 bytes, RX 80 bytes | DS L607-608, L1699-1700 |
| RX contents | packet data **plus the received CRC** | DS L597-599 |
| Oscillator | auto-enabled on CC activity; the FIFO is loaded when a packet arrives | DS L600-603 |
| RX events | I_ACTIVITY / I_CRC_CHK "alert that a valid packet was received" | DS L604-606 |
| Reserved bits | write 0 in TX; ignore in RX | DS L1646-1648 |
| Flushes | Control0.TX_FLUSH (b6), Control1.RX_FLUSH (b2), both self-clearing | DS T10 L1193, T11 L1214-1234 |

## RX: frame format in the RxFIFO

Each received packet occupies:

| Byte(s) | Content | Source |
|---|---|---|
| 1 | SOP token: top 3 bits. `111` = SOP, `110` = SOP', `101` = SOP'', `100` = SOP'_DBG, `011` = SOP''_DBG, `010`/`001`/`000` = do not use. The low 5 bits are undefined, so **mask with 0xE0**. | DS T30 L1698-1722 |
| 2 | header low byte (header bits 7..0) | LNX L1431-1482 (reads 2 bytes into a LE u16) |
| 3 | header high byte (bits 15..8) | same |
| 4 .. 3+4·NDO | NDO data objects, each 4 bytes **little-endian** | LNX L1431-1482; NDO = header b14..12 (PSE L589-607) |
| +4 | CRC-32 bytes (already checked by HW; discard) | LNX L1431-1482; DS L597-599 |

- Total bytes per packet = 1 + 2 + 4·NDO + 4. A control message is 7 bytes. Source_Caps with 7 PDOs is 35 bytes, so two worst-case packets fit in 80 bytes.
- **SOP' / SOP'' frames only appear if ENSOP1/ENSOP2 = 1** (DS T30). A sink leaves them 0, so every token should be 0xE0-class.
  - A token that is not `111xxxxx` means a desync. **RX_FLUSH and resync.**

### RX poll algorithm (polling, no INT)

```
loop every <= ~2-5 ms (see pd-sink-policy tSenderResponse budget):
  ia  = read 0x3E   (Interrupta, R/C)   -> I_HARDRST, I_SOFTRST, I_TXSENT, I_RETRYFAIL, I_HARDSENT
  ib  = read 0x3F   (Interruptb, R/C)   -> I_GCRCSENT
  i   = read 0x42   (Interrupt, R/C)    -> I_COLLISION, I_CRC_CHK, I_ACTIVITY, I_VBUSOK
  if ia & I_HARDRST: handle hard reset (below); continue
  while (read 0x41 & RX_EMPTY(0x20)) == 0:
      tok = read 0x43;               if (tok & 0xE0) != 0xE0 -> RX_FLUSH, break
      h   = read 0x43, 0x43 (2 bytes, LE)
      n   = (h >> 12) & 7
      payload = read 4*n bytes from 0x43
      crc = read 4 bytes from 0x43   (discard)
      if n == 0 and type(h) == GoodCRC: this acknowledges OUR last TX (see TX below)
      else: pass the message to the policy engine
```

- Driving RX from **Status1.RX_EMPTY** (a level) instead of I_GCRCSENT (a latch) makes polling robust. It also survives out-of-order events: Linux notes that TX_SUCCESS and GCRCSENT may be reported out of order and decides by message type, not by event (LNX L1431-1482, comment).
- The Linux event→action mapping (LNX L1502-1635):
  - I_GCRCSENT → read one message (L1623-1631)
  - I_TXSENT → read one message, which is the partner's GoodCRC (L1603-1611)
  - I_HARDRST → PD_RESET, then HR handling (L1613-1621)
  - I_HARDSENT → PD_RESET (L1593-1601)
  - I_COLLISION or I_RETRYFAIL → TX failed (L1583-1591)
- **The GoodCRC our chip auto-sends does not appear in the RxFIFO.** The partner's GoodCRC for our TX **does** appear, as a 7-byte packet with NDO = 0 and type 1 (LNX L1431-1482, L1603-1611).
- Every I2C transaction costs time. Linux reads the payload as one block read (LNX L1431-1482). A burst read at 0x43 is allowed because there is no auto-increment (DS L1068).

## Auto GoodCRC (RX acknowledge)

| Fact | Source |
|---|---|
| HW computes the CRC. If it is good, I_CRC_CHK fires. If **AUTO_CRC = 1 and AUTO_PRE = 0**, the chip sends the GoodCRC automatically. | DS L642-653 |
| The GoodCRC header uses Switches1 POWERROLE/SPECREV/DATAROLE plus the received MessageID | DS T7 L1108-1137 |
| The GoodCRC must be sent within tTransmit (195 µs) of the received EOP. HW meets this; SW cannot. | PSE L715-721, Table 6-32 L1995-2050; DS L935-939 |
| Event on success: Interruptb.I_GCRCSENT | DS T24 L1508-1516 |
| "If GoodCRC is required for anything beyond SOP, enable SOP*" (ENSOPx) | DS L652-653 |
| Duplicate MessageID: the receiver still sends GoodCRC but drops the message. The chip always GoodCRCs; **SW must drop duplicates.** | PSE L2090-2121 |

## TX: token sequence for a message

Written as **one contiguous burst to 0x43** (LNX L964-1011; DS L1651-1659 prefers "data first, then TXON"):

| Order | Token | Byte | Source |
|---|---|---|---|
| 1-3 | SOP1 (Sync-1) ×3 | 0x12 0x12 0x12 | DS T29 L1660-1662; LNX L964-1011 |
| 4 | SOP2 (Sync-2) | 0x13 | DS L1663-1665 |
| 5 | PACKSYM \| N, where N = 2 + 4·NDO (2 ≤ N ≤ 30) | 0x80+N | DS L1675-1681 |
| 6.. | N bytes: header LE, then data objects LE | — | DS L1682-1683 (the LSB nibble is sent first); LNX L964-1011 |
| next | JAM_CRC | 0xFF | DS L1684-1686 |
| next | EOP | 0x14 | DS L1687-1689 |
| next | TXOFF | 0xFE | DS L1690 |
| last | TXON (start; exactly 0xA1) | 0xA1 | DS L1651-1659 |

- SOP = Sync-1, Sync-1, Sync-1, Sync-2 (the PD K-code ordered set). SOP' / SOP'' use other combinations with Sync-3 (0x1B) and are not needed.
- Hard reset is RESET1 ×3 + RESET2 (DS L678-679).
- The total for a Request (NDO = 1) is 4 + 1 + 6 + 4 = 15 bytes, below the 48-byte TX FIFO.
- **Worked Request (9 V/3 A, PDO #2, MessageID 0, SpecRev 01)** — derivation in [pd-messages.md](pd-messages.md):
  `12 12 12 13 86 42 10 2C B1 04 20 FF 14 FE A1`
- The preamble is generated by HW (DS L1210-1212: "Preamble is started first").

### TX completion events

| Event | Meaning | SW action | Source |
|---|---|---|---|
| Interrupta.I_TXSENT | the packet was sent and "acknowledged with a GoodCRC response packet" (DS L1502-1504). The DS does not say whether the chip checks the GoodCRC's MessageID (UNCERTAIN); SW can check it in the GoodCRC read from the RxFIFO. | **increment MessageID** (mod 8); start the sender response timer; read the GoodCRC out of the RxFIFO | DS T23 L1484-1506; PSE L615-621, L1609-1622; LNX L1603-1611 |
| Interrupta.I_RETRYFAIL | no GoodCRC after N_RETRIES retries (each retry is sent tRetry after tReceive expires) | TX failed → PE Soft Reset path (or the chip's AUTO_SOFTRESET) | DS L668-677; LNX L1588; PSE L2122-2126 |
| Interrupt.I_COLLISION | CC busy at TX start, so not sent. **The incoming packet is received normally.** | read RX first, then decide (a new Source_Caps overrides) | DS T27 L1615-1617; LNX L1583 |
| Status0a.RETRYFAIL/SOFTFAIL | status copy; cleared by TXON/START_TX/SEND_HARD_RESET | — | DS T21 L1434-1439 |

Retry and soft-reset behaviour:

| Mechanism | Detail | Source |
|---|---|---|
| Auto retry | if AUTO_RETRY = 1 and no GoodCRC arrives within tReceive (0.9-1.1 ms), the same FIFO content is resent after tRetry, N_RETRIES times | DS L668-673, L924-931 |
| Auto soft reset (Control3.AUTO_SOFTRESET) | when all retries fail, the chip sends a Soft_Reset with MessageID 0, retried N_RETRIES times. Success → I_TXSENT; failure → I_SOFTFAIL. SW must set its MessageIDCounter to 0. | DS L666-685 |
| Auto hard reset (AUTO_HARDRESET) | after SOFTFAIL, one Hard Reset is sent. The chip does **not** retry hard resets; the PE does. | DS L675-683 |
| Recommendation | leave AUTO_SOFTRESET = AUTO_HARDRESET = 0 (the reset value) and implement Soft_Reset / Hard Reset in the PE, as Linux does (Linux only sets N_RETRIES + AUTO_RETRY, LNX L350-358) | LNX L350-358 |

- nRetryCount: PD2.0 = 3, PD3.x = 2 (PSE Table 6-34 L2172-2185; R32 Table 7.11 L16000; LNX L1030-1066).

## Hard reset

| Direction | How | Events | After | Source |
|---|---|---|---|---|
| Send | Control3 \|= SEND_HARD_RESET (b6, 0x40) — "highest priority" | Interrupta.I_HARDSENT | PD_RESET (0x0C = 0x02), flush FIFOs, MessageID = 0, PE_SNK_Transition_to_default | DS T13 L1278-1280; LNX L1013-1017, L1593-1601 |
| Receive | the HW detects the RESET1/RESET2 ordered set | Interrupta.I_HARDRST (+ Status0a.HARDRST) | PD_RESET, flush, MessageID = 0, wait for VBUS to drop and return, then Wait_for_Caps | DS T21/T23; LNX L1613-1621; PSE L2286-2312 |

- The hard reset ordered set is RESET1, RESET1, RESET1, RESET2. The chip sends it once (DS L678-683).
- tHardReset = 5 ms and tHardResetComplete = 4-5 ms (DS L916-923; PSE Table 6-32).
- Manual alternative: write the tokens `15 15 15 16` followed by TXON. Not needed, because SEND_HARD_RESET does it.
- A Soft_Reset **received** also sets Interrupta.I_SOFTRST (DS T23). The PE must answer with Accept, MessageID 0 (PSE L2202-2224, L4181).
