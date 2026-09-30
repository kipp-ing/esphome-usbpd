idf_component_register(
    SRCS "main.c" "log.c" "fusb302.c" "console.c" "pd_sm.c" "pd_drv.c" "pd_msg.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_driver_i2c esp_driver_usb_serial_jtag esp_timer esp_system
)
