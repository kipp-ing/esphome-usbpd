# fw — ESP32-C6 + FUSB302 sink firmware (owner: mr-engineer)

ESP-IDF 5.5 via PlatformIO, env `esp32c6` (board esp32-c6-devkitc-1, 8 MB flash).

## Build (engineer) — flashing is mr-bench's job
```
BUILD_ID=<id> /opt/homebrew/Cellar/esphome/2026.8.0/libexec/bin/python -m platformio run -d fw
```
Artifacts: `fw/.pio/build/esp32c6/firmware.factory.bin` (merged, flash at 0x0) or
`bootloader.bin`@0x0, `partitions.bin`@0x8000, `firmware.bin`@0x10000.

## Build-id contract (mr-engineer <-> mr-bench)
- Env var `BUILD_ID` at build time -> `fw/src/build_id.h` (`#define BUILD_ID "<id>"`, gitignored),
  written by the shared pre-script `bench/build_id.py` only when the id changes; `main.c` includes it.
  Unset/empty -> `"dev"`. A new id recompiles only main.c (seconds, not a full ESP-IDF rebuild).
- Chars `[A-Za-z0-9._:+-]`, <= 48 (flash.sh generates ids in that form).
- `"build"` is also in every `hb` line, so the id is visible even if the boot line was dropped.
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
| `hb` | every 1 s: `build`, `state`, `vbus`, `want`, `mv` (contract mV or 0), `poll_max_us` (slowest `pd_sm_step` since the last hb), `i2c_errs`, (`log_drops` when it changed) |
| `poll.slow` | one `pd_sm_step` took > 2000 us: `us`, `state`, `n` (total count); rate-limited to 1 line/s |
| `sm` | every real state change `from`,`to`,`on` (SAME rows are not logged) |
| `sm.init` | `rows` (85), `states` (16), `state` |
| `sm.unhandled` / `sm.qfull` | no row matched (`state`,`on`) / event queue full (`on`,`drops`) |
| `vbus` `cc` `cc.retry` `pd.cfg` | Type-C layer, docs/spec.md §7 |
| `pd.rx` `pd.caps` `pd.caps.bad` `pd.sel` `pd.tx` `pd.txres` `pd.gcrc` `pd.dup` `pd.rxraw` | protocol layer, docs/spec.md §7 (`pd.txres` res=fail may carry `i2c:true` / `coll:true`) |
| `pd.rej` `pd.sr` `pd.hr` `pd.giveup` `pd.want` `contract` `contract.lost` | policy/SM, docs/spec.md §7 |
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
- `src/pd_msg.{c,h}` — L2 codec: header, classify, PDO decode, select, RDO, TX burst (pure)
- `src/pd_drv.{c,h}` — L1 driver poll (§2.1): IRQ/status reads, TX result, VBUS edge, RX FIFO,
  MessageID/dup filter; posts events into the SM queue
- `src/pd_sm.{c,h}` — table-driven SM per docs/spec.md §3-5 (16 states, 85 rows), pure C,
  hardware via `struct pd_port_ops`
- `src/console.{c,h}`, `src/main.c` — ESP-IDF glue; single task, 1 ms poll loop
- Host sim: `make -C test/host test` (fake FUSB302 + scenario runner): test_msg = T38 codec vectors,
  test_sm = T01-T37 + mechanism checks; prints the worst-case I2C load per step
