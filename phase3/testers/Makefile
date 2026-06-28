# Makefile per gli U-proc di PandOSsh Phase 3 (adattato per Debian)
#
# Differenze rispetto al Makefile fornito dai prof:
#  - Toolchain: su questa macchina e' installata solo riscv64-unknown-elf-
#    (riscv32-unknown-linux-gnu- e' assente). Il march/abi rv32 e' imposto via flag.
#  - Header uriscv in /usr/include, ldscript e sorgenti in /usr/local/share/uriscv.
#  - Ogni tester viene linkato con print.o E utils.o (i tester usano sia print()
#    sia myStrlen/myStrcmp/itoa).
#  - crti.o e liburiscv.o vengono ricompilati dai .S con lo stesso march/abi dei
#    tester, per garantire ABI coerente.

XT_PRG_PREFIX = riscv64-unknown-elf-
CC = $(XT_PRG_PREFIX)gcc
LD = $(XT_PRG_PREFIX)ld
EF = uriscv-elf2uriscv
UDEV = uriscv-mkdev

# uRISCV-related paths (Debian)
URISCV_DATA_DIR = /usr/local/share/uriscv
URISCV_INCLUDE_DIR = /usr/include

# Project paths (siamo dentro phase3/testers/)
HEADERS_DIR = ../../headers
P3_HEADERS_DIR = ../headers

INCLUDES = -I$(URISCV_INCLUDE_DIR) -I$(HEADERS_DIR) -I$(P3_HEADERS_DIR)
CFLAGS = -ffreestanding -static -nostdlib -Wall -O0 -march=rv32imafd -mabi=ilp32d $(INCLUDES)
LDAOUTFLAGS = -G 0 -nostdlib -m elf32lriscv -T $(URISCV_DATA_DIR)/uriscvaout.ldscript

# print.c e utils.c sono librerie di supporto, NON programmi: niente .uriscv per loro.
all: shell.uriscv calc.uriscv date.uriscv echo.uriscv fibEight.uriscv fibEleven.uriscv sl.uriscv uname.uriscv

# Oggetti di supporto ricompilati dai sorgenti uriscv
crti.o: $(URISCV_DATA_DIR)/crti.S
	$(CC) $(CFLAGS) -c $< -o $@

liburiscv.o: $(URISCV_DATA_DIR)/liburiscv.S
	$(CC) $(CFLAGS) -c $< -o $@

# Regola generica per i .o dei tester e per print.o/utils.o (sorgenti locali)
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.t: %.o print.o utils.o crti.o liburiscv.o
	$(LD) $(LDAOUTFLAGS) crti.o $< print.o utils.o liburiscv.o -o $@

%.t.aout.uriscv: %.t
	$(EF) -a $<

%.uriscv: %.t.aout.uriscv
	$(UDEV) -f $@ $<

clean:
	rm -f *.o *.t *.uriscv *.t.aout.uriscv
