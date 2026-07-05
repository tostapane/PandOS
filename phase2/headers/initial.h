#ifndef PANDOS_INITIAL_H
#define PANDOS_INITIAL_H

#include "../../phase1/headers/asl.h"
#include "../../phase1/headers/pcb.h"

/**
 * @brief Contatore globale dei processi.
 * Numero di processi avviati e non ancora terminati.
 */
extern unsigned int processCount;

/**
 * @brief Contatore globale dei processi bloccati.
 * Numero di processi che si trovano in stato "bloccato" a causa
 * di un'operazione di I/O o di una richiesta al timer.
 */
extern unsigned int softBlockCount;

/**
 * @brief Coda dei processi in stato "ready".
 * Contiene i PCB dei processi pronti all'esecuzione.
 */
extern struct list_head readyQueue;

/**
 * @brief Processo in esecuzione.
 * Puntatore al PCB del processo che sta attualmente occupando la CPU.
 */
extern pcb_t *currProc;

/**
 * @brief Semafori dei dispositivi.
 * Array di semafori utilizzato per la sincronizzazione dei dispositivi esterni.
 * L'ultimo elemento funge da pseudoclock.
 */
extern int subDevice[NRSEMAPHORES];

/**
 * @brief Semaforo dello pseudoclock.
 * Utilizzato per operazioni di wait specifiche allo pseudoclock.
 */
extern unsigned int pseudoClock;

/**
 * @brief Timer del processo.
 * Variabile utilizzata per salvare il tempo di sistema nel momento in cui
 * un processo riceve il controllo della CPU.
 */
extern cpu_t processTimer;

/**
 * @brief Funzione di test fornita per la validazione della Phase 2.
 * Questa funzione è il punto di ingresso per il test del Nucleo.
 */
extern void test(void);

/**
 * @brief Gestore degli eventi di TLB Refill.
 * (Al momento usata come placeholder per la Fase 3).
 */
extern void uTLB_RefillHandler(void);

/**
 * @brief Punto di ingresso principale per le eccezioni.
 * Riceve il controllo all'insorgere di qualsiasi eccezione eccetto i TLB miss.
 */
extern void exceptionHandler(void);

/**
 * @brief Motore di scheduling dei processi (Round-Robin preemptivo).
 * Sceglie il prossimo processo da eseguire e gli assegna il controllo.
 */
extern void scheduler(void);

#endif // PANDOS_INITIAL_H
