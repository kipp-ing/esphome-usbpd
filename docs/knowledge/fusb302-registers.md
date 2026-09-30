# FUSB302 register map (sink-relevant)

Sources:
- **DS L<n>**: the FUSB302 datasheet text `/Users/jan/dev/repos/PCB-pi-r0/tmp/pdfs/fusb302mpx.txt` (onsemi FUSB302MPX). Line numbers refer to that file.
- **LNX** (`fusb302.c`): `https://github.com/torvalds/linux/blob/55311a92bc9564b058a036074f85200a5954ccd2/drivers/usb/typec/tcpm/fusb302.c#L<n>`
- **LNXH** (`fusb302_reg.h`): same commit, `drivers/usb/typec/tcpm/fusb302_reg.h#L<n>`.

## Bus

| Item | Value | Source |
|---|---|---|
| I2C 7-bit address | 0x22 (FUSB302MPX/BMPX). Other part suffixes use 0x23/0x24/0x25. | DS Table 4, L1021-1023 |
| I2C speed | up to 1 MHz (Fm+) | DS L694-697 |
| Auto-increment | Multi-byte reads and writes auto-increment the address, **except FIFOs 0x43**. Repeated reads or writes to 0x43 stay on the FIFO. | DS L1068, Table 28 L1620-1629 |
| Write rule | Do not write undefined registers. Write reserved bits only with their reset value. | DS L1066-1067 |

## Summary

| Addr | Name | R/W | Reset | b7 | b6 | b5 | b4 | b3 | b2 | b1 | b0 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 0x01 | Device ID | R | — | VER3 | VER2 | VER1 | VER0 | REV3 | REV2 | REV1 | REV0 |
| 0x02 | Switches0 | RW | 0x03 | PU_EN2 | PU_EN1 | VCONN_CC2 | VCONN_CC1 | MEAS_CC2 | MEAS_CC1 | PDWN2 | PDWN1 |
| 0x03 | Switches1 | RW | 0x20 | POWERROLE | SPECREV1 | SPECREV0 | DATAROLE | rsv | AUTO_CRC | TXCC2 | TXCC1 |
| 0x04 | Measure | RW | 0x31 | rsv | MEAS_VBUS | MDAC5 | MDAC4 | MDAC3 | MDAC2 | MDAC1 | MDAC0 |
| 0x05 | Slice | RW | 0x60 | SDAC_HYS1 | SDAC_HYS0 | SDAC5..0 → | | | | | |
| 0x06 | Control0 | RW | 0x24 | rsv | TX_FLUSH | INT_MASK | rsv | HOST_CUR1 | HOST_CUR0 | AUTO_PRE | TX_START |
| 0x07 | Control1 | RW | 0x00 | rsv | ENSOP2DB | ENSOP1DB | BIST_MODE2 | rsv | RX_FLUSH | ENSOP2 | ENSOP1 |
| 0x08 | Control2 | RW | 0x02 | TOG_SAVE_PWR2 | TOG_SAVE_PWR1 | TOG_RD_ONLY | rsv | WAKE_EN | MODE1 | MODE0 | TOGGLE |
| 0x09 | Control3 | RW | 0x06 | rsv | SEND_HARD_RESET | rsv | AUTO_HARDRESET | AUTO_SOFTRESET | N_RETRIES1 | N_RETRIES0 | AUTO_RETRY |
| 0x0A | Mask | RW | 0x00 | M_VBUSOK | M_ACTIVITY | M_COMP_CHNG | M_CRC_CHK | M_ALERT | M_WAKE | M_COLLISION | M_BC_LVL |
| 0x0B | Power | RW | 0x01 | rsv | rsv | rsv | rsv | PWR3 | PWR2 | PWR1 | PWR0 |
| 0x0C | Reset | W/C | 0x00 | rsv | rsv | rsv | rsv | rsv | rsv | PD_RESET | SW_RES |
| 0x0D | OCPreg | RW | 0x0F | rsv | rsv | rsv | rsv | OCP_RANGE | OCP_CUR2 | OCP_CUR1 | OCP_CUR0 |
| 0x0E | Maska | RW | 0x00 | M_OCP_TEMP | M_TOGDONE | M_SOFTFAIL | M_RETRYFAIL | M_HARDSENT | M_TXSENT | M_SOFTRST | M_HARDRST |
| 0x0F | Maskb | RW | 0x00 | rsv ×7 | | | | | | | M_GCRCSENT |
| 0x10 | Control4 | RW | 0x00 | rsv ×7 | | | | | | | TOG_USRC_EXIT |
| 0x3C | Status0a | R | — | rsv | rsv | SOFTFAIL | RETRYFAIL | POWER3 | POWER2 | SOFTRST | HARDRST |
| 0x3D | Status1a | R | — | rsv | rsv | TOGSS3 | TOGSS2 | TOGSS1 | RXSOP2DB | RXSOP1DB | RXSOP |
| 0x3E | Interrupta | R/C | — | I_OCP_TEMP | I_TOGDONE | I_SOFTFAIL | I_RETRYFAIL | I_HARDSENT | I_TXSENT | I_SOFTRST | I_HARDRST |
| 0x3F | Interruptb | R/C | — | rsv ×7 | | | | | | | I_GCRCSENT |
| 0x40 | Status0 | R | — | VBUSOK | ACTIVITY | COMP | CRC_CHK | ALERT | WAKE | BC_LVL1 | BC_LVL0 |
| 0x41 | Status1 | R | 0x28 | RXSOP2 | RXSOP1 | RX_EMPTY | RX_FULL | TX_EMPTY | TX_FULL | OVRTEMP | OCP |
| 0x42 | Interrupt | R/C | — | I_VBUSOK | I_ACTIVITY | I_COMP_CHNG | I_CRC_CHK | I_ALERT | I_WAKE | I_COLLISION | I_BC_LVL |
| 0x43 | FIFOs | RW | — | TX write tokens / RX read bytes | | | | | | | |

