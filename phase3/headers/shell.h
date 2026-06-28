#ifndef SHELL_H
#define SHELL_H

#include <uriscv/liburiscv.h>

#define NUM_PROGRAMS 7


/* Struttura che definisce un comando, composto da
 * il suo nome e il suo ASID (Address Space Identifier) */
typedef struct command {
  char *name;
  int asid;
} command;

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
