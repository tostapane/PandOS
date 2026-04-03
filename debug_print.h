
#ifndef DEBUG_PRINT_H
#define DEBUG_PRINT_H

static inline void debug_print(char *str) {
  volatile unsigned int *term0_base = (volatile unsigned int *)0x10000254;
  while (*str) {
    term0_base[3] = 2 | (*str << 8); // PRINTCHR
    while ((term0_base[2] & 0xFF) != 5) {
    }                  // Wait for RECVD
    term0_base[3] = 1; // ACK
    str++;
  }
}

static inline void debug_print_hex(unsigned int num) {
  const char digits[] = "0123456789ABCDEF";
  char buf[9];
  buf[8] = '\0';
  for (int i = 7; i >= 0; i--) {
    buf[i] = digits[num % 16];
    num /= 16;
  }
  debug_print(buf);
}

#endif
