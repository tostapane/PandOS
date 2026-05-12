#include "../headers/utils.h"

int myStrlen(char *arg) {
  int len = 0;
  while (arg[len] != '\0' && arg[len] != '\n') {
    len++;
  }
  return len;
}

int myStrcmp(char *s1, char *s2) {
  while (*s1 && (*s1 == *s2)) {
    s1++;
    s2++;
  }
  return *(unsigned char *)s1 - *(unsigned char *)s2;
}

void itoa(int n, char s[]) {
  int i = 0;
  // gestione del segno '-'
  if (n < 0) {
    s[i++] = '-';
    // rendiamo il numero positivo per estrarre le cifre
    n = -n;
  }
  // gestione prima cifra
  if (n >= 10) {
    s[i++] = (n / 10) + '0';
  }
  // gestione seconda cifra
  s[i++] = (n % 10) + '0';
  // nullterm
  s[i++] = '\0';
}

void *memcpy(void *dest, const void *src, int n) {
  char *d = (char *)dest;
  const char *s = (const char *)src;
  for (int i = 0; i < n; i++) {
    d[i] = s[i];
  }
  return dest;
}
