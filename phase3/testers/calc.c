#include "../headers/calc.h"
#include "../headers/sysSupport.h"
#include "../headers/utils.h"
#include <uriscv/liburiscv.h>

/**
 * @brief Legge dal terminale un singolo carattere appartenente a un
 *        insieme di caratteri ammessi, riproponendo il prompt finché
 *        l'input non è valido.
 *
 * L'input è considerato valido solo se l'utente inserisce esattamente
 * un carattere seguito da invio e tale carattere è presente nella
 * stringa `allowed`. In ogni altro caso (riga vuota, più caratteri,
 * carattere non ammesso) viene stampato `errMsg` e il prompt viene
 * ristampato per una nuova lettura.
 *
 * La lettura passa sempre sizeof(buf) come limite a READTERMINAL:
 * qualunque cosa digiti l'utente, la syscall non scriverà mai oltre
 * il buffer, quindi il buffer overflow è impossibile per costruzione.
 * Un input più lungo del buffer viene troncato dalla syscall (i char
 * in eccesso sono scartati fino all'invio) e charsRead != 2 lo fa
 * rifiutare come ogni altro input multi-carattere.
 *
 * @param prompt  Stringa da stampare per richiedere l'input all'utente.
 * @param allowed Stringa (null-terminata) con i caratteri accettati.
 * @param errMsg  Messaggio d'errore stampato in caso di input non valido.
 * @return Il carattere valido letto (appartenente a `allowed`).
 */
static char readOneChar(char *prompt, char *allowed, char *errMsg) {
  char buf[8];

  // Ciclo finché l'utente non fornisce un input valido
  while (1) {
    // Stampa il prompt sul terminale tramite WRITETERMINAL
    SYSCALL(WRITETERMINAL, (unsigned int)prompt, myStrlen(prompt), 0);
    // Legge la riga digitata dall'utente; charsRead è il numero di caratteri
    // effettivamente letti (compreso il '\n' finale)
    int charsRead = SYSCALL(READTERMINAL, (unsigned int)buf, sizeof(buf), 0);

    /* Valore negativo: errore del device. Riprovare è inutile, terminiamo */
    if (charsRead < 0)
      SYSCALL(TERMINATE, 0, 0, 0);

    /* Input valido solo se composto da esattamente un carattere + '\n' */
    if (charsRead == 2 && buf[1] == '\n') {
      // Verifica che il carattere digitato sia tra quelli ammessi
      for (int i = 0; allowed[i] != '\0'; i++) {
        if (buf[0] == allowed[i])
          return buf[0];
      }
    }

    // Input non valido: segnala l'errore e ripropone il prompt
    SYSCALL(WRITETERMINAL, (unsigned int)errMsg, myStrlen(errMsg), 0);
  }
}

/**
 * @brief Programma calcolatrice: esegue un'operazione aritmetica di base
 *        tra due numeri di una singola cifra.
 *
 * Stampa le istruzioni d'uso, poi legge (validando l'input tramite
 * readOneChar) il primo operando, l'operatore e il secondo operando,
 * calcola il risultato e lo stampa sul terminale. Al termine l'U-proc
 * si auto-termina tramite TERMINATE (SYS9 del Livello di Supporto).
 *
 * @return 0 (mai raggiunto: il programma termina con TERMINATE).
 */
int main() {
  // Buffer per la conversione del risultato numerico in stringa (itoa)
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

  // Messaggi d'errore riutilizzati ad ogni lettura non valida
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
    // La divisione per zero non è definita: segnala l'errore e termina
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

  /* Converte il risultato in stringa per poterlo stampare.
   * res è sempre in [-9, 81]: al più 2 cifre più segno e '\0',
   * quindi resChar[5] è sufficiente per qualunque risultato */
  itoa(res, resChar);

  SYSCALL(WRITETERMINAL, (unsigned int)resChar, myStrlen(resChar), 0);
  char newline[] = "\n";
  SYSCALL(WRITETERMINAL, (unsigned int)newline, 1, 0);

  // Terminazione dell'U-proc (SYS9 del Livello di Supporto)
  SYSCALL(TERMINATE, 0, 0, 0);
  return 0;
}
