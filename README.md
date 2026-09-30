# esphome-usbpd

USB-C Power Delivery **sink** for the **FUSB302** on an **ESP32-C6**. The firmware asks a USB-PD
charger for a fixed voltage (tested: 5 V and 9 V from a 100 W charger) and holds the contract.

Status (2026-09-30): standalone ESP-IDF firmware, verified on hardware (milestones M0–M6). An ESPHome
external component based on this driver is planned next.

## Layout

| Path | What |
|---|---|
| `docs/spec.md` | The behaviour contract: 16 states, transition table, message codec, log contract, test list |
| `docs/spec-questions.md` | Questions raised during implementation, with resolutions |
| `docs/knowledge/` | FUSB302 and USB-PD facts, each with a source reference |
| `docs/report-2026-09-30.md` | Timeline of the build day, evening prompts verbatim |
| `docs/how-we-built-it.md` | The multi-agent method: roles, order, rules, what came before |
| `docs/prompts-2026-09-30-morning.md` | The morning agent briefs, verbatim |
| `docs/sources.md` | Links to the specifications and datasheet (not included here) |
| `firmware/fw/` | ESP-IDF firmware (PlatformIO): FUSB302 driver, PD codec, table-driven state machine |
| `firmware/test/host/` | Host simulation with a fake FUSB302 and fake source (`make -C firmware/test/host test`) |
| `firmware/bench/` | Bench tooling: flash, reset, capture daemon, charger scenarios, `bench/q` log query |
| `firmware/runs/` | Recorded bench runs (JSONL events) |

Paths inside the docs (`fw/`, `bench/`, `runs/`, ...) are relative to `firmware/`.

## Hardware

- ESP32-C6-DevKitC-1, console over USB-Serial-JTAG
- FUSB302 at I2C address 0x22, SDA GPIO21, SCL GPIO22, no interrupt line (polled every 1 ms)

## Quick start

```sh
make -C firmware/test/host test                 # host simulation
BUILD_ID=dev python -m platformio run -d firmware/fw  # build firmware
```

Console commands on the device: `regs`, `rd <reg>`, `wr <reg> <val>`, `pd want <mV>`, `reboot`.

## Specifications

The USB-PD specification and the FUSB302 datasheet are **not** part of this repository. See
[`docs/sources.md`](docs/sources.md) for where to get them.
