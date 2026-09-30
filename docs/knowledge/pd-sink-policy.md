# USB-PD sink policy: Type-C attach, PE_SNK states, protocol rules, timers

Sources:
- **PSE L<n>** = `/Users/jan/dev/repos/PCB-pi-r0/docs/spec/usb-pd/pd-sink-essentials.txt` (USB-PD R2.0 v1.3)
- **R32 L<n>** = `/Users/jan/dev/repos/PCB-pi-r0/docs/spec/usb-pd/usb-pd-spec-r3.2-v1.2.txt`
- **DS** and **LNX** as in [fusb302-registers.md](fusb302-registers.md)

**GAP:** the USB Type-C spec is **not available locally**. Type-C values below come from the FUSB302 DS and Linux, or are marked "Type-C spec, not verified locally".

## 1. Type-C sink attach

| Step | Fact | Source |
|---|---|---|
| Rd | the sink presents Rd (5.1 kΩ) on **both** CC1 and CC2 (FUSB302 PDWN1/PDWN2, RDEVICE 5.1 kΩ) | DS L846, T6 L1091-1107 |
| Source detects Rd | it applies vSafe5V on VBUS | DS "Manual Device Detection" L356-369 |
| VBUS detect | Status0.VBUSOK above vVBUSthr of 4.0 V (needs PWR2) | DS L868-870, T25 |
| Orientation | the CC with Rp shows BC_LVL ≠ 00. The other CC is open (BC_LVL 00), or Ra through an e-marked cable (also < 200 mV). | DS L857-861, L1555-1565 |
| Rp current (vRd bands) | BC_LVL 01 = 200-660 mV → Default USB; 10 = 660 mV-1.23 V → 1.5 A; 11 = > 1.23 V → 3.0 A | DS L857-861, L1557-1560 |
| Debounce | Type-C tCCDebounce 100-200 ms (Type-C spec, not verified locally). Linux only defers BC_LVL reads by 30 ms while ACTIVITY is set. | LNX L45, L1079-1125 |
| BC_LVL validity | measure only while no BMC activity is present | DS L1561-1562 |
| Detach | VBUS drops below the threshold (VBUSOK = 0) **outside** a hard reset → detach. Reset all PD state and HardResetCounter. | PSE L3910-3923 note 4; PSE PE_SNK_Hard_Reset note ("nHardResetCount is reset on a power cycle or Detach") |

- During a Hard Reset, VBUS goes vSafe5V → vSafe0V → vSafe5V. **A sink must not treat that as a detach.** It must see both transitions before indicating VBUS present again (PSE L3910-3923, note 4).

## 2. PE_SNK state machine (sink-only consumer, Type-C)

| State | Entry actions | Exit conditions → next state | Source |
|---|---|---|---|
| **Startup** | reset the Protocol Layer: MessageIDCounter = 0, stored RX MessageID cleared | reset done → Discovery | PSE L3924-3930 |
| **Discovery** | wait for VBUS | VBUS present → Wait_for_Capabilities. (The NoResponse → Hard Reset path applies only to non-Type-C.) | PSE L3931-3939 |
| **Wait_for_Capabilities** | start SinkWaitCapTimer | Source_Capabilities received → Evaluate_Capability. Timer timeout → Hard_Reset. | PSE L3940-3946 |
| **Evaluate_Capability** | DPM picks a PDO (e.g. 9 V) or flags Capability Mismatch | → Select_Capability | PSE L3947-3961 |
| **Select_Capability** | send the Request; start SenderResponseTimer | Accept → Transition_Sink. Reject/Wait with no contract → Wait_for_Capabilities. Reject/Wait with a contract → Ready. SenderResponseTimer timeout → Hard_Reset. | PSE L3962-3983 |
| **Transition_Sink** | start PSTransitionTimer | PS_RDY → Ready (apply the new power level on exit). PSTransitionTimer timeout → Hard_Reset. | PSE L3984-3992 |
| **Ready** | start SinkActivityTimer (see exception below). After Wait, start SinkRequestTimer. | Source_Caps → Evaluate_Capability. DPM wants a new level or SinkRequestTimer expires → Select_Capability. GotoMin → Transition_Sink. Get_Sink_Cap → Give_Sink_Cap. Ping → stay. SinkActivityTimer expires → Hard_Reset. | PSE L3993-4023 |
| **Give_Sink_Cap** | send Sink_Capabilities | sent → Ready | PSE L4056-4062 |
| **Get_Source_Cap** | send Get_Source_Cap | sent → Ready | PSE L4063-4070 |
| **Hard_Reset** | send Hard Reset signaling; HardResetCounter++ | complete → Transition_to_default | PSE L4024-4039 |
| **Transition_to_default** | entered from any state when a HR is sent or received. Tell the DPM to go to default power; reset local HW; Data Role = UFP. | DPM reached default → Startup | PSE L4040-4055 |
| **Send_Soft_Reset** | reset the Protocol Layer; send Soft_Reset (ID 0); start SenderResponseTimer | Accept → Wait_for_Capabilities. Timer timeout or TX error → Hard_Reset. | PSE L4149-4180 |
| **Soft_Reset** (received) | reset the Protocol Layer; send Accept | Accept sent → Wait_for_Capabilities. TX error → Hard_Reset. | PSE L4181-4195 |

