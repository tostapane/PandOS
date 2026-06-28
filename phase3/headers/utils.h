#ifndef UTILS_H
#define UTILS_H

// restituisce la lunghezza di arg
int myStrlen(char *arg);

// ritorna 0 se s1 == s2
int myStrcmp(char *s1, char *s2);

// effettua una conversione da intero ad alfanumerico
void itoa(int n, char s[]);

void *memcpy(void *dest, const void *src, int n);
#endif
