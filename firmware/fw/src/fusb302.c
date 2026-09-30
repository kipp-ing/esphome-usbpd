#include "fusb302.h"
#include "log.h"
#include <string.h>
#include "driver/i2c_master.h"

#define I2C_TIMEOUT_MS 10
#define BURST_MAX 64

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev;
static uint32_t s_errs;

static int err(const char *op, uint8_t reg, size_t n, esp_err_t rc)
{
    s_errs++;
    log_line_t l;
    log_begin(&l, "i2c.err");
    log_str(&l, "op", op);
    log_hex8(&l, "reg", reg);
    log_uint(&l, "n", (uint32_t)n);
    log_int(&l, "rc", rc);
    log_str(&l, "rc_name", esp_err_to_name(rc));
    log_uint(&l, "errs", s_errs);
    log_end(&l);
    return rc < 0 ? rc : -(int)rc;
}

int fusb302_init_bus(void)
{
    i2c_master_bus_config_t bc = {
        .i2c_port = -1,
        .sda_io_num = FUSB302_I2C_SDA,
        .scl_io_num = FUSB302_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t rc = i2c_new_master_bus(&bc, &s_bus);
    if (rc != ESP_OK) return err("bus", 0, 0, rc);
    i2c_device_config_t dc = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = FUSB302_I2C_ADDR,
        .scl_speed_hz = FUSB302_I2C_HZ,
    };
    rc = i2c_master_bus_add_device(s_bus, &dc, &s_dev);
    if (rc != ESP_OK) return err("dev", 0, 0, rc);
    return 0;
}

int fusb302_read_burst(uint8_t reg, uint8_t *buf, size_t n)
{
    if (!s_dev) return err("rd", reg, n, ESP_ERR_INVALID_STATE);
    esp_err_t rc = i2c_master_transmit_receive(s_dev, &reg, 1, buf, n, I2C_TIMEOUT_MS);
    if (rc != ESP_OK) return err("rd", reg, n, rc);
    return 0;
}

int fusb302_write_burst(uint8_t reg, const uint8_t *buf, size_t n)
{
    if (!s_dev) return err("wr", reg, n, ESP_ERR_INVALID_STATE);
    if (n > BURST_MAX) return err("wr", reg, n, ESP_ERR_INVALID_SIZE);
    uint8_t tmp[BURST_MAX + 1];
    tmp[0] = reg;
    memcpy(tmp + 1, buf, n);
    esp_err_t rc = i2c_master_transmit(s_dev, tmp, n + 1, I2C_TIMEOUT_MS);
    if (rc != ESP_OK) return err("wr", reg, n, rc);
    return 0;
}

int fusb302_read(uint8_t reg, uint8_t *val) { return fusb302_read_burst(reg, val, 1); }
int fusb302_write(uint8_t reg, uint8_t val) { return fusb302_write_burst(reg, &val, 1); }
int fusb302_fifo_read(uint8_t *buf, size_t n) { return fusb302_read_burst(FUSB_FIFOS, buf, n); }
int fusb302_fifo_write(const uint8_t *buf, size_t n) { return fusb302_write_burst(FUSB_FIFOS, buf, n); }
int fusb302_device_id(uint8_t *id) { return fusb302_read(FUSB_DEVICE_ID, id); }
uint32_t fusb302_err_count(void) { return s_errs; }