The summary comes from DS register table L1030-1062. Per-register details follow below.

## Per-register detail

### 0x01 Device ID (DS Table 5, L1076-1090)
- VER[7:4] is the version ID and REV[3:0] the revision.
- Linux reads it last in init as a sanity check (LNX L395-424).
- Use it as an **I2C liveness check** (any value other than 0x00/0xFF).

### 0x02 Switches0, reset 0x03 (DS Table 6, L1091-1107)

| Bit | Name | Meaning | Sink value |
|---|---|---|---|
| 7/6 | PU_EN2/1 | host current source on CC2/CC1 | **0** (sink never pulls up) |
| 5/4 | VCONN_CC2/1 | VCONN switch to CC2/CC1 | **0** |
| 3/2 | MEAS_CC2/1 | route the measure block (BC_LVL/MDAC comparator) **and the PD receiver** to CC2/CC1 | **1 only on the active CC** |
| 1/0 | PDWN2/1 | Rd pull-down on CC2/CC1 | **1/1** (Rd on both) |

- Reset 0x03 means both Rd are on and nothing is measured.
- Linux sink with orientation known: `PDWN1|PDWN2|MEAS_CCx` (LNX L1148-1187). Example: CC1 → 0x07, CC2 → 0x0B.

### 0x03 Switches1, reset 0x20 (DS Table 7, L1108-1137)

| Bit | Name | Meaning | Sink value |
|---|---|---|---|
| 7 | POWERROLE | PowerRole bit placed into the auto-generated GoodCRC header. 0 = Sink. | 0 |
| 6:5 | SPECREV | SpecRev placed into the auto GoodCRC header. 00 = Rev1.0, 01 = Rev2.0. **10/11 = "Do Not Use"** (DS). | **01** (reset value) |
| 4 | DATAROLE | DataRole bit for the auto GoodCRC. 0 = UFP (SNK). | 0 |
| 3 | rsv | — | 0 |
| 2 | AUTO_CRC | 1 = HW automatically sends a GoodCRC for every valid received packet | **1** once PD is attached |
| 1/0 | TXCC2/1 | enable the BMC transmit driver on CC2/CC1 | **1 only on the active CC** |

- Linux sets `TXCCx` (active only) plus AUTO_CRC (LNX L804-811, L1148-1187). POWERROLE and DATAROLE are set via set_roles (LNX L892-921).
- Linux never touches SPECREV, so it stays at 01.
- Example sink, CC1 active: `0x25` = SPECREV01 | AUTO_CRC | TXCC1. For CC2: `0x26`.
- The GoodCRC SpecRev is 01 even when the partner talks PD3.0. Chip GoodCRCs use the "Rev2.0" encoding. See pd-messages.md for the implications (R3.2 treats 01 as 2.0, which is legal).

### 0x04 Measure, reset 0x31 (DS Table 8, L1141-1161)
- b6 MEAS_VBUS: 0 = the comparator measures CC (per MEAS_CCx); 1 = it measures VBUS through the MDAC.
- MDAC[5:0]: the COMP threshold is (MDAC+1)×42 mV for CC, or ×420 mV with MEAS_VBUS.
- **LNXH L29-37 bit defines disagree with the DS. Trust the DS: MEAS_VBUS is b6.**
- The sink does not need Measure for PD; BC_LVL is used instead. Keep the reset value.

### 0x05 Slice, reset 0x60 (DS Table 9, L1163-1179)
- The BMC slicer DAC and hysteresis. Leave it at reset; Linux never writes it.

