#include "../headers/calc.h"
#include "../headers/sysSupport.h"
#include "../headers/utils.h"
#include <uriscv/liburiscv.h>

/* legge dal terminale un singolo carattere, accettando solo quelli
 * presenti nella stringa `allowed`. L'input e' valido solo se l'utente
 * inserisce esattamente un carattere seguito da invio; in ogni altro
 * caso (riga vuota, piu' caratteri, carattere non ammesso) stampa
 * l'errore e ripropone il prompt.
 *
 * la lettura passa sempre sizeof(buf) come limite alla READTERMINAL:
 * qualunque cosa digiti l'utente, la syscall non scrivera' mai oltre
 * il buffer, quindi il buffer overflow e' impossibile per costruzione.
 * Un input piu' lungo del buffer viene troncato dalla syscall (i char
 * in eccesso sono scartati fino all'invio) e charsRead != 2 lo fa
 * rifiutare come ogni altro input multi-carattere. */
static char readOneChar(char *prompt, char *allowed, char *errMsg) {
  char buf[8];

  while (1) {
    SYSCALL(WRITETERMINAL, (unsigned int)prompt, myStrlen(prompt), 0);
    int charsRead = SYSCALL(READTERMINAL, (unsigned int)buf, sizeof(buf), 0);

    /* errore del device: inutile riprovare, terminiamo */
    if (charsRead < 0)
      SYSCALL(TERMINATE, 0, 0, 0);

    /* esattamente un carattere + '\n' */
    if (charsRead == 2 && buf[1] == '\n') {
      for (int i = 0; allowed[i] != '\0'; i++) {
        if (buf[0] == allowed[i])
          return buf[0];
      }
    }

    SYSCALL(WRITETERMINAL, (unsigned int)errMsg, myStrlen(errMsg), 0);
  }
}

int main() {
  char resChar[5];

  char init1[128] =
      "The calc program is a basic calculator program that do basic arithmetic "
      "operations between two single-digit numbers.\n";
  char init2[128] = "It takes the first single-digit number, an operator (+, "
                    "-, *, /) and the second "
                    "single-digit number, and prints the result. \n";

  // stampa istruzioni
  SYSCALL(WRITETERMINAL, (unsigned int)init1, myStrlen(init1), 0);
  SYSCALL(WRITETERMINAL, (unsigned int)init2, myStrlen(init2), 0);

  char digitErr[] = "invalid input: insert ONE digit (0-9)! \n";
  char opErr[] = "invalid input: insert ONE operator among +, -, /, *! \n";

  int n1 =
      readOneChar("insert the first number: \n", "0123456789", digitErr) - '0';

  char op = readOneChar("insert one of the available operators: +, -, /, *: \n",
                        "+-/*", opErr);

  int n2 =
      readOneChar("insert the second number: \n", "0123456789", digitErr) - '0';

  int res = 0;
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
      char errMsg[] = "second number can't be 0 \n";
      SYSCALL(WRITETERMINAL, (unsigned int)errMsg, myStrlen(errMsg), 0);
      SYSCALL(TERMINATE, 0, 0, 0);
    }
    break;
  case '*':
    res = n1 * n2;
    break;
  }

  /* res e' sempre in [-9, 81]: al piu' 2 cifre piu' segno e '\0',
   * resChar[5] e' sufficiente per qualunque risultato */
  itoa(res, resChar);

  SYSCALL(WRITETERMINAL, (unsigned int)resChar, myStrlen(resChar), 0);
  char newline[] = "\n";
  SYSCALL(WRITETERMINAL, (unsigned int)newline, 1, 0);
  SYSCALL(TERMINATE, 0, 0, 0);
  return 0;
}
