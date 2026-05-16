#ifndef PRINT_H
#define PRINT_H

/**
 * Funzioni di stampa per i processi utente.
 * Facilita l'invio di stringhe ai dispositivi di output tramite syscall.
 */

/**
 * Invia una stringa a un dispositivo specifico (es. terminale o stampante).
 * Gestisce internamente il calcolo della lunghezza e la chiamata di sistema.
 * @param device ID del dispositivo di output.
 * @param str La stringa da stampare.
 */
extern void print(int device, char *str);

#endif
