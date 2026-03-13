#ifndef PANDOS_EXCEPTIONS_H
#define PANDOS_EXCEPTIONS_H

#include "../../../uriscv-latest/src/include/uriscv/cpu.h"
#include "../../headers/const.h"
#include "../../headers/types.h"
#include "../../phase1/headers/asl.h"
#include "../../phase1/headers/pcb.h"
#include "./interrupts.h"
#include "./scheduler.h"
#include <uriscv/liburiscv.h>

// SEZIONE 2.1: Dichiarazione delle variabili globali del Nucleo

/**
 * @brief Conteggio dei processi avviati ma non ancora terminati.
 * Incrementato da `CreateProcess`, decrementato durante la terminazione.
 */
extern int process_count;

/**
 * @brief Conteggio dei processi bloccati in attesa di I/O o del timer
 * (soft-blocked). Incrementato quando un processo si blocca su un semaforo di
 * device/timer, decrementato quando viene sbloccato o terminato.
 */
extern int soft_block_count;

/**
 * @brief Coda dei processi pronti per essere eseguiti (in stato "ready").
 * Gestita dallo scheduler e dalle funzioni che sbloccano i processi.
 */
extern struct list_head ready_queue;

/**
 * @brief Puntatore al PCB del processo attualmente in esecuzione (in stato
 * "running"). Se `NULL`, nessun processo è in esecuzione.
 */
extern pcb_t *current_process;

/**
 * @brief Array dei semafori per i dispositivi e per il pseudo-clock.
 * L'indice del semaforo corrisponde a un device specifico.
 * Dimensione: 48 (device) + 1 (pseudo-clock) = 49.
 * L'ordine è: 8 dischi, 8 flash, 8 network, 8 stampanti, 8 terminali
 * (ricezione), 8 terminali (trasmissione), 1 pseudo-clock.
 */
// int dev_semaphores[TOT_DEV_SEM];

/**
 * @brief Punto di ingresso principale per la gestione di tutte le eccezioni.
 *
 * Questa funzione viene chiamata dal BIOS ogni volta che si verifica
 * un'eccezione (esclusi i TLB-Refill). Il suo compito è determinare la causa
 * dell'eccezione e delegare il lavoro al gestore appropriato.
 */
void exceptionHandler(void);

/**
 * @brief Gestore per le eccezioni di tipo SYSCALL.
 *
 * Questa funzione gestisce le richieste di servizi del Nucleo fatte dai
 * processi (SYSCALL da -1 a -10).
 */
void syscallHandler(void);

/**
 * @brief Gestore per le eccezioni di tipo Program Trap.
 *
 * Gestisce errori di programma come istruzioni illegali, errori di
 * indirizzamento, ecc. Implementa la logica "Pass Up or Die".
 */
void programTrapHandler(void);

/**
 * @brief Gestore per le eccezioni relative alla TLB (Translation Lookaside
 * Buffer).
 *
 * Gestisce errori di traduzione degli indirizzi.
 * Implementa la logica "Pass Up or Die".
 */
void tlbHandler(void);

/**
 * @brief Gestore "placeholder" per gli eventi di TLB-Refill.
 *
 * Questa funzione è un gestore speciale che viene chiamato solo per eventi di
 * TLB-Refill. Per la Fase 2, il suo codice è fisso.
 */
void uTLB_RefillHandler(void);

#endif // PANDOS_EXCEPTIONS_H
