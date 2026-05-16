#include "../headers/print.h"
#include "../headers/tconst.h"
#include <uriscv/liburiscv.h>

/**
 * Applicazione 'echo' per PandOS.
 * Legge una stringa dall'input del terminale e la riproduce (echo) in output.
 */
void main() {
  int status;
  char buf[15];

  print(WRITETERMINAL, "Enter a string: ");

  /* Lettura bloccante dal terminale 0 */
  status = SYSCALL(READTERMINAL, (int)&buf[0], 0, 0);

  /* Inserimento del terminatore di stringa basato sui caratteri letti */
  if (status >= 0) {
    buf[status] = EOS;
  }

  print(WRITETERMINAL, "\n");
  print(WRITETERMINAL, &buf[0]);
  print(WRITETERMINAL, "\n");

  /* Ritorno al controllo della shell */
  SYSCALL(TERMINATE, 0, 0, 0);
}
