#ifndef PANDOS_INTERRUPTS_H
#define PANDOS_INTERRUPTS_H

#include "../../headers/const.h"
#include "../../headers/types.h"
#include "./initial.h"

#include <stdbool.h>
#include <uriscv/liburiscv.h>

#define START_ADDR 0x10000054
#define BITMAP_BASE 0x10000040

/**
 * @brief Gestore principale degli interrupt hardware.
 *
 * Identifica la natura dell'interrupt (Timer o Dispositivi) a partire
 * dal registro CAUSE e delega la gestione alla routine corrispondente.
 */
void interruptHandler(void);

/**
 * @brief Gestore degli interrupt generati dai dispositivi esterni.
 *
 * @param intlineNo Linea di interrupt che ha segnalato l'eccezione (da 3 a 7).
 */
void deviceInterrupt(unsigned int intlineNo);

/**
 * @brief Gestore dell'interrupt causato dal Process Local Timer (PLT).
 *
 * Segnala l'esaurimento del Time Slice assegnato al processo corrente.
 */
void PLTInterrupt(void);

/**
 * @brief Gestore dell'interrupt causato dall'Interval Timer di sistema.
 *
 * Sblocca i processi in attesa sullo pseudoclock ogni 100 millisecondi.
 */
void ITInterrupt(void);

#endif // PANDOS_INTERRUPTS_H