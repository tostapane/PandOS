#include "../headers/utils.h"

/**
 * Implementazione delle funzioni di utilita' per i processi utente.
 * Queste funzioni forniscono supporto di base per stringhe e memoria
 * senza dipendere dalla libreria standard C.
 */

/**
 * Calcola la lunghezza di una stringa fino al null-terminator o al newline.
 */
int myStrlen(char *arg) {
  int len = 0;
  while (arg[len] != '\0' && arg[len] != '\n') {
    len++;
  }
  return len;
}

/**
 * Confronta due stringhe e ne determina l'uguaglianza.
 */
int myStrcmp(char *s1, char *s2) {
  while (*s1 && (*s1 == *s2)) {
    s1++;
    s2++;
  }
  return *(unsigned char *)s1 - *(unsigned char *)s2;
}

/**
 * Algoritmo di conversione da intero a stringa (base 10).
 * Supporta numeri negativi e valori fino a due cifre per semplicita'.
 */
void itoa(int n, char s[]) {
  int i = 0;
  /* Gestione del segno per numeri negativi */
  if (n < 0) {
    s[i++] = '-';
    n = -n;
  }
  /* Estrazione della cifra delle decine se presente */
  if (n >= 10) {
    s[i++] = (n / 10) + '0';
  }
  /* Cifra delle unita' */
  s[i++] = (n % 10) + '0';
  s[i++] = '\0';
}

/**
 * Copia un blocco di dati tra locazioni di memoria.
 */
void *memcpy(void *dest, const void *src, int n) {
  char *d = (char *)dest;
  const char *s = (const char *)src;
  for (int i = 0; i < n; i++) {
    d[i] = s[i];
  }
  return dest;
}
