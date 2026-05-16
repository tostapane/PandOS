#ifndef UTILS_H
#define UTILS_H

/**
 * Funzioni di utilita' per la manipolazione di stringhe e memoria.
 * Implementazioni personalizzate per evitare dipendenze esterne non necessarie.
 */

/**
 * Calcola la lunghezza di una stringa escludendo il null-terminator.
 * @param arg La stringa da misurare.
 * @return Numero di caratteri.
 */
int myStrlen(char *arg);

/**
 * Confronta due stringhe carattere per carattere.
 * @param s1 Prima stringa.
 * @param s2 Seconda stringa.
 * @return 0 se le stringhe sono identiche, valore diverso altrimenti.
 */
int myStrcmp(char *s1, char *s2);

/**
 * Converte un valore intero in una stringa di caratteri (alfanumerico).
 * @param n Il numero intero da convertire.
 * @param s Buffer di destinazione della stringa.
 */
void itoa(int n, char s[]);

/**
 * Copia un blocco di memoria da una sorgente a una destinazione.
 * @param dest Puntatore alla destinazione.
 * @param src Puntatore alla sorgente.
 * @param n Numero di byte da copiare.
 * @return Puntatore alla destinazione.
 */
void *memcpy(void *dest, const void *src, int n);

#endif
