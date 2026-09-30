# USB-PD messages: header, types, PDOs, RDO, worked Request

Sources:
- **PSE L<n>** = `/Users/jan/dev/repos/PCB-pi-r0/docs/spec/usb-pd/pd-sink-essentials.txt` (USB-PD **R2.0 v1.3** verbatim excerpts)
- **R32 L<n>** = `/Users/jan/dev/repos/PCB-pi-r0/docs/spec/usb-pd/usb-pd-spec-r3.2-v1.2.txt`

All multi-byte fields go on the wire **little-endian**: header byte 0 = bits 7..0 (LNX L964-1011, L1431-1482).

## 1. Message header (16 bit)

| Bits | R2.0 (PSE Table 6-1, L589-607) | R3.x (R32 Table 6.2, L7666-7700) |
|---|---|---|
| 15 | reserved (0) | **Extended** (0 = control/data message) |
| 14..12 | Number of Data Objects (NDO), 0 = control message | same |
| 11..9 | MessageID (rolling 0..7) | same |
| 8 | Port Power Role: 0 Sink, 1 Source (SOP) | same |
| 7..6 | Spec Revision: 00 = 1.0, 01 = 2.0, 10/11 = reserved | 00 = 1.0 (deprecated, read as 2.0), **01 = 2.0, 10 = 3.x**, 11 = reserved |
| 5 | Port Data Role: 0 UFP, 1 DFP | same |
| 4 | **reserved** (0) | **part of Message Type** |
| 3..0 | Message Type (4 bit) | Message Type bits 4..0 (5 bit) |

**Decoding rule:**
- If Extended = 1, the message is extended (R3 only; a sink may answer Not_Supported / ignore it).
- Else if NDO > 0, it is a data message.
- Else it is a control message (R32 L7715-7720).
- **Parse the type as 5 bits (`h & 0x1F`)**. This is safe for R2.0 because b4 is reserved = 0 there.

### Field rules
- **MessageID**:
  - The transmitter's MessageIDCounter is set to 0 at power-on, Soft Reset (sent or received) and Hard Reset.
  - It increments **only when a GoodCRC with a matching ID** is received (or when the PE aborts the message). Retries reuse the same ID.
  - Sources: PSE L615-621, L2090-2121; R32 §7.32.1-7.32.2, L15886-15917.
- **Receiver**:
  - Store the ID of the first good packet after a reset.
  - If a later packet has the **same** ID, send GoodCRC (HW does this) but **drop** it as a retry.
  - Soft_Reset (always ID 0) is exempt.
  - Sources: PSE L2090-2121; R32 L15919-15930.
- **Power Role / Data Role** as a sink presenting Rd: PowerRole = 0, DataRole = 0 (UFP) (PSE L638-640, L657-670; R32 L7690-7704).
  - PowerRole is not verified by the receiver.
  - Receiving a message whose DataRole equals ours (except GoodCRC) triggers Type-C Error Recovery (R32 L7700-7708).
- **Spec Revision negotiation** (R32 §6.1.3.1, L7372-7386): the source sends Source_Caps with its highest revision. The sink's Request uses the highest revision it supports that is ≤ the source's. Both then use the Request's revision until Detach, Hard Reset or Error Recovery.
  - Every PD product must support Rev 2.0 (R32 L7343-7347).
  - **Recommendation:** a fixed-voltage sink may simply send SpecRev = **01** in every header. PD2.0 rules then apply for the whole contract (4-bit types, nRetryCount 3).
  - PPS and extended messages would require 10.
- **GoodCRC SpecRev**: it carries no meaning, and the receiver ignores it.
  - Responding with 01 to a 01 message is mandatory; responding with 01 or 10 to a 10 message is allowed (R32 L7357-7362).
  - **So the FUSB302's fixed SPECREV = 01 in auto GoodCRC is compliant with both.**
- GoodCRC echoes the received MessageID and is sent within tTransmit (PSE L715-721).

## 2. Control message types (NDO = 0)

