#ifndef PANDOS_SCHEDULER_H
#define PANDOS_SCHEDULER_H

#include "../../phase1/headers/asl.h"
#include "../../phase1/headers/pcb.h"
#include "./initial.h"
#include <uriscv/const.h>
#include <uriscv/types.h>

/* 
 * The nucleus should implement a simple preemptive round-robin scheduling
 * algorithm with a time slice value of 5 mills (const TIMESLICE).
 */
void scheduler(void);

#endif // PANDOS_SCHEDULER_H