#include "../headers/print.h"
#include "../headers/tconst.h"
#include <uriscv/liburiscv.h>

/**
 * Applicazione 'date' per PandOS.
 * Visualizza una data fissa nel passato come test di visualizzazione.
 */
void main() {
  print(WRITETERMINAL, "Sat  5 Nov 06:15:00 PST 1955\n");

  /* Terminazione normale del processo */
  SYSCALL(TERMINATE, 0, 0, 0);
}
