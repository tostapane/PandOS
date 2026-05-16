#ifndef TCONST_H
#define TCONST_H

/**
 * Costanti per il Support Level.
 * Definisce i codici delle system call disponibili per i processi utente.
 */

#define EOS '\0' /* End Of String */

/* Codici identificativi per le System Call della Fase 3 */
#define GET_TOD       1 /* Get Time of Day */
#define TERMINATE     2 /* Terminate Process */
#define WRITEPRINTER  3 /* Write to Printer */
#define WRITETERMINAL 4 /* Write to Terminal */
#define READTERMINAL  5 /* Read from Terminal */
#define EXECUTE       6 /* Execute Program */

#endif
