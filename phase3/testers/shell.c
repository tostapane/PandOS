#include "../headers/shell.h"
#include "../headers/sysSupport.h"
#include "../headers/utils.h"

/**
 * Interprete dei comandi (shell) per PandOS.
 * Fornisce un'interfaccia utente interattiva per lanciare programmi
 * caricati sui dispositivi Flash del sistema.
 */

/* Array dei programmi disponibili con i relativi ASID */
command programs[] = {{"calc", 2}, {"sl", 3},       {"date", 4},
                      {"echo", 5}, {"fibEight", 6}, {"fibEleven", 7},
                      {"uname", 8}};

/**
 * Punto di ingresso della Shell.
 * Gestisce il ciclo infinito di lettura input, parsing ed esecuzione.
 */
int main() {
  char prompt[12] = "PandOSsh>> ";
  char inputBuf[128]; /* Buffer per ospitare l'input digitato dall'utente */

  while (1) {
    /* Visualizzazione del prompt interattivo sul terminale */
    SYSCALL(WRITETERMINAL, (unsigned int)prompt, myStrlen(prompt), 0);

    /* Acquisizione della stringa inserita dall'utente.
     * La syscall blocca l'esecuzione finche' non viene ricevuto il carattere
     * newline. */
    int charsRead = SYSCALL(READTERMINAL, (unsigned int)inputBuf, 0, 0);

    /* Pulizia dell'input: sostituzione del carattere newline con il
     * null-terminator per permettere il corretto confronto tra stringhe. */
    if (charsRead > 0)
      inputBuf[charsRead - 1] = '\0';

    /* Comando speciale per la chiusura della shell e lo shutdown del sistema */
    if (myStrcmp(inputBuf, "exit") == 0) {
      SYSCALL(TERMINATE, 0, 0, 0);
    } else {
      int found = 0;

      /* Ricerca del comando inserito nell'elenco dei programmi registrati */
      for (int i = 0; i < NUM_PROGRAMS; i++) {
        if (myStrcmp(programs[i].name, inputBuf) == 0) {
          found = 1;
          /* Caricamento ed esecuzione del processo figlio tramite syscall
           * EXECUTE. La shell rimarra' sospesa fino alla terminazione del
           * figlio. */
          SYSCALL(EXECUTE, programs[i].asid, 0, 0);
          break;
        }
      }

      /* Messaggio di errore nel caso il comando non sia presente nell'elenco */
      if (!found && myStrlen(inputBuf) > 0) {
        char errMsg[] = "Command not found.\n";
        SYSCALL(WRITETERMINAL, (unsigned int)errMsg, myStrlen(errMsg), 0);
      }
    }
  }
}