### 0x06 Control0, reset 0x24 (DS Table 10, L1187-1212)

| Bit | Name | Meaning | Sink value |
|---|---|---|---|
| 6 | TX_FLUSH | write 1 → flush the TX FIFO (self-clearing) | pulse |
| 5 | INT_MASK | 1 = mask all interrupts to the host (INT_N pin); 1 at reset (reset 0x24 = INT_MASK + HOST_CUR 01) | 0 in Linux (LNX L364-384). Irrelevant when polling: the Interrupt registers latch regardless. |
| 3:2 | HOST_CUR | pull-up current for the source role (01 default USB) | don't care (PU off) |
| 1 | AUTO_PRE | 1 = start the transmitter automatically about 170 µs after a good-CRC packet is received, so SW can load a *manual* response within 300 µs (DS L1203-1209) | **0** (auto GoodCRC requires AUTO_CRC = 1 **and** AUTO_PRE = 0; DS L647-653) |
| 0 | TX_START | start transmitting the TX FIFO contents | 0 when using the TXON token |

### 0x07 Control1, reset 0x00 (DS Table 11, L1214-1234)
- b2 RX_FLUSH: write 1 to flush the RX FIFO (self-clearing).
- b1/b0 ENSOP2/ENSOP1: also receive SOP''/SOP'. A sink leaves these 0.
- ENSOPxDB: debug SOP variants. BIST_MODE2: BIST carrier. Both 0.

### 0x08 Control2, reset 0x02 (DS Table 12, L1242-1270)
- MODE[2:1]: 11 = SRC polling, **10 = SNK polling**, 01 = DRP polling.
- b0 TOGGLE: 1 enables autonomous toggle. The result is reported in Status1a.TOGSS and I_TOGDONE.
- TOG_RD_ONLY b5: when set, toggle stops only on Rd detection.
- WAKE_EN b3: enables the wake detector.
- Toggle requires PWR = 0x01 (see DS L372-384).
- A manual sink with Rd on both CCs does **not** need toggle. Set TOGGLE = 0 and keep MODE at reset.

### 0x09 Control3, reset 0x06 (DS Table 13, L1272-1296)

| Bit | Name | Meaning |
|---|---|---|
| 6 | SEND_HARD_RESET | write 1 → send a Hard Reset ordered set (RESET1×3 + RESET2) |
| 5 | rsv | Do Not Use (DS L1281) |
| 4 | AUTO_HARDRESET | on SOFTFAIL, automatically send a Hard Reset |
| 3 | AUTO_SOFTRESET | on RETRYFAIL, automatically send a Soft_Reset |
| 2:1 | N_RETRIES | 00 none, 01 = 1, 10 = 2, **11 = 3** (reset) |
| 0 | AUTO_RETRY | 1 = retry TX when no GoodCRC arrives within tReceive |

- Reset 0x06 means N_RETRIES = 3 and AUTO_RETRY = 0.
- Linux writes `N_RETRIES_3 | AUTO_RETRY` = 0x07 (LNX L350-358, LNXH L68).
- Per message, Linux uses 2 retries when the revision is above PD2.0 and 3 for PD2.0 (LNX L1030-1066, comment L1040). Spec: nRetryCount is 3 in R2.0 (PSE L2172-2185) and 2 in R3.2 (R32 Table 7.11 L16000).

### 0x0A Mask / 0x0E Maska / 0x0F Maskb (DS Tables 14/18/19, L1304-1332, L1388-1410)
- 1 = mask the corresponding interrupt bit. The DS wording is only "Mask I_xxx interrupt bit" (DS L1309-1311); Control0.INT_MASK separately gates the INT_N pin (DS L1194-1196).
- **UNCERTAIN:** the DS does not state whether a masked event still latches in Interrupt/Interrupta/Interruptb. **Polling rule: leave Mask/Maska/Maskb = 0x00 (reset)** so every event latches. Linux's 0xFF masking assumes a pin IRQ and must not be copied for a polling design.
- Linux init: Mask = 0xFF & ~M_VBUSOK, Maska = 0xFF, Maskb = 0xFF (LNX L364-384).
- Linux PD enable: unmask M_COLLISION, M_RETRYFAIL, M_HARDSENT, M_TXSENT, M_HARDRST, M_GCRCSENT (LNX L813-837).

### 0x0B Power, reset 0x01 (DS Table 15, L1334-1346)

| Bit | Block |
|---|---|
| PWR0 | bandgap + wake circuit |
| PWR1 | receiver powered + current references for the measure block |
| PWR2 | measure block powered |
| PWR3 | enable the internal oscillator |

- **PD sink needs 0x0F** (LNX tcpm_init writes PWR_ALL 0x0F; LNX L395-424, LNXH L86).
- The DS says the oscillator is automatically enabled by CC activity (L600-602). Linux still sets PWR3, and so should we.

