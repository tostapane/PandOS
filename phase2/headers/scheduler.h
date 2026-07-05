#ifndef PANDOS_SCHEDULER_H
#define PANDOS_SCHEDULER_H

#include "../../phase1/headers/asl.h"
#include "../../phase1/headers/pcb.h"
#include "./initial.h"
#include <uriscv/const.h>
#include <uriscv/types.h>

/**
 * @brief Esegue lo scheduling dei processi (Round-Robin preemptivo).
 *
 * Il Nucleo implementa un semplice algoritmo di scheduling assegnando a ciascun
 * processo in readyQueue un "time slice" massimo di 5 millisecondi (TIMESLICE).
 */
void scheduler(void);

#endif // PANDOS_SCHEDULER_H