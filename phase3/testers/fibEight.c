#include <uriscv/liburiscv.h>
#include "../headers/print.h"
#include "../headers/tconst.h"

/**
 * Test di calcolo intensivo: Successione di Fibonacci (n=8).
 * Verifica la capacita' del sistema di gestire ricorsione profonda e utilizzo della CPU.
 */

/**
 * Calcola l'n-esimo numero di Fibonacci ricorsivamente.
 */
int fib(int i) {
  if ((i == 1) || (i == 2))
    return (1);

  return (fib(i - 1) + fib(i - 2));
}

void main() {
  int i;

  print(WRITETERMINAL, "Recursive Fibonacci (8) Test starts\n");

  /* Esecuzione del calcolo ricorsivo */
  i = fib(8);

  print(WRITETERMINAL, "Recursion Concluded\n");

  /* Verifica dell'accuratezza del risultato (Fib(8) = 21) */
  if (i == 21) {
    print(WRITETERMINAL, "Recursion Concluded Successfully\n");
  } else {
    print(WRITETERMINAL, "ERROR: Recursion problems\n");
  }

  /* Terminazione normale */
  SYSCALL(TERMINATE, 0, 0, 0);
}
