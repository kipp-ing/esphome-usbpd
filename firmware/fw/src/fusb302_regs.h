/* FUSB302 register map — addresses and bit masks, straight from the datasheet
 * (PCB-pi-r0/tmp/pdfs/fusb302mpx.txt, "Register Definitions" p.18-19, Tables 5-24).
 * Pure defines, no policy. Shared by firmware and host sim. */
#ifndef USBPD_FUSB302_REGS_H
#define USBPD_FUSB302_REGS_H

#define FUSB302_I2C_ADDR     0x22   /* 7-bit */

#define FUSB_DEVICE_ID       0x01   /* R    rst 8x : Version ID[7:4], Revision ID[3:0] */
#define FUSB_SWITCHES0       0x02   /* R/W  rst 03 */
#define FUSB_SWITCHES1       0x03   /* R/W  rst 20 */
#define FUSB_MEASURE         0x04   /* R/W  rst 31 */
#define FUSB_SLICE           0x05   /* R/W  rst 60 */
#define FUSB_CONTROL0        0x06   /* R/W/C rst 24 */
#define FUSB_CONTROL1        0x07   /* R/W/C rst 00 */
#define FUSB_CONTROL2        0x08   /* R/W  rst 02 */
#define FUSB_CONTROL3        0x09   /* R/W  rst 06 */
#define FUSB_MASK1           0x0A   /* R/W  rst 00 */
#define FUSB_POWER           0x0B   /* R/W  rst 01 */
#define FUSB_RESET           0x0C   /* W/C  rst 00 */
#define FUSB_OCPREG          0x0D   /* R/W  rst 0F */
#define FUSB_MASKA           0x0E   /* R/W  rst 00 */
#define FUSB_MASKB           0x0F   /* R/W  rst 00 */
#define FUSB_CONTROL4        0x10   /* R/W  rst 00 (TOG_USRC_EXIT) */
#define FUSB_STATUS0A        0x3C   /* R */
#define FUSB_STATUS1A        0x3D   /* R */
#define FUSB_INTERRUPTA      0x3E   /* R/C */
#define FUSB_INTERRUPTB      0x3F   /* R/C */
#define FUSB_STATUS0         0x40   /* R */
#define FUSB_STATUS1         0x41   /* R    rst 28 */
#define FUSB_INTERRUPT       0x42   /* R/C */
#define FUSB_FIFOS           0x43   /* R/W  no auto-increment */

