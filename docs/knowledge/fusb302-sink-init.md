# FUSB302 sink init sequence (polling, no INT line)

Citation keys: DS/LNX/LNXH as in [fusb302-registers.md](fusb302-registers.md).

- **PSE L<n>** = `/Users/jan/dev/repos/PCB-pi-r0/docs/spec/usb-pd/pd-sink-essentials.txt` (verbatim USB-PD R2.0 v1.3 excerpts)
- **R32 L<n>** = `/Users/jan/dev/repos/PCB-pi-r0/docs/spec/usb-pd/usb-pd-spec-r3.2-v1.2.txt`

## 0. Linux reference order (what a known-good driver does)

| # | Linux step | Registers touched | Source |
|---|---|---|---|
| 1 | `fusb302_sw_reset` | Reset = SW_RES (0x01) | LNX L336-348, L395-424 |
| 2 | `enable_tx_auto_retries(N_RETRIES_3)` | Control3 \|= 0x07 | LNX L350-358 |
| 3 | `init_interrupt` | Mask = 0xFF & ~M_VBUSOK (0x7F), Maska = 0xFF, Maskb = 0xFF, Control0 &= ~INT_MASK | LNX L364-384 |
| 4 | `set_power_mode(PWR_ALL)` | Power = 0x0F | LNX L395-424, LNXH L86 |
| 5 | read Status0 (VBUSOK), read DeviceID | — | LNX L395-424 |
| 6 | tcpm core: `set_cc(RD)` → toggling off, Switches0 PDWN1\|PDWN2 | Control2 TOGGLE = 0, Switches0 = 0x03 | LNX L600-704 |
| 7 | tcpm core: `start_toggling(SNK)` → Control2 MODE = SNK (10), TOGGLE = 1 | Control2 | LNX L500-580 |
| 8 | I_TOGDONE: Status1a.TOGSS 101/110 → `set_cc_polarity_and_pull` | Switches0 = PDWN1\|PDWN2\|MEAS_CCx; Switches1 TXCCx only | LNX L1189-1249, L1148-1187 |
| 9 | read BC_LVL on the active CC. If 00 → restart toggle. Else toggle off. | Status0 | LNX L1189-1249 |
| 10 | tcpm core: `set_roles(sink, UFP)` | Switches1 POWERROLE = 0, DATAROLE = 0 (SPECREV untouched = 01) | LNX L892-921 |
| 11 | tcpm core: `set_pd_rx(on)` → RX_FLUSH, TX_FLUSH, AUTO_CRC = 1, unmask PD interrupts | Control1, Control0, Switches1, Mask/Maska/Maskb | LNX L839-880, L792-837 |
| 12 | Hard Reset received: `pd_reset` then tcpm hard-reset handling | Reset = PD_RESET | LNX L1613-1621, L1425-1429 |

- Linux uses an INT pin (level-low IRQ, LNX L1770-1772). We poll, so masks do not matter the same way. See §3.
- Linux never writes Slice, Measure (in sink mode), OCPreg or Control4.

## 1. Recommended polling sequence (derived from DS + LNX)

Each step lists its rationale and source.

### A. Reset and power

| Step | Write/read | Why | Source |
|---|---|---|---|
| A1 | read 0x01 DeviceID. It must not be 0x00/0xFF and must not NAK. | I2C alive | DS T5 L1076-1090 |
| A2 | write 0x0C = 0x01 (SW_RES) | all registers to reset values | DS T16 L1348-1359; LNX L336-348 |
| A3 | write 0x0B Power = 0x0F | PWR0 bandgap, PWR1 **receiver** + current refs, PWR2 measure (needed for BC_LVL/VBUSOK), PWR3 oscillator | DS T15 L1334-1346; LNX L395-424 |
| A4 | write 0x09 Control3 = 0x07 (N_RETRIES = 3, AUTO_RETRY = 1). For PD3.0-only operation use 0x05 (N_RETRIES = 2). | HW retry when no GoodCRC arrives | DS T13 L1272-1296; LNX L350-358, L1030-1066 |
| A5 | leave Mask/Maska/Maskb = 0x00. Read 0x42, 0x3E, 0x3F once to clear. | latch everything for polling (see §3) | DS T14/18/19/27 |

- The DS gives no mandated delay after SW_RES. Linux has none.
- **Engineering choice (not sourced):** wait ≥ 1 ms after SW_RES and after Power = 0x0F before relying on measurements.

