#include <uriscv/liburiscv.h>
#include "../headers/print.h"
#include "../headers/tconst.h"

/**
 * Applicazione 'uname' per PandOS.
 * Restituisce il nome del sistema operativo corrente.
 */
void main() {
  /* Visualizza il nome identificativo del kernel e della shell */
  print(WRITETERMINAL, "PandOSsh\n");
  
  /* Terminazione del processo e ritorno alla shell */
  SYSCALL(TERMINATE, 0, 0, 0);
}
