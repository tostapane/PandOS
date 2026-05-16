#ifndef CALC_H
#define CALC_H

/**
 * Funzioni di supporto per l'applicazione calcolatrice.
 * Questo header definisce utilita' per la conversione e visualizzazione dei
 * risultati.
 */

/**
 * Converte un intero in una stringa alfanumerica.
 * Gestisce i segni e numeri a piu' cifre
 * @param n Il numero intero da convertire [-99, 99]
 * @param s Il buffer di destinazione (deve essere sufficientemente capiente,
 * almeno 5 Byte)
 */
void itoa(int n, char s[]);

#endif /* CALC_H */
