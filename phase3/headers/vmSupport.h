#ifndef VMSUPPORT_H
#define VMSUPPORT_H

#include "../../headers/types.h"
#include "../../headers/const.h"

#define EXC_TLBMOD 1
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