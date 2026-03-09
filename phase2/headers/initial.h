#ifndef PANDOS_INITIAL_H
#define PANDOS_INITIAL_H

#include "../../phase1/headers/asl.h"
#include "../../phase1/headers/pcb.h"

// the number of started, but not yet terminated processes
extern unsigned int processCount;

// the number of started, but not terminated processes
// that are in the "blocked" state due to an I/O or timer request
extern unsigned int softBlockCount;

// a queue of PCBs that are in "ready" state
extern struct list_head readyQueue;

// the current executing process
extern pcb_t *currProc;

// semaphore for external (sub)device
extern unsigned int subDevice[NRSEMAPHORES];

// semaphore to support the pseudo clock
extern unsigned int pseudoClock;

extern void test(void);
extern void uTLB_RefillHandler(void);
extern void exceptionHandler(void);
extern void scheduler(void);

#endif // PANDOS_INITIAL_H
