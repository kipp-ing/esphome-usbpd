/* Line command reader on the USB-Serial-JTAG console. Replies are JSONL {"ev":"cmd",...}.
 * Commands: help | regs | rd <reg> | wr <reg> <val> | pd want <mV> | reboot
 * Numbers accept decimal or 0x-hex. */
#ifndef USBPD_CONSOLE_H
#define USBPD_CONSOLE_H

#include "pd_sm.h"

void console_init(pd_sm_t *sm);
void console_feed(char c);          /* one input byte */
void console_exec(const char *line);

#endif
