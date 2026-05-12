# Makefile for PandOSsh Phase 3 U-procs
XT_PRG_PREFIX = riscv64-unknown-elf-
CC = $(XT_PRG_PREFIX)gcc
LD = $(XT_PRG_PREFIX)ld
EF = uriscv-elf2uriscv
UDEV = uriscv-mkdev

# uRISCV-related paths
URISCV_DATA_DIR = /usr/local/share/uriscv
URISCV_INCLUDE_DIR = /usr/local/include

# Project paths
# We are inside phase3/testers/
HEADERS_DIR = ../../headers
P3_HEADERS_DIR = ../headers

# Compiler and Linker flags
INCLUDES = -I$(URISCV_INCLUDE_DIR) -I$(HEADERS_DIR) -I$(P3_HEADERS_DIR)
CFLAGS = -ffreestanding -static -nostdlib -Wall -O0 -march=rv32imafd -mabi=ilp32d $(INCLUDES)
LDAOUTFLAGS = -G 0 -nostdlib -T $(URISCV_DATA_DIR)/uriscvaout.ldscript -march=rv32imafd -melf32lriscv

# Targets
all: shell.uriscv calc.uriscv

# Support objects
crti.o: $(URISCV_DATA_DIR)/crti.S
	$(CC) $(CFLAGS) -c $< -o $@

liburiscv.o: $(URISCV_DATA_DIR)/liburiscv.S
	$(CC) $(CFLAGS) -c $< -o $@

utils.o: utils.c
	$(CC) $(CFLAGS) -c $< -o $@

# Pattern rules for tester programs
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.t: %.o utils.o crti.o liburiscv.o
	$(LD) $(LDAOUTFLAGS) crti.o $< utils.o liburiscv.o -o $@

%.t.aout.uriscv: %.t
	$(EF) -a $<

%.uriscv: %.t.aout.uriscv
	$(UDEV) -f $@ $<

clean:
	rm -f *.o *.t *.uriscv
