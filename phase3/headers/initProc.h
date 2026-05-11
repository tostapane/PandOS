#ifndef INITPROC_H
#define INITPROC_H

#include "../../headers/const.h"
#include "../../headers/types.h"

/* Exported global data structures for Phase 3 */
extern swap_t swapPool[POOLSIZE];
extern int swapSemaphore;
extern int masterSemaphore;
extern int shellSemaphore;

extern int page_mutex_holder;
/* Array of semaphores for peripheral I/O devices */
extern int devSemaphores[NSUPPSEM];

/* Array of Support Structures for up to 8 U-procs */
extern support_t supStructs[UPROCMAX];

/* The Instantiator Process  */
void test();

#endif