### B. Attach and CC orientation (manual, no toggle)

| Step | Write/read | Why | Source |
|---|---|---|---|
| B1 | Switches0 = 0x03 (PDWN1\|PDWN2; the reset value). Control2 TOGGLE = 0 (reset 0x02 is already TOGGLE = 0). | present Rd on both CCs = sink | DS T6 L1091-1107, T12 L1242-1270; LNX L600-704 |
| B2 | wait for Status0.VBUSOK = 1 | the source applies vSafe5V only after it sees Rd | DS T25 L1529-1531, L868-870; DS "Manual Device Detection" L356-369 |
| B3 | Switches0 = 0x07 (MEAS_CC1). Wait. Read Status0.BC_LVL → lvl1. | measure CC1 | DS T25 L1555-1565 |
| B4 | Switches0 = 0x0B (MEAS_CC2). Wait. Read Status0.BC_LVL → lvl2. | measure CC2 | same |
| B5 | active CC = the one with BC_LVL ≠ 00 (the Rp side). The other side reads 00 (open, or Ra of a cable). If both are 00, no source; retry. Re-read when Status0.ACTIVITY = 1. | orientation | DS L1561-1565; LNX L1079-1125 (ACTIVITY → 30 ms defer, L45) |
| B6 | Switches0 = 0x07 (CC1) or 0x0B (CC2): MEAS **stays** on the active CC | the PD receiver listens on the MEAS-selected CC | LNX L1148-1187 |

- BC_LVL → Rp advertisement: 01 Default USB, 10 1.5 A, 11 3.0 A (DS L857-861, L1557-1560).
- **Alternative: HW toggle.** Set Control2 MODE = 10 (SNK) and TOGGLE = 1, then poll Interrupta.I_TOGDONE and read Status1a.TOGSS (101 = CC1, 110 = CC2) (DS L372-384, T22 L1450-1476; LNX L500-580, L1189-1249).
  - The DS contradicts itself on PWR during toggle: Table 1 says PWR = 0x07 (L371), the text says PWR = 0x01 (L382). After TOGDONE, set PWR = 0x0F.
  - The manual method is simpler for a sink-only design.
- Type-C tCCDebounce (100-200 ms) comes from the Type-C spec. **That spec is not local (GAP).** Linux uses only 30 ms plus the toggle state machine (LNX L45).
- **Engineering choice:** after VBUSOK, wait ~100 ms before the orientation read. This keeps well inside tTypeCSinkWaitCap / tFirstSourceCap (see pd-sink-policy.md).

### C. PD configuration (order as in LNX set_polarity → set_roles → set_pd_rx)

| Step | Write | Value (CC1 / CC2) | Source |
|---|---|---|---|
| C1 | Switches1 (0x03) | **0x25 / 0x26** = SPECREV 01 \| AUTO_CRC \| TXCC1 or TXCC2; POWERROLE = 0, DATAROLE = 0 | DS T7 L1108-1137; LNX L804-811, L892-921, L1148-1187 |
| C2 | Control0 (0x06) | read-modify-write `|= 0x40` (TX_FLUSH, self-clearing). From reset this is 0x64. AUTO_PRE (b1) must stay 0. | DS T10 L1187-1212; LNX L792-796 |
| C3 | Control1 (0x07) | 0x04 = RX_FLUSH (ENSOP1/2 = 0: SOP only) | DS T11 L1214-1234; LNX L798-802 |
| C4 | Reset (0x0C) | 0x02 = PD_RESET (PD logic + internal MessageID state) | DS T16 L1348-1359; LNX L1425-1429 |
| C5 | clear interrupts | read 0x42, 0x3E, 0x3F | DS T23/24/27 |
| C6 | go to the RX poll loop | see fusb302-fifo.md | — |

- Linux does not issue PD_RESET at pd_rx on; it does so only on hard reset received/sent (LNX L1593-1601, L1613-1621).
- Doing PD_RESET once before listening is harmless: it touches only the PD logic (DS L1348-1359).
- **Timing budget:** the source sends Source_Capabilities after tFirstSourceCap (250 ms) from vSafe5V. It keeps re-sending every tTypeCSendSourceCap (100-200 ms) up to nCapsCount (50) times (PSE L1694-1697, L2135-2147, Table 6-32 L1995-2050).
  - If B takes longer, nothing is lost: the source keeps retrying.
  - If no sink GoodCRC arrives within ~tTypeCSinkWaitCap/nCaps, the source may give up and stay at 5 V Type-C current only.

