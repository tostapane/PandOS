/* Exercises every Support Level syscall a U-proc can issue.
 *
 * termStress only uses WRITETERMINAL (plus TERMINATE in its
 * unreachable tail); this test drives all four implemented services:
 *
 * 1. WRITETERMINAL (SYS4): return value must equal the length sent.
 * 2. READTERMINAL (SYS5): interactive echo; the returned count
 *    includes the closing '\n', so it must be myStrlen(line) + 1.
 * 3. EXECUTE (SYS6): reserved to the shell (ASID 1); issued from this
 *    process the Support Level must treat it as a Program Trap and
 *    terminate us, and the shell must come back with its prompt.
 * 4. TERMINATE (SYS2): only in the must-not-get-here tail, so any
 *    output after the last expected message is an error.
 *
 * GET_TOD (SYS1) and WRITEPRINTER (SYS3) are listed in tconst.h but
 * this Support Level does not implement them (an unknown syscall
 * number kills the caller), so they cannot be part of a test that
 * keeps running. */

#include <uriscv/liburiscv.h>

#include "../headers/print.h"
#include "../headers/tconst.h"
#include "../headers/utils.h"

#define BUFLEN 128

void main() {
  int ret, len, errors = 0;
  char numBuf[12];
  char inBuf[BUFLEN];

  print(WRITETERMINAL, "sysStress: syscall exercise starts\n");

  /* SYS4: check the returned count against the length requested
   * (myStrlen stops at '\n', so the newline is sent separately) */
  char *msg = "sysStress: checking WRITETERMINAL return value";
  len = myStrlen(msg);
  ret = SYSCALL(WRITETERMINAL, (int)msg, len, 0);
  if (ret != len)
    errors++;
  ret = SYSCALL(WRITETERMINAL, (int)"\n", 1, 0);
  if (ret != 1)
    errors++;

  /* SYS5: read a line and cross-check the returned count */
  print(WRITETERMINAL, "sysStress: type a line and press enter> ");
  ret = SYSCALL(READTERMINAL, (int)inBuf, BUFLEN, 0);
  if (ret < 1)
    errors++;
  else {
    /* the count includes the '\n': replace it with the terminator */
    inBuf[ret - 1] = '\0';
    if (myStrlen(inBuf) != ret - 1)
      errors++;

    /* echo the line back through an explicit SYS4, checking again */
    print(WRITETERMINAL, "sysStress: you typed: ");
    len = myStrlen(inBuf);
    ret = SYSCALL(WRITETERMINAL, (int)inBuf, len, 0);
    if (ret != len)
      errors++;
    print(WRITETERMINAL, "\n");
  }

  if (errors == 0)
    print(WRITETERMINAL, "sysStress: I/O part SUCCESS\n");
  else {
    print(WRITETERMINAL, "sysStress: I/O part ERROR count: ");
    itoa(errors, numBuf);
    print(WRITETERMINAL, numBuf);
    print(WRITETERMINAL, "\n");
  }

  /* SYS6 from a non-shell process: the Support Level must kill us */
  print(WRITETERMINAL, "sysStress: calling EXECUTE without being the shell\n");
  print(WRITETERMINAL, "sysStress: expect no further output, shell must return\n");
  SYSCALL(EXECUTE, 2, 0, 0);

  /* must not get here */
  print(WRITETERMINAL, "sysStress: ERROR, survived the EXECUTE call\n");
  SYSCALL(TERMINATE, 0, 0, 0);
}