/* Switches0 */
#define FUSB_SW0_PU_EN2      0x80
#define FUSB_SW0_PU_EN1      0x40
#define FUSB_SW0_VCONN_CC2   0x20
#define FUSB_SW0_VCONN_CC1   0x10
#define FUSB_SW0_MEAS_CC2    0x08
#define FUSB_SW0_MEAS_CC1    0x04
#define FUSB_SW0_PDWN2       0x02
#define FUSB_SW0_PDWN1       0x01
/* Switches1 */
#define FUSB_SW1_POWERROLE   0x80
#define FUSB_SW1_SPECREV1    0x40
#define FUSB_SW1_SPECREV0    0x20
#define FUSB_SW1_DATAROLE    0x10
#define FUSB_SW1_AUTO_CRC    0x04
#define FUSB_SW1_TXCC2       0x02
#define FUSB_SW1_TXCC1       0x01
/* Measure */
#define FUSB_MEAS_VBUS       0x40
#define FUSB_MEAS_MDAC_MASK  0x3F
/* Control0 */
#define FUSB_CTL0_TX_FLUSH   0x40
#define FUSB_CTL0_INT_MASK   0x20
#define FUSB_CTL0_HOST_CUR_MASK 0x0C
#define FUSB_CTL0_AUTO_PRE   0x02
#define FUSB_CTL0_TX_START   0x01
/* Control1 */
#define FUSB_CTL1_ENSOP2DB   0x40
#define FUSB_CTL1_ENSOP1DB   0x20
#define FUSB_CTL1_BIST_MODE2 0x10
#define FUSB_CTL1_RX_FLUSH   0x04
#define FUSB_CTL1_ENSOP2     0x02
#define FUSB_CTL1_ENSOP1     0x01
/* Control2 */
#define FUSB_CTL2_TOG_SAVE_PWR_MASK 0xC0
#define FUSB_CTL2_TOG_RD_ONLY 0x20
#define FUSB_CTL2_WAKE_EN    0x08
#define FUSB_CTL2_MODE_MASK  0x06
#define FUSB_CTL2_TOGGLE     0x01
/* Control3 */
#define FUSB_CTL3_SEND_HARD_RESET 0x40
#define FUSB_CTL3_AUTO_HARDRESET  0x10
#define FUSB_CTL3_AUTO_SOFTRESET  0x08
#define FUSB_CTL3_N_RETRIES_MASK  0x06
#define FUSB_CTL3_AUTO_RETRY      0x01
/* Power */
#define FUSB_PWR_MASK        0x0F
/* Reset */
#define FUSB_RESET_PD_RESET  0x02
#define FUSB_RESET_SW_RES    0x01
/* Status0a */
#define FUSB_ST0A_SOFTFAIL   0x20
#define FUSB_ST0A_RETRYFAIL  0x10
#define FUSB_ST0A_POWER_MASK 0x0C
#define FUSB_ST0A_SOFTRST    0x02
#define FUSB_ST0A_HARDRST    0x01
/* Status1a */
#define FUSB_ST1A_TOGSS_MASK 0x38
#define FUSB_ST1A_RXSOP2DB   0x04
#define FUSB_ST1A_RXSOP1DB   0x02
#define FUSB_ST1A_RXSOP      0x01
/* Interrupta */
#define FUSB_IA_OCP_TEMP     0x80
#define FUSB_IA_TOGDONE      0x40
#define FUSB_IA_SOFTFAIL     0x20
#define FUSB_IA_RETRYFAIL    0x10
#define FUSB_IA_HARDSENT     0x08
#define FUSB_IA_TXSENT       0x04
#define FUSB_IA_SOFTRST      0x02
#define FUSB_IA_HARDRST      0x01
/* Interruptb */
#define FUSB_IB_GCRCSENT     0x01
/* Status0 */
#define FUSB_ST0_VBUSOK      0x80
#define FUSB_ST0_ACTIVITY    0x40
#define FUSB_ST0_COMP        0x20
#define FUSB_ST0_CRC_CHK     0x10
#define FUSB_ST0_ALERT       0x08
#define FUSB_ST0_WAKE        0x04
#define FUSB_ST0_BC_LVL_MASK 0x03
/* Status1 */
#define FUSB_ST1_RXSOP2      0x80
#define FUSB_ST1_RXSOP1      0x40
#define FUSB_ST1_RX_EMPTY    0x20
#define FUSB_ST1_RX_FULL     0x10
#define FUSB_ST1_TX_EMPTY    0x08
#define FUSB_ST1_TX_FULL     0x04
#define FUSB_ST1_OVRTEMP     0x02
#define FUSB_ST1_OCP         0x01
/* Interrupt */
#define FUSB_I_VBUSOK        0x80
#define FUSB_I_ACTIVITY      0x40
#define FUSB_I_COMP_CHNG     0x20
#define FUSB_I_CRC_CHK       0x10
#define FUSB_I_ALERT         0x08
#define FUSB_I_WAKE          0x04
#define FUSB_I_COLLISION     0x02
#define FUSB_I_BC_LVL        0x01

/* All documented registers, in address order (for "regs" dumps). */
#define FUSB_REG_LIST \
    FUSB_DEVICE_ID, FUSB_SWITCHES0, FUSB_SWITCHES1, FUSB_MEASURE, FUSB_SLICE, \
    FUSB_CONTROL0, FUSB_CONTROL1, FUSB_CONTROL2, FUSB_CONTROL3, FUSB_MASK1, \
    FUSB_POWER, FUSB_OCPREG, FUSB_MASKA, FUSB_MASKB, FUSB_CONTROL4, \
    FUSB_STATUS0A, FUSB_STATUS1A, FUSB_STATUS0, FUSB_STATUS1
/* NOTE: RESET (W/C), the three R/C interrupt registers and FIFOS are
 * deliberately NOT in the dump list: reading them has side effects
 * (clears pending interrupts / pops RX FIFO). Read them explicitly with `rd`. */

#endif
