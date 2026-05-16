#include <uriscv/liburiscv.h>
#include "../headers/tconst.h"

/**
 * Implementazione della funzione print per l'output parametrizzato.
 * Automatizza l'invio di stringhe ai dispositivi terminali gestendo errori di scrittura.
 */

/**
 * Invia una stringa null-terminated a un dispositivo hardware specificato.
 * @param device Identificativo del dispositivo (es. WRITETERMINAL).
 * @param str Puntatore alla stringa da stampare.
 */
void print(int device, char *str) {
  char *errorMsg = "Bad device write status\n";
  int length, status;

  /* Calcolo manuale della lunghezza della stringa */
  for (length = 0; str[length] != '\0'; length++)
    ;

  /* Richiesta di I/O al sistema tramite syscall */
  status = SYSCALL(device, (int)str, length, 0);

  /* Gestione rudimentale degli errori di I/O */
  if (status < 0) {
    SYSCALL(device, (int)errorMsg, 26, 0);
    SYSCALL(TERMINATE, 0, 0, 0);
  }
}
