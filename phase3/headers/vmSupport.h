#ifndef VMSUPPORT_H
#define VMSUPPORT_H

#include "../../headers/types.h"
#include "../../headers/const.h"

/* Codice eccezione TLB-Modification in uriscv (EXC_MOD in <uriscv/cpu.h>).
 * Il Nucleus instrada gli exCode 24..28 al TLB handler / Pager, quindi qui va
 * confrontato con 24, NON con 1. */
#define EXC_TLBMOD 24
#define INTLINE_DISK 3
#define INTLINE_FLASH 4

/* * The initialization function for the Swap Pool.
 * Your Instantiator Process MUST call this before launching any U-procs.
 */
void initSwapStructs();

/* * The Pager: The TLB exception handler.
 * This gets invoked when a process tries to access memory that isn't actually in RAM.
 */
void Pager();

#endif