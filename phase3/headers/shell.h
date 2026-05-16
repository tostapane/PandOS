#ifndef SHELL_H
#define SHELL_H

#include <uriscv/liburiscv.h>

/**
 * Definizioni per l'interprete dei comandi (shell).
 * Gestisce la mappatura tra nomi dei programmi e i loro identificativi di
 * spazio indirizzi.
 */

#define NUM_PROGRAMS 7

/**
 * Rappresenta un programma eseguibile dalla shell.
 */
typedef struct command {
  char *name; /* Nome del comando inserito dall'utente */
  int asid;   /* ASID associato al dispositivo Flash contenente il binario */
} command;

/* Elenco dei programmi disponibili per l'esecuzione */
extern command programs[];

/* La shell stampa la riga "PandOSsh>> ", poi esegue la funzione SYSCALL
 * READTERMINAL, che permette all'utente di scrivere e blocca la shell finche
 * quest ultimo non preme invio.
 *
 * Viene analizzato il comando inserito dall'utente:
 * - la shell termina se il comando inserito è 'exit'.
 * - altrimenti effettua una ricerca del comando digitato.
 *   - se il comando viene trovato viene eseguito
 *   - altrimenti viene stampato un messaggio d'errore.
 * */

#endif