### 0x0C Reset (DS Table 16, L1348-1359)
- b1 PD_RESET: resets only the PD logic (TX/RX state machines and the internal MessageID). Registers are unchanged.
- b0 SW_RES: full chip reset, **all registers back to reset values**.
- Both self-clear.

### 0x3C Status0a (DS Table 21, L1428-1448)
- SOFTFAIL, RETRYFAIL (all retries failed, no GoodCRC), POWER3:2 (reported power state), SOFTRST (Soft_Reset **received**), HARDRST (Hard Reset **received**).
- The DS notes that RETRYFAIL/SOFTFAIL status is cleared by START_TX/TXON/SEND_HARD_RESET.

### 0x3D Status1a (DS Table 22, L1450-1476)
- TOGSS[5:3]: 000 running, 001 SRC on CC1, 010 SRC on CC2, **101 SNK on CC1, 110 SNK on CC2**, 111 AudioAccessory.
- RXSOP: the last packet in the FIFO was SOP; RXSOP1DB/RXSOP2DB are the debug variants.

### 0x3E Interrupta / 0x3F Interruptb (DS Tables 23/24, L1484-1516)
- Read-to-clear (R/C). The bit order matches Maska/Maskb.

| Bit | Meaning |
|---|---|
| I_HARDRST | Hard Reset ordered set received |
| I_SOFTRST | Soft_Reset message received |
| I_TXSENT | our packet was sent **and a GoodCRC was received** |
| I_HARDSENT | our Hard Reset was sent |
| I_RETRYFAIL | all retries failed |
| I_SOFTFAIL | auto soft reset failed |
| I_TOGDONE | toggle finished |
| I_OCP_TEMP | over-current or over-temperature |
| I_GCRCSENT (0x3F b0) | **a packet was received and a GoodCRC was sent** (the RX success event) |

### 0x40 Status0 (DS Table 25, L1524-1565)

| Bit | Meaning |
|---|---|
| VBUSOK b7 | VBUS above vVBUSthr: 4.0 V per the electrical table (DS L868-870), 4.5 V in the Table 27 text (DS L1595). Needs PWR2 = 1. |
| ACTIVITY b6 | BMC transitions seen on CC (after ≥3 transitions) |
| COMP b5 | CC or VBUS above the MDAC threshold |
| CRC_CHK b4 | the last received packet had a valid CRC; valid until the next SOP |
| ALERT b3 | TX or RX FIFO full/overflow alert |
| WAKE b2 | wake detected |
| BC_LVL[1:0] | CC voltage level on the MEAS_CC pin, see below |

BC_LVL values:
- 00 < 200 mV (vRa / open)
- 01 200-660 mV (Rp default USB)
- 10 660 mV-1.23 V (Rp 1.5 A)
- 11 > 1.23 V (Rp 3.0 A)

Thresholds: DS L857-861. BC_LVL is valid only when PWR2 = 1 and MEAS_CCx = 1 (and MEAS_VBUS = 0). **Read it only when no BMC activity is present**: during PD traffic the CC line toggles and BC_LVL is garbage. This is why Linux defers 30 ms while ACTIVITY is set (LNX L1079-1125, L45).

### 0x41 Status1, reset 0x28 (DS Table 26, L1567-1585)

| Bit | Name | Meaning |
|---|---|---|
| 7/6 | RXSOP2/RXSOP1 | received SOP''/SOP' |
| 5 | **RX_EMPTY** | 1 = RX FIFO empty (reset: empty). **Loop reading packets while this is 0.** |
| 4 | RX_FULL | RX FIFO full (80 bytes) |
| 3 | TX_EMPTY | TX FIFO empty |
| 2 | TX_FULL | TX FIFO full (48 bytes) |
| 1 | OVRTEMP | over temperature |
| 0 | OCP | over current |

The reset value 0x28 means RX_EMPTY = 1 and TX_EMPTY = 1.

### 0x42 Interrupt (DS Table 27, L1590-1618)
- Read-to-clear. Bit order matches Mask.
- I_BC_LVL: BC_LVL changed. I_COLLISION: CC busy when TX was attempted. I_WAKE, I_ALERT.
- I_CRC_CHK: CRC_CHK changed, i.e. a packet with a valid CRC arrived. I_COMP_CHNG, I_ACTIVITY, I_VBUSOK: VBUSOK changed.

### 0x43 FIFOs (DS Table 28, L1620-1629)
- A write pushes into the TX FIFO (tokens, see fusb302-fifo.md). A read pops from the RX FIFO.
- **No address auto-increment**: a burst read or write at 0x43 stays on the FIFO.
- RX FIFO is 80 bytes; TX FIFO is 48 bytes (DS L607-608).
