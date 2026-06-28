#ifndef CALC_H
#define CALC_H

/**
 * Converte un intero in una stringa alfanumerica.
 * Gestisce numeri da -99 a 99 (sufficiente per calc).
 * @param n Il numero da convertire.
 * @param s Il buffer di destinazione (deve essere almeno 5 byte).
 */
void itoa(int n, char s[]);

#endif /* CALC_H */
