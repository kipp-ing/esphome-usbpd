# fw — ESP32-C6 + FUSB302 sink firmware (owner: mr-engineer)

ESP-IDF 5.5 via PlatformIO, env `esp32c6` (board esp32-c6-devkitc-1, 8 MB flash).

## Build (engineer) — flashing is mr-bench's job
```
BUILD_ID=<id> /opt/homebrew/Cellar/esphome/2026.8.0/libexec/bin/python -m platformio run -d fw
```
Artifacts: `fw/.pio/build/esp32c6/firmware.factory.bin` (merged, flash at 0x0) or
`bootloader.bin`@0x0, `partitions.bin`@0x8000, `firmware.bin`@0x10000.

## Build-id contract (mr-engineer <-> mr-bench)
- Env var `BUILD_ID` at build time -> `-DBUILD_ID="<id>"` (via `fw/build_id.py`). Unset/empty -> `"dev"`.
- Allowed chars `[A-Za-z0-9._:+-]`, 1..48 long; anything else fails the build.
- Changing BUILD_ID rebuilds everything (~60 s); that's expected.
- The first JSONL line after every reset is
  `{"t":<ms>,"ev":"boot","build":"<BUILD_ID>","reset_reason":"poweron|sw|panic|usb|...","i2c_hz":400000}`
  — bench verifies a flash by matching `build`.
- Followed by `{"ev":"fusb.id","ok":true,"id":"0x91","ver":9,"rev":1}` (ok:false if I2C fails)
  and `{"ev":"sm.init",...}`.

## Output stream (USB-Serial-JTAG)
JSONL, one object per line, always `t` (ms since boot) and `ev`. ROM/bootloader text may
precede the boot line — consumers must skip lines that don't start with `{`.
If the host isn't reading, whole lines are dropped (never blocks); the next `hb` then
carries `log_drops`. Lines too long for the buffer end in `"trunc":true`.

| ev | meaning |
|---|---|
| `boot` | see above |
| `hb` | every 1 s: `state`, `i2c_errs`, (`log_drops` when it changed) |
| `sm` | every transition `from`,`to`,`on` (internal transitions have from==to) |
| `sm.init` / `sm.unhandled` / `sm.qfull` / `sm.want` | SM bookkeeping |
| `i2c.err` | `op`,`reg`,`n`,`rc`,`rc_name`,`errs` |
| `cmd` / `regs` | console replies |

## Console (send lines terminated by `\n` or `\r`)
| command | reply |
|---|---|
| `help` | `{"ev":"cmd","cmd":"help","ok":true,"usage":...}` |
| `regs` | `{"ev":"regs","v":{"0x01":"0x91",...},"ok":true}` — all documented regs EXCEPT Reset, Interrupt/a/b (read-clears) and FIFOs (pops) |
| `rd <reg>` | `{"ev":"cmd","cmd":"rd","a":"0x40","v":"0x81","ok":true}` |
| `wr <reg> <val>` | `{"ev":"cmd","cmd":"wr","a":..,"v":..,"ok":true}` |
| `pd want <mV>` | `{"ev":"cmd","cmd":"pd want","mv":9000,"ok":true}` (stores target, posts SM event) |
| `reboot` | `{"ev":"cmd","cmd":"reboot","ok":true}` then esp_restart |
Numbers accept decimal or `0x` hex. Errors: `{"ev":"cmd","cmd":..,"ok":false,"err":"bad_arg|unknown|line_too_long"}`.

## Layout
- `src/log.{c,h}` — JSONL logger, pure C (clock + sink injected)
- `src/fusb302_regs.h` — datasheet register map + bits (pure defines)
- `src/fusb302.{c,h}` — I2C HAL @ 400 kHz (SDA 21, SCL 22, addr 0x22), no init policy
- `src/pd_sm.{c,h}` — table-driven SM, pure C, hardware via `struct pd_port_ops`
  (states/events are PLACEHOLDERS until docs/spec.md lands)
- `src/console.{c,h}`, `src/main.c` — ESP-IDF glue; single task, 1 ms poll loop
- Host sim: `make -C test/host test` (fake FUSB302 + scenario runner)
