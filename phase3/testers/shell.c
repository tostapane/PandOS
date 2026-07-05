#include "../headers/shell.h"
#include "../headers/sysSupport.h"
#include "../headers/utils.h"

/* Tabella dei comandi/programmi disponibili nella shell. Ogni entry
 * associa il nome digitabile dall'utente all'ASID dell'U-proc che
 * verrà lanciato tramite EXECUTE (SYS6). Gli ASID partono da 2 poiché
 * 0 è riservato al kernel e 1 alla shell stessa. */
command programs[] = {{"calc", 2}, {"sl", 3},       {"date", 4},
                      {"echo", 5}, {"fibEight", 6}, {"fibEleven", 7},
                      {"uname", 8}};

/**
 * @brief Shell interattiva di PandOS: legge comandi dal terminale e
 *        lancia il programma corrispondente.
 *
 * Esegue un ciclo infinito (REPL) che stampa un prompt, legge una riga
 * di input, e la interpreta: il comando 'exit' termina la shell,
 * altrimenti il nome viene cercato nella tabella `programs` e il
 * relativo U-proc viene avviato tramite EXECUTE. Un comando non
 * riconosciuto produce un messaggio d'errore.
 */
int main() {
  char prompt[12] = "PandOSsh>> ";
  char inputBuf[128]; // buffer in cui verrà scritto l'input dell'utente

  // Ciclo principale della shell: legge ed esegue comandi indefinitamente
  while (1) {
    /* stampa il prompt
     * la SYSCALL prende: la funzione che deve eseguire (WRITETERMINAL),
     * l'indirizzo della stringa da stampare,
     * la dimensione della stringa da stamprare.
     * L'ultimo argomento è 0 perche non viene usato. */
    SYSCALL(WRITETERMINAL, (unsigned int)prompt, myStrlen(prompt), 0);

    /* legge l'input inserito dall'utente e lo mette in inputBuf
     * la SYSCALL blocca la shell finchè l'utente non preme invio. */
    int charsRead =
        SYSCALL(READTERMINAL, (unsigned int)inputBuf, sizeof(inputBuf), 0);

    /* sostituzione del newline (\n) con il nullterm (\0)
     * permette a myStrcmp di capire quando termina la stringa
     * (avremmo potuto usare il nullterm come terminatore della stringa in
     * myStrcmp, ma personalmente lo trovo meno elegante). */
    if (charsRead > 0)
      inputBuf[charsRead - 1] = '\0';

    /* Il comando 'exit' termina la shell: l'auto-terminazione tramite
     * TERMINATE (SYS9) causa a sua volta la V sul masterSemaphore che
     * risveglia l'Instantiator Process per lo shutdown del sistema. */
    if (myStrcmp(inputBuf, "exit") == 0) {
      SYSCALL(TERMINATE, 0, 0, 0);
    } else {
      int found = 0; // flag: 1 se il comando digitato è stato riconosciuto

      // Ricerca lineare del comando nella tabella dei programmi
      for (int i = 0; i < NUM_PROGRAMS; i++) {
        if (myStrcmp(programs[i].name, inputBuf) == 0) {
          found = 1;
          /* Comando trovato: lancia l'U-proc associato tramite EXECUTE
           * (SYS6). La shell resta bloccata finché il figlio non termina. */
          SYSCALL(EXECUTE, programs[i].asid, 0, 0);
          break;
        }
      }
      /* Comando non trovato (ma non riga vuota): stampa il messaggio
       * d'errore. La riga vuota viene ignorata */
      if (!found && myStrlen(inputBuf) > 0) {
        char errMsg[] = "Command not found.";
        char newline[] = "\n";
        SYSCALL(WRITETERMINAL, (unsigned int)errMsg, myStrlen(errMsg), 0);
        SYSCALL(WRITETERMINAL, (unsigned int)newline, 1, 0);
      }
    }
  }
}
