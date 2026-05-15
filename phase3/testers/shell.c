#include "../headers/shell.h"
#include "../headers/sysSupport.h"
#include "../headers/utils.h"

// array dei comandi/programmi disponibili
command programs[] = {{"calc", 2},      {"sl", 3},        {"date", 4},
                      {"echo", 5},      {"fibEight", 6},  {"fibEleven", 7},
                      {"uname", 8}};



int main() {
  char prompt[12] = "PandOSsh>> ";
  char inputBuf[128]; // buffer in cui verrà scritto l'input dell'utente
  while (1) {
    /* stampa il prompt
     * la SYSCALL prende: la funzione che deve eseguire (WRITETERMINAL),
     * l'indirizzo della stringa da stampare,
     * la dimensione della stringa da stamprare.
     * L'ultimo argomento è 0 perche non viene usato. */
    SYSCALL(WRITETERMINAL, (unsigned int)prompt, myStrlen(prompt), 0);

    /* legge l'input inserito dall'utente e lo mette in inputBuf
     * la SYSCALL blocca la shell finchè l'utente non preme invio. */
    int charsRead = SYSCALL(READTERMINAL, (unsigned int)inputBuf, 0, 0);

    /* sostituzione del newline (\n) con il nullterm (\0)
     * permette a myStrcmp di capire quando termina la stringa
     * (avremmo potuto usare il nullterm come terminatore della stringa in
     * myStrcmp, ma personalmente lo trovo meno elegante). */
    if (charsRead > 0)
      inputBuf[charsRead - 1] = '\0';

    if (myStrcmp(inputBuf, "exit") == 0) {
      SYSCALL(TERMINATE, 0, 0, 0);
    } else {
      int found = 0;

      for (int i = 0; i < NUM_PROGRAMS; i++) {
        if (myStrcmp(programs[i].name, inputBuf) == 0) {
          found = 1;
          SYSCALL(EXECUTE, programs[i].asid, 0, 0);
          break;
        }
      }
      if (!found && myStrlen(inputBuf) > 0) {
        char errMsg[] = "Command not found.\n";
        SYSCALL(WRITETERMINAL, (unsigned int)errMsg, myStrlen(errMsg), 0);
      }
    }
  }
}
