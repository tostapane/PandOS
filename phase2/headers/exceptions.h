#ifndef PANDOS_EXCEPTIONS_H
#define PANDOS_EXCEPTIONS_H

#include "../../headers/const.h"
#include "../../headers/types.h"
#include "../../phase1/headers/asl.h"
#include "../../phase1/headers/pcb.h"
#include "./interrupts.h"
#include "./scheduler.h"
#include "initial.h"
#include <uriscv/cpu.h>
#include <uriscv/liburiscv.h>

void uTLB_RefillHandler(void);
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
 * @brief Copia un blocco di memoria da una sorgente a una destinazione.
 *
 * @param dest Puntatore alla destinazione della copia.
 * @param src Puntatore alla sorgente della copia.
 * @param n Numero di byte da copiare.
 * @return Puntatore alla destinazione.
 */
void *memcpy(void *dest, const void *src, unsigned int n);

/**
 * @brief Funzione ausiliaria ricorsiva per la ricerca di un PCB all'interno
 * dell'albero genealogico.
 *
 * @param root Puntatore al nodo radice da cui iniziare la ricerca (solitamente
 * `root_proc`).
 * @param pid PID da cercare.
 * @return Puntatore al PCB trovato o `NULL`.
 */
static pcb_t *find_pcb_recursive(pcb_t *root, int pid);

/**
 * @brief Cerca e restituisce il descrittore del processo (PCB) corrispondente a
 * un dato PID. Tenta di individuare il processo navigando l'albero genealogico
 * partendo dalla root locale in uso.
 *
 * @param pid L'identificativo numerico (Process ID) che stiamo cercando.
 * @return Puntatore al blocco di controllo (`pcb_t *`) se trovato, altrimenti
 * `NULL`.
 */
static pcb_t *find_pcb(int pid);

/**
 * @brief Distrugge in modo ricorsivo il processo indicato e l'intera sua stirpe
 * (tutti i figli derivati). Si occupa di de-allocare i PCB rimuovendoli da
 * qualsiasi coda o semaforo essi siano legati.
 *
 * @param proc Puntatore al PCB bersaglio che fa da "nodo radice" da epurare per
 * la terminazione.
 */
static void recursive_terminate(pcb_t *proc);

#endif // PANDOS_EXCEPTIONS_H
