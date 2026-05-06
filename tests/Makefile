CC = cc
CFLAGS = -Wall -Wextra -Werror -std=c99 -I..

LEGACY_SRCS = ../protocol/walkingpad_protocol.c test_protocol.c
FTMS_SRCS = ../protocol/walkingpad_ftms.c test_ftms.c

.PHONY: all clean run run_legacy run_ftms

all: test_protocol test_ftms

test_protocol: $(LEGACY_SRCS) minunit.h
	$(CC) $(CFLAGS) -o $@ $(LEGACY_SRCS)

test_ftms: $(FTMS_SRCS) minunit.h
	$(CC) $(CFLAGS) -o $@ $(FTMS_SRCS)

run: run_legacy run_ftms

run_legacy: test_protocol
	./test_protocol

run_ftms: test_ftms
	./test_ftms

clean:
	rm -f test_protocol test_ftms
