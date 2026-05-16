#ifndef INITPROC_H
#define INITPROC_H

#include "../../headers/const.h"
#include "../../headers/types.h"

/**
 * Strutture dati ed esportazioni per il processo Instantiator (test).
 * Definisce i semafori globali, la swap pool e le funzioni di allocazione per la Fase 3.
 */

/* Swap Pool globale per la gestione dei frame di memoria virtuale */
extern swap_t swapPool[POOLSIZE];

/* Semaforo per la mutua esclusione sull'accesso alla Swap Pool */
extern int swapSemaphore;

/* Semaforo master per sincronizzare lo shutdown del sistema con la shell */
extern int masterSemaphore;

/* Semaforo di controllo per la sincronizzazione tra shell e processi figli */
extern int shellSemaphore;

/* ASID del processo che detiene il lock sulle tabelle delle pagine */
extern int page_mutex_holder;

/* Array di semafori per il controllo dei dispositivi periferici I/O */
extern int devSemaphores[NSUPPSEM];

/* Strutture di supporto per il numero massimo di processi utente consentiti */
extern support_t supStructs[UPROCMAX];

/**
 * Alloca una struttura di supporto libera dalla lista globale.
 * @return Puntatore alla struttura allocata o NULL se non disponibili.
 */
support_t *allocateSupport();

/**
 * Rilascia una struttura di supporto rendendola nuovamente disponibile.
 * @param s Puntatore alla struttura da deallocare.
 */
void deallocateSupport(support_t *s);

/**
 * Entry point del processo Instantiator (test).
 * Inizializza l'ambiente della Fase 3 e lancia la shell.
 */
void test();

#endif
