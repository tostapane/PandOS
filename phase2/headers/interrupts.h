#ifndef PANDOS_INTERRUPTS_H
#define PANDOS_INTERRUPTS_H

#include "../../headers/const.h"
#include "../../headers/types.h"
#include "./initial.h"
#include <stdbool.h>
#include <uriscv/liburiscv.h>

#define START_ADDR 0x100000054
#define BITMAP_BASE 0x10000040
volatile unsigned int *bitmap = (unsigned int *)BITMAP_BASE;
void handleInterrupt(void);

// gestione non-timer interrupts
void deviceInterrupt(unsigned int);

void PLTInterrupt(void);

void ITInterrupt(void);
#endif // PANDOS_INTERRUPTS_H