## 2. After a Hard Reset (either direction)

| Event | Action | Source |
|---|---|---|
| Interrupta.I_HARDRST (received) | PD_RESET (0x0C = 0x02), RX_FLUSH, TX_FLUSH, MessageID counter = 0. The source drops VBUS to vSafe0V and restores it (tSrcRecover etc.). Re-enter PE_SNK_Transition_to_default → Startup. | LNX L1613-1621; PSE L2286-2312, L4040 |
| Interrupta.I_HARDSENT (our HR sent) | PD_RESET, then the same as above | LNX L1593-1601 |
| VBUSOK drops during HR | expected. **Do not** treat it as a detach while in the hard-reset flow (Type-C rule; GAP: Type-C spec not local). Keep Rd and the Switches config. | PSE L2286-2312 |

## 3. Masks when polling

- Recommendation: Mask = Maska = Maskb = 0x00 (reset). Poll Interrupt (0x42), Interrupta (0x3E) and Interruptb (0x3F). **Each read clears the bits.**
  - Read them into a variable once per loop and act on all bits.
  - Do not read the same register twice in a loop, or you lose events (DS T23/24/27: R/C).
- Control0.INT_MASK only gates the INT_N pin (DS L1194-1196). It is irrelevant without the pin.
- The DS does not say whether a masked event still latches. Keeping masks at 0 avoids the question (UNCERTAIN; see registers doc).
- **Robust fallback:** do not depend on interrupt bits for RX at all. Poll Status1.RX_EMPTY (0x41 b5) directly; it is a level, not a latch (DS T26 L1567-1585).

## 4. "No frames received" pitfalls (checklist)

| # | Symptom / cause | Fix | Source |
|---|---|---|---|
| P1 | Power left at reset 0x01 → receiver (PWR1) and oscillator (PWR3) off | Power = 0x0F | DS T15 L1334-1346; LNX L395-424 |
| P2 | MEAS_CCx not set, or set on the **wrong** CC → receiver listens on the idle pin | MEAS on the CC with BC_LVL ≠ 00 | LNX L1148-1187; DS L1561-1565 |
| P3 | Orientation measured during BMC traffic → BC_LVL garbage → wrong CC chosen | measure with ACTIVITY = 0; defer 30 ms | DS L1561-1562; LNX L1079-1125 |
| P4 | TXCCx not on the active CC → our GoodCRC and Request are never driven | TXCC on the same CC as MEAS | LNX L1148-1187 |
| P5 | AUTO_CRC = 0 → no GoodCRC. The source retries (nRetryCount), then Soft_Reset, then Hard Reset. Looks like "caps arrive repeatedly, then VBUS dips". | Switches1.AUTO_CRC = 1 | DS L647-653; PSE L2122-2134 |
| P6 | AUTO_PRE = 1 → the chip waits for SW to load a response instead of auto-GoodCRC | Control0.AUTO_PRE = 0 | DS L647-653, L1203-1209 |
| P7 | SPECREV set to 10/11 ("Do Not Use") → malformed GoodCRC header | SPECREV = 01 | DS T7 L1108-1137 |
| P8 | Reading 0x43 with a register auto-increment assumption, or reading 0x42 etc. twice | FIFO reads are sequential at 0x43; interrupt reads clear | DS L1068, T27 |
| P9 | Stale bytes in the RX FIFO after config/HR → misparsed first frame | RX_FLUSH + PD_RESET before listening and after every HR | LNX L839-880, L1613-1621 |
| P10 | Writing Switches0 with PDWN cleared while measuring → the source sees detach and drops VBUS | always keep PDWN1\|PDWN2 = 1 in every Switches0 write | DS T6; LNX L1148-1187 |
| P11 | SW_RES issued while attached (e.g. on each retry) → Rd briefly still on (reset 0x03), but PD state, AUTO_CRC and Power are lost | do SW_RES only at boot | DS T16 L1348-1359 |
| P12 | Polling too slowly to answer Source_Caps: the GoodCRC is HW, but the Request must be sent within tSenderResponse (24-30 ms; R3.2 nominal 27 ms) of the GoodCRC | poll ≤ ~5 ms while negotiating | PSE L1609-1622, Table 6-32 L1995-2050; R32 Table 7.9 L15757-15848 |
