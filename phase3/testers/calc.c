#include "../headers/calc.h"
#include "../headers/sysSupport.h"
#include "../headers/utils.h"
#include <uriscv/liburiscv.h>

int main() {
  int n1, n2;
  char op = '0';
  char resChar[5];

  /* stampiamo un messaggio che fornisce istruzioni all'utente
   * facciamo inserire il primo numero nel buf,
   * poi l'operatore
   * poi il secondo numero.
   *
   * Estrazione dell'operazione completa dal buf (i primi 3 char)
   * calcolo del risultato
   * scrittura del risultato nel buf
   * stampa nel terminale
   * pulizia del buffer
   * ritorno.*/

  char init1[128] =
      "The calc program is a basic calculator program that do basic arithmetic "
      "operations between two single-digit numbers.\n";
  char init2[128] = "It takes the first single-digit number, an operator (+, "
                    "-, *, /) and the second "
                    "single-digit number, and prints the result. \n";

  // stampa istruzioni
  SYSCALL(WRITETERMINAL, (unsigned int)init1, myStrlen(init1), 0);
  SYSCALL(WRITETERMINAL, (unsigned int)init2, myStrlen(init2), 0);

  int res = 0;
  int charsRead = 0;

  char first[] = "insert the first number: \n";
  SYSCALL(WRITETERMINAL, (unsigned int)first, myStrlen(first), 0);
  charsRead = SYSCALL(READTERMINAL, (unsigned int)resChar, 0, 0);
  // char + \n (enter)
  if (charsRead != 2) {
    char errMsg[] = "you must insert ONE char per time!!! \n";
    SYSCALL(WRITETERMINAL, (unsigned int)errMsg, myStrlen(errMsg), 0);
    char nl[] = "\n";
    SYSCALL(WRITETERMINAL, (unsigned int)nl, 1, 0);

    SYSCALL(TERMINATE, 0, 0, 0);
  } else {
    n1 = resChar[0] - '0';
  }

  char operator[] = "insert one of the available operators: +, -, /, *: \n";
  SYSCALL(WRITETERMINAL, (unsigned int)operator, myStrlen(operator), 0);
  charsRead = SYSCALL(READTERMINAL, (unsigned int)resChar, 0, 0);
  // char + \n (enter)
  if (charsRead != 2) {
    char errMsg[] = "Available operators: +, -, /, *.!! \n";
    SYSCALL(WRITETERMINAL, (unsigned int)errMsg, myStrlen(errMsg), 0);
    SYSCALL(TERMINATE, 0, 0, 0);
  } else {
    op = resChar[0];
  }

  char second[] = "insert the second number: \n";
  SYSCALL(WRITETERMINAL, (unsigned int)second, myStrlen(second), 0);
  charsRead = SYSCALL(READTERMINAL, (unsigned int)resChar, 0, 0);
  // char + \n (enter)
  if (charsRead != 2) {
    char errMsg[] = "you must insert ONE char per time!! \n";
    SYSCALL(WRITETERMINAL, (unsigned int)errMsg, myStrlen(errMsg), 0);
    SYSCALL(TERMINATE, 0, 0, 0);
  } else {
    n2 = resChar[0] - '0';
  }

  switch (op) {
  case '+':
    res = n1 + n2;
    break;
  case '-':
    res = n1 - n2;
    break;
  case '/':
    if (n2 != 0)
      res = n1 / n2;
    else {
      char errMsg[] = "second number can't be 0";
      SYSCALL(WRITETERMINAL, (unsigned int)errMsg, myStrlen(errMsg), 0);
      SYSCALL(TERMINATE, 0, 0, 0);
    }
    break;
  case '*':
    res = n1 * n2;
    break;
  }

  itoa(res, resChar);

  SYSCALL(WRITETERMINAL, (unsigned int)resChar, myStrlen(resChar), 0);
  char newline[] = "\n";
  SYSCALL(WRITETERMINAL, (unsigned int)newline, 1, 0);
  SYSCALL(TERMINATE, 0, 0, 0);
  return 0;
}