| Type | R2.0 (PSE Table 6-2, L693-714) | R3.x additions (R32 Table 6.4, L7768-7800) |
|---|---|---|
| 0x00 | reserved | reserved |
| 0x01 | **GoodCRC** | = |
| 0x02 | GotoMin | = (deprecated in R3) |
| 0x03 | **Accept** | = |
| 0x04 | **Reject** | = |
| 0x05 | Ping | = |
| 0x06 | **PS_RDY** | = |
| 0x07 | Get_Source_Cap | = |
| 0x08 | Get_Sink_Cap | = |
| 0x09 | DR_Swap | = |
| 0x0A | PR_Swap | = |
| 0x0B | VCONN_Swap | = |
| 0x0C | **Wait** | = |
| 0x0D | **Soft_Reset** | = |
| 0x0E | — | Data_Reset |
| 0x0F | — | Data_Reset_Complete |
| 0x10 | — | Not_Supported |
| 0x11 | — | Get_Source_Cap_Extended |
| 0x12 | — | Get_Status |
| 0x13 | — | FR_Swap |
| 0x14 | — | Get_PPS_Status |
| 0x15 | — | Get_Country_Codes |
| 0x16 | — | Get_Sink_Cap_Extended |
| 0x17 | — | Get_Source_Info |
| 0x18 | — | Get_Revision |

## 3. Data message types (NDO > 0)

| Type | R2.0 (PSE Table 6-3, L984-998) | R3.x (R32 Table 6.5, L8176) |
|---|---|---|
| 0x01 | **Source_Capabilities** | = |
| 0x02 | **Request** | = |
| 0x03 | BIST | = |
| 0x04 | Sink_Capabilities | = |
| 0x05 | — | Battery_Status |
| 0x06 | — | Alert |
| 0x07 | — | Get_Country_Info |
| 0x08 | — | Enter_USB |
| 0x09 | — | EPR_Request |
| 0x0A | — | EPR_Mode |
| 0x0B | — | Source_Info |
| 0x0C | — | Revision |
| 0x0F | Vendor_Defined | = |

## 4. Power Data Objects (in Source_Capabilities)

- Ordering: the first PDO is **always vSafe5V Fixed**; the rest are in increasing voltage (PSE L1000-1008).
- Up to 7 PDOs; object positions 1..7 are SPR (R32 L9142-9151).
- The sink **must answer every Source_Capabilities with a Request** (PSE L1092-1094).

### PDO type, bits 31..30 (PSE Table 6-4 L1036-1049; R32 Table 6.6 L8282)

| Bits 31..30 | Type | Sub-type (R3: APDO bits 29..28, R32 Table 6.7 L8302) |
|---|---|---|
| 00 | Fixed | — |
| 01 | Battery | — |
| 10 | Variable | — |
| 11 | APDO (R3 only) | 00 SPR PPS, 01 EPR AVS, 10 SPR AVS, 11 reserved |

### Fixed Supply PDO, source (PSE Table 6-6 L1150-1162; R32 Tables 6.8 L8321, 6.10 L8443)

| Bits | Field | Note |
|---|---|---|
| 31..30 | 00 | Fixed |
| 29 | Dual-Role Power | **PDO #1 (5 V) only** |
| 28 | USB Suspend Supported | PDO #1 only |
| 27 | Unconstrained Power | PDO #1 only |
| 26 | USB Communications Capable | PDO #1 only |
| 25 | Dual-Role Data | PDO #1 only |
| 24 | R3: Unchunked Ext Msg Supported (R2: reserved) | PDO #1 only |
| 23 | R3: EPR Capable (R2: reserved) | PDO #1 only |
| 22 | reserved | |
| 21..20 | Peak Current | |
| 19..10 | Voltage, **50 mV units** | 9 V = 180 = 0xB4 |
| 9..0 | Max Current, **10 mA units** | 3 A = 300 = 0x12C |

- For PDOs other than #1, bits 29..22 are reserved and ignored (PSE L1140-1142; R32 Table 6.10 L8443-8450).
- Decode: `mV = ((pdo >> 10) & 0x3FF) * 50`, `mA = (pdo & 0x3FF) * 10`.

### Variable PDO (PSE Table 6-8 L1248-1254; R32 Table 6.12 L8512)
- 31..30 = 10
- 29..20 Max V ×50 mV
- 19..10 Min V ×50 mV
- 9..0 Max I ×10 mA

### Battery PDO (PSE Table 6-9 L1265-1271; R32 Table 6.11 L8473)
- 31..30 = 01
- 29..20 Max V ×50 mV
- 19..10 Min V ×50 mV
- 9..0 Max **power** ×250 mW

