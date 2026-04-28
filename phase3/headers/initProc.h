#ifndef INITPROC_H
#define INITPROC_H

#include "../../headers/types.h"
#include "../../headers/const.h"

/* Exported global data structures for Phase 3 [cite: 382] */
extern swap_t swapPool[POOLSIZE];
extern int swapSemaphore;
extern int masterSemaphore;
extern int shellSemaphore;

/* Array of semaphores for peripheral I/O devices [cite: 292] */
extern int devSemaphores[NSUPPSEM];

/* Array of Support Structures for up to 8 U-procs [cite: 323] */
extern support_t supStructs[UPROCMAX];

/* The Instantiator Process  */
void test();

#endif