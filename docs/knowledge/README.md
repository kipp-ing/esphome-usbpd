# Knowledge base: FUSB302 + USB-PD sink (ESP32-C6, I2C 0x22, polled, no INT)

Goal: request a fixed PDO above 5 V (e.g. 9 V), get Accept, then PS_RDY.

- Every fact in these files carries a source: the datasheet line, a Linux driver line, or a spec table/line.
- The previous firmware in PCB-pi-r0 was deliberately **not** used as a source.

| File | Contents |
|---|---|
| [fusb302-registers.md](fusb302-registers.md) | full register map (addr, bits, reset, meaning) + sink values |
| [fusb302-sink-init.md](fusb302-sink-init.md) | Linux reference order; recommended polled init/attach/orientation/PD-enable sequence; hard-reset recovery; "no frames received" pitfalls P1-P12 |
| [fusb302-fifo.md](fusb302-fifo.md) | RX token/frame format and poll loop, auto GoodCRC, TX token burst, TX events, retries/soft reset, hard reset send/receive |
| [pd-messages.md](pd-messages.md) | header R2.0 vs R3.x, SpecRev negotiation, control/data types, Fixed/Variable/Battery/PPS layouts, Fixed RDO, **worked 9 V/3 A Request bytes** |
| [pd-sink-policy.md](pd-sink-policy.md) | Type-C sink attach (Rd, BC_LVL vRd bands), PE_SNK states + transitions, protocol-layer rules, timers, counters, happy-path timeline |

## Source keys

| Key | Location |
|---|---|
| DS L<n> | `/Users/jan/dev/repos/PCB-pi-r0/tmp/pdfs/fusb302mpx.txt` (FUSB302 datasheet text) |
| LNX L<n> | https://github.com/torvalds/linux/blob/55311a92bc9564b058a036074f85200a5954ccd2/drivers/usb/typec/tcpm/fusb302.c |
| LNXH L<n> | same commit, `drivers/usb/typec/tcpm/fusb302_reg.h` |
| PSE L<n> | `/Users/jan/dev/repos/PCB-pi-r0/docs/spec/usb-pd/pd-sink-essentials.txt` (USB-PD R2.0 v1.3 excerpts) |
| R32 L<n> | `/Users/jan/dev/repos/PCB-pi-r0/docs/spec/usb-pd/usb-pd-spec-r3.2-v1.2.txt` |

## Top 10: things that must be right for RX to work

| # | Must be right | Wrong → symptom | Source |
|---|---|---|---|
| 1 | **Power (0x0B) = 0x0F**. PWR1 = receiver, PWR2 = measure/BC_LVL, PWR3 = oscillator. Reset is 0x01. | receiver off: RX FIFO stays empty forever | DS T15 L1334-1346; LNX L395-424 |
| 2 | **Switches0 = PDWN1\|PDWN2\|MEAS_CCx on the CC that carries Rp** (0x07 for CC1, 0x0B for CC2). Keep both PDWN in every write. | wrong CC: nothing received. PDWN dropped: the source sees a detach and cuts VBUS | DS T6; LNX L1148-1187 |
| 3 | **Orientation from BC_LVL measured while ACTIVITY = 0** (BC_LVL ≠ 00 = the Rp side); defer ~30 ms if active | wrong CC chosen during BMC traffic | DS L1555-1565; LNX L1079-1125, L45 |
| 4 | **Switches1 = SPECREV 01 \| AUTO_CRC \| TXCCx, same CC as MEAS** (0x25 for CC1, 0x26 for CC2); POWERROLE = DATAROLE = 0 | no GoodCRC → the source retries, then Soft Reset, then Hard Reset (VBUS drops); caps arrive repeatedly | DS T7, L647-653; LNX L804-811, L892-921 |
| 5 | **Control0.AUTO_PRE = 0** (reset value). Auto GoodCRC needs AUTO_CRC = 1 **and** AUTO_PRE = 0. | no auto GoodCRC | DS L647-653, L1203-1209 |
| 6 | **RX_FLUSH + TX_FLUSH (+ PD_RESET) after config and after every Hard Reset** | stale bytes → token desync, garbage headers | LNX L839-880, L1593-1621; DS T16 |
| 7 | **Read FIFO 0x43 sequentially** (no auto-increment): token (`& 0xE0 == 0xE0` = SOP) → header 2 bytes LE → 4·NDO bytes → 4 CRC bytes. **Loop while Status1.RX_EMPTY (0x41 b5) == 0.** On a bad token, RX_FLUSH. | misaligned parse; lost second packet | DS L1068, T30 L1698-1722, T26; LNX L1431-1482 |
| 8 | **Interrupt registers 0x42/0x3E/0x3F are read-to-clear.** Read each once per poll and act on all bits. Masks stay 0x00 when polling. Drive RX from RX_EMPTY; the partner's GoodCRC for our TX also lands in the RxFIFO (NDO = 0, type 1). | lost I_HARDRST/I_TXSENT events; GoodCRC mistaken for a message | DS T23/24/27; LNX L1431-1482, L1603-1631 |
| 9 | **Answer Source_Caps fast**: Request preamble within tReceiverResponse 15 ms of the caps EOP (source backstop tSenderResponse 24-30 ms). Poll ≤ ~5 ms while negotiating. | the source never sees a Request → Soft Reset / Hard Reset loop | PSE L1609-1622, Table 6-32 |
| 10 | **Header/MessageID correctness**: parse type as `h & 0x1F`, NDO = `(h>>12)&7`. Our TX IDs start at 0 after Startup/SR/HR and increment only on I_TXSENT. Drop RX duplicates (same ID) except Soft_Reset. Send SpecRev 01 (or ≤ the source's). N_RETRIES 3 for PD2.0 / 2 for PD3. | Request ignored as a duplicate; wrong type decode for R3 sources | PSE L589-621, L2090-2121; R32 L7372-7386, L15886-15930; LNX L1030-1066 |

## Open uncertainties / gaps

- **Type-C spec not local**: tCCDebounce (100-200 ms), exact vRd thresholds, and the error-recovery timing are not verified from source. Datasheet BC_LVL thresholds and Linux's 30 ms are used instead.
- **Masks:** the DS does not state whether a masked event still latches in the Interrupt registers. Mitigation: masks = 0x00 and RX driven by RX_EMPTY.
- **I_TXSENT:** the DS does not say whether the chip checks the MessageID of the received GoodCRC. SW can verify it from the GoodCRC in the RxFIFO.
- **Toggle PWR:** the DS contradicts itself for toggle mode (Table 1 PWR = 0x07 vs text 0x01, L371 vs L382). The manual orientation path avoids it.
- **Measure register:** LNXH L29-37 bit defines disagree with the DS. The DS is trusted (MEAS_VBUS = b6).
- **No mandated delays** after SW_RES / Power-up in the DS or Linux. Wait ≥ 1 ms (engineering choice, unsourced).
- **tRetry:** 75 µs (R2.0, DS) vs 195 µs (R3.2) — HW-controlled, informational only.