Notes:
- Hard_Reset is entered only while HardResetCounter ≤ nHardResetCount (2). After that, the source is considered non-responsive: stay at 5 V (PSE L4024-4039).
- **SinkActivityTimer does not run** at vSafe5V or between two Type-C ports, because Ping is optional (PSE L3910-3923, note 2). So a Type-C sink effectively ignores it.
- A Source_Capabilities message received in a state other than Wait_for_Caps/Ready (or Select_Capability awaiting a reply) is a protocol error, i.e. a Soft Reset.
  - Exception: during a voltage transition (Transition_Sink), a protocol error → **Hard Reset** (PSE L2202-2224, L4149-4165).
- Unrecognized or unsupported message in Ready:
  - R2.0 → **Reject** (PSE L2202-2224).
  - R3.x → **Not_Supported** (R32 L8043, L13057).
  - Never Soft_Reset.
- **Unexpected message in Ready → Soft Reset** (PSE L2202-2224).
- If our first message of an AMS was not yet acknowledged (no GoodCRC) when another message arrives: do not Soft Reset. Go to Ready and process the incoming message (PSE L2202-2224, L4149-4165).
  - This is the normal race when the source re-sends Source_Caps while our Request is being retried.

## 3. Protocol layer rules

| Rule | Source |
|---|---|
| Every received good packet is acknowledged with GoodCRC within tTransmit (the FUSB302 does this automatically) | PSE L715-721; DS L642-653 |
| RX flow: Wait_for_PHY → Send_GoodCRC → Check_MessageID → Store_MessageID → pass to PE | PSE §6.9.2.2 L2511-2577 |
| Same MessageID as the stored one → drop (GoodCRC was still sent). Soft_Reset is exempt. | PSE L2090-2121; R32 L15919-15930 |
| TX: after sending, CRCReceiveTimer (tReceive). No matching GoodCRC → retry up to nRetryCount with the same ID. Success → MessageIDCounter++ mod 8. | PSE L1595-1608, L2090-2126; R32 L15903-15917 |
| All retries failed → Soft Reset (PE_SNK_Send_Soft_Reset) | PSE L2122-2126, L4149 |
| Soft_Reset sent or received → MessageIDCounter = 0, RetryCounter = 0; stored RX ID cleared | PSE L2202-2224; R32 L13876-13877 |
| Hard Reset sent or received → Protocol Layer reset (as above) | PSE L2286-2312 |
| SenderResponseTimer starts when the **GoodCRC for our request** is received (EOP of the GoodCRC) | PSE L1609-1622 |
| PSTransitionTimer starts at the last bit of the **Accept** (or GotoMin) EOP received; it stops at the PS_RDY EOP | PSE L1729-1741 |
| **The responder (us, for Source_Caps) Shall respond within tReceiverResponse (15 ms)**, measured from the received message EOP to our response preamble | PSE L1609-1622 |
| The FUSB302 retry count is set by Control3.N_RETRIES (3 for PD2.0, 2 for PD3.x) | DS T13; LNX L1030-1066 |

## 4. Timers (values)

