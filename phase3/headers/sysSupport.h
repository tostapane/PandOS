#ifndef SYSSUPPORT_H
#define SYSSUPPORT_H

#include "../../headers/const.h"
#include "../../headers/types.h"

/**
 * Definizioni e costanti per il supporto alle eccezioni di sistema.
 * Questo header definisce l'interfaccia per la gestione delle chiamate di sistema
 * e delle terminazioni dei processi utente.
 */

#define START_ADDR 0x10000054
#define READTERMINAL 5
#define EXECUTE 6

/* Handler esterno per la gestione del rimpiazzo delle pagine */
extern void Pager();

/**
 * Entry point per le eccezioni non legate al TLB passate dal Nucleus.
 * Coordina la distribuzione tra SYSCALL e Program Trap.
 */
void GeneralExceptionHandler();

/**
 * Gestisce le system call invocate dai processi utente (SYSCALL >= 1).
 * @param supStruct Puntatore alla struttura di supporto del processo chiamante.
 * @param excCode Codice identificativo dell'eccezione.
 */
void SyscallExceptionHandler(support_t *supStruct, unsigned int excCode);

/**
 * Gestisce la terminazione, sia normale che anomala, di un processo utente.
 * @param supStruct Puntatore alla struttura di supporto del processo da terminare.
 */
void ProgramTrapHandler(support_t *supStruct);

#endif
