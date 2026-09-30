/* FUSB302 thin I2C HAL (ESP-IDF). No init policy — that comes from docs/spec.md. */
#ifndef USBPD_FUSB302_H
#define USBPD_FUSB302_H

#include <stdint.h>
#include <stddef.h>
#include "fusb302_regs.h"

#define FUSB302_I2C_SDA   21
#define FUSB302_I2C_SCL   22
#define FUSB302_I2C_HZ    400000   /* FUSB302 supports Fast-mode (400 kHz) */

/* All functions return 0 on success, negative esp_err_t-ish code on failure,
 * and log {"ev":"i2c.err",...} on failure. */
int fusb302_init_bus(void);                       /* I2C master bus + device handle */
int fusb302_read(uint8_t reg, uint8_t *val);
int fusb302_write(uint8_t reg, uint8_t val);
int fusb302_read_burst(uint8_t reg, uint8_t *buf, size_t n);   /* auto-increment (except FIFOS) */
int fusb302_write_burst(uint8_t reg, const uint8_t *buf, size_t n);
int fusb302_fifo_read(uint8_t *buf, size_t n);    /* n bytes from RX FIFO (0x43) */
int fusb302_fifo_write(const uint8_t *buf, size_t n); /* n bytes into TX FIFO (0x43) */
int fusb302_device_id(uint8_t *id);

uint32_t fusb302_err_count(void);

#endif
