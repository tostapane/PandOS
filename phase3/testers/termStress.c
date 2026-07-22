/* Stress test for terminal output and Support Level trap handling.
 *
 * Part 1 floods the terminal with SYS4 requests, checking that every
 * call returns the exact number of characters transmitted, including
 * the boundary cases of length 0 and the maximum legal length (128).
 * Part 2 deliberately causes a Program Trap (store below kuseg): the
 * Support Level must terminate this process and the shell must come
 * back with its prompt. Any output after the last message is an
 * error. */

#include <uriscv/liburiscv.h>

#include "../headers/print.h"
#include "../headers/tconst.h"
#include "../headers/utils.h"

#define LINES 40
#define MAXLEN 128

void main() {
  int i, len, ret, errors = 0;
  char numBuf[12];
  char maxBuf[MAXLEN];

  print(WRITETERMINAL, "termStress: terminal stress test starts\n");

  /* flood: three SYS4 per iteration, checking returned lengths */
  for (i = 1; i <= LINES; i++) {
    char *msg = "termStress line ";
    len = myStrlen(msg);
    ret = SYSCALL(WRITETERMINAL, (int)msg, len, 0);
    if (ret != len)
      errors++;

    itoa(i, numBuf);
    len = myStrlen(numBuf);
    ret = SYSCALL(WRITETERMINAL, (int)numBuf, len, 0);
    if (ret != len)
      errors++;

    ret = SYSCALL(WRITETERMINAL, (int)"\n", 1, 0);
    if (ret != 1)
      errors++;
  }

  /* boundary: length 0 is legal (only len < 0 or len > 128 are errors) */
  ret = SYSCALL(WRITETERMINAL, (int)numBuf, 0, 0);
  if (ret != 0)
    errors++;

  /* boundary: one line of the maximum legal length */
  for (i = 0; i < MAXLEN - 1; i++)
    maxBuf[i] = 'a' + (i % 26);
  maxBuf[MAXLEN - 1] = '\n';
  ret = SYSCALL(WRITETERMINAL, (int)maxBuf, MAXLEN, 0);
  if (ret != MAXLEN)
    errors++;

  if (errors == 0)
    print(WRITETERMINAL, "termStress: I/O part SUCCESS\n");
  else {
    print(WRITETERMINAL, "termStress: I/O part ERROR count: ");
    itoa(errors, numBuf);
    print(WRITETERMINAL, numBuf);
    print(WRITETERMINAL, "\n");
  }

  print(WRITETERMINAL, "termStress: now causing a Program Trap on purpose\n");
  print(WRITETERMINAL, "termStress: expect no further output, shell must return\n");

  /* store below kuseg from user-mode: Program Trap, then SYS2 */
  *((volatile int *)0x20000000) = 42;

  /* must not get here */
  print(WRITETERMINAL, "termStress: ERROR, survived the trap\n");
  SYSCALL(TERMINATE, 0, 0, 0);
}