| Timer / time | R2.0 (PSE Table 6-32 L1995-2050) | R3.2 (R32 Table 7.9 L15757-15848) | Owner and meaning for us |
|---|---|---|---|
| tTransmit | 195 µs | 195 µs | max from RX EOP to GoodCRC start; HW |
| tReceive (CRCReceiveTimer) | 0.9-1.1 ms | 0.9-1.1 ms | wait for GoodCRC after our TX; HW (DS L924-926) |
| tRetry | 75 µs | 195 µs | gap before a retry; HW (DS L927-931 says 75 µs) |
| tSenderResponse | 24-30 ms | 24-30 ms (nominal 27) | **the source waits this long for our Request** after its Source_Caps GoodCRC. Our PE also uses it waiting for Accept. |
| tReceiverResponse | 15 ms | 15 ms | **our** limit: from the Source_Caps EOP to the Request preamble (PSE L1609-1622) |
| tFirstSourceCap | 250 ms | 250 ms | the source sends its first Source_Caps within this after vSafe5V |
| tTypeCSendSourceCap | 100-200 ms | 100-200 ms | the source's re-send period when no GoodCRC is received |
| tTypeCSinkWaitCap | 310-620 ms | 310-620 ms | **our SinkWaitCapTimer (Type-C)** → Hard Reset if no caps |
| tSinkWaitCap | 2.1-2.5 s | — | legacy SinkWaitCapTimer (non-Type-C) |
| tPSTransition | 450-550 ms | SPR 450-550 ms | **Accept → PS_RDY** max; timeout → Hard Reset |
| tPSHardReset | 25-35 ms | 25-35 ms | source HR → VBUS off timing |
| tHardReset | 5 ms | — | HR signaling send time (DS L916-923) |
| tHardResetComplete | 4-5 ms | 4-5 ms | HR must complete within this |
| tSoftReset | 15 ms | 15 ms | Soft_Reset response time |
| tNoResponse | 4.5-5.5 s | 4.5-5.5 s | NoResponseTimer (HR count exhaustion) |
| tSinkRequest | 100 ms | — | after Wait, re-request in Ready |
| tSinkActivity | 120-150 ms | — | not run for Type-C (see §2) |
| tSinkTx | — | 16-20 ms | R3 collision avoidance (Rp SinkTxOk/NG); only for sink-initiated AMS in R3 |

## 5. Counters

| Counter | R2.0 (PSE Table 6-34 L2172-2185) | R3.2 (R32 Table 7.11 L16000) | Use | Source |
|---|---|---|---|---|
| nRetryCount | 3 | 2 | TX retries after the first attempt | PSE L2122-2126 |
| nHardResetCount | 2 | 2 | HR attempts before giving up | PSE L2127-2134 |
| nCapsCount | 50 | 50 | the source's Source_Caps sends without GoodCRC before it stops PD (source side) | PSE L2135-2147 |
| nMessageIDCount | 7 | 7 | MessageID wraps 7 → 0 | PSE L2090-2121 |

## 6. Happy path timeline for 9 V (sink view)

| t | Event | Our action | Limit |
|---|---|---|---|
| 0 | VBUSOK = 1 (vSafe5V) | orientation via BC_LVL, configure PD (fusb302-sink-init.md) | finish well before tFirstSourceCap (250 ms), or catch a re-send |
| ≤ 250 ms | Source_Caps (SOP, NDO = N) | HW GoodCRC (I_GCRCSENT); parse PDOs; build RDO | — |
| + ≤ 15 ms | send Request (MessageID m) | wait for I_TXSENT (source GoodCRC); m++ | **spec: within tReceiverResponse 15 ms** of the Source_Caps EOP. Hard backstop: the source's tSenderResponse (min 24 ms) from our GoodCRC. |
| + ≤ 24-30 ms | Accept (HW GoodCRC) | start PSTransitionTimer | our SenderResponseTimer → HR if missing |
| + ≤ 450-550 ms | PS_RDY (HW GoodCRC) | contract done; VBUS ≈ 9 V (confirm via VBUS measure) | PSTransitionTimer → HR if missing |
| later | Source_Caps again, Get_Sink_Cap, Ping, etc. | respond per §2 | — |

**Missed the source's tSenderResponse?** The source will Soft Reset or Hard Reset, then re-send Source_Caps. Poll fast enough (≤ ~5 ms loop, so RX + parse + TX ≤ 15 ms) during negotiation.
