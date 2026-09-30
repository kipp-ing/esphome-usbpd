# Host sim: builds the pure-C firmware core against a fake FUSB302 and runs tests.
#   make -C test/host test
CC      ?= cc
FW      := ../../fw/src
CFLAGS  ?= -std=c11 -Wall -Wextra -Wno-missing-field-initializers -Werror -g -O0 -fsanitize=address,undefined
CFLAGS  += -I$(FW) -I.
BUILD   := build

# Firmware sources that are pure C (no ESP-IDF). Add pd_msg.c etc. here.
FW_SRCS := $(FW)/pd_sm.c $(FW)/log.c
SIM_SRCS := fake_port.c scenario.c
TESTS   := test_sm

.PHONY: all test clean
all: $(addprefix $(BUILD)/,$(TESTS))

$(BUILD)/%: %.c $(FW_SRCS) $(SIM_SRCS) $(wildcard *.h) $(wildcard $(FW)/*.h)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ $< $(FW_SRCS) $(SIM_SRCS)

test: all
	@set -e; for t in $(TESTS); do echo "== $$t"; ./$(BUILD)/$$t; done; echo "ALL HOST TESTS PASSED"

clean:
	rm -rf $(BUILD)