### SPR PPS APDO (R32 Table 6.13 L8583)
- 31..30 = 11, 29..28 = 00
- 27 PPS power limited; 26..25 reserved
- 24..17 Max V ×100 mV; 16 reserved
- 15..8 Min V ×100 mV; 7 reserved
- 6..0 Max I ×50 mA
- A PPS request requires SpecRev 3.x; not needed for a 9 V fixed contract.

## 5. Request Data Object: Fixed/Variable RDO

| Bits | R2.0 (PSE Table 6-13 L1404-1415) | R3.2 (R32 Table 6.19 L9011) | Value to send |
|---|---|---|---|
| 31 | reserved | Object Position b3 (EPR positions 8+) | 0 |
| 30..28 | Object Position (1..7; **0 reserved**) | Object Position (1..7 = SPR) | index of the chosen PDO, **1-based** |
| 27 | GiveBack flag | Giveback, deprecated | 0 |
| 26 | Capability Mismatch | = | 0 if the PDO satisfies the sink |
| 25 | USB Communications Capable | = | 0 (no USB data on this board) |
| 24 | No USB Suspend | = | 1 is advisable for a charger-type sink; 0 is legal |
| 23 | reserved | Unchunked Ext Msg Supported | 0 |
| 22 | reserved | EPR Capable | 0 |
| 21..20 | reserved | reserved | 0 |
| 19..10 | Operating Current ×10 mA | = | ≤ the PDO's Max Current |
| 9..0 | Max Operating Current ×10 mA | deprecated, **set equal to Operating** | = Operating |

- Object position 1 is always the 5 V PDO (PSE L1459-1463).
- Operating Current ≤ Max, and ≤ the source PDO's current (PSE L1523-1530; R32 L9022-9030).

## 6. Worked example: Request 9 V / 3 A

**Assumption:** Source_Caps PDO #2 is Fixed 9 V with ≥ 3 A. Our MessageID counter is 0 and SpecRev is 01.

**RDO**
- Object Position = 2 → `2 << 28 = 0x20000000`
- Operating = 300 (3 A) → `300 << 10 = 0x0004B000`
- Max = 300 → `0x0000012C`
- **RDO = 0x2004B12C** → LE bytes `2C B1 04 20`

**Header**

| Field | Value | Bits |
|---|---|---|
| Extended b15 | 0 | 0x0000 |
| NDO b14..12 | 1 | 0x1000 |
| MessageID b11..9 | 0 | 0x0000 (ID n adds `n << 9`) |
| PowerRole b8 | 0 Sink | 0 |
| SpecRev b7..6 | 01 | 0x0040 |
| DataRole b5 | 0 UFP | 0 |
| Type b4..0 | 0x02 Request | 0x0002 |

- **Header = 0x1042** → LE bytes `42 10`.
- For MessageID n: header = `0x1042 | (n << 9)`. For example, ID 1 → 0x1242 (`42 12`) and ID 3 → 0x1642.

**FUSB302 TX FIFO burst (to 0x43)** — see fusb302-fifo.md:
```
12 12 12 13  86  42 10  2C B1 04 20  FF 14 FE A1
SOP1x3 SOP2  PACKSYM|6  header  RDO  JAM_CRC EOP TXOFF TXON
```
Verified by computation: RDO = 0x2004B12C, header = 0x1042.

**Expected response sequence** (PSE Transition_Sink L3984; timers in pd-sink-policy.md):
1. The source's GoodCRC for our Request (in the RxFIFO; I_TXSENT). Then increment MessageID.
2. Accept, type 0x03, NDO 0. Our HW auto-GoodCRCs it.
3. PS_RDY, type 0x06, within tPSTransition 450-550 ms of Accept. VBUS is now 9 V.
   - Reject 0x04 or Wait 0x0C are the alternatives to Accept.

## 7. Parse helpers (bit math)

```
ndo   = (h >> 12) & 0x7
msgid = (h >> 9)  & 0x7
prole = (h >> 8)  & 0x1
rev   = (h >> 6)  & 0x3
drole = (h >> 5)  & 0x1
type  =  h        & 0x1F     // 0x0F mask is enough for PD2.0 partners
ext   = (h >> 15) & 0x1
pdo_type = pdo >> 30;  fixed_mV = ((pdo>>10)&0x3FF)*50;  fixed_mA = (pdo&0x3FF)*10
```
