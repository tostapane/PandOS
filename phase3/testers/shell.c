#include "../headers/sysSupport.h"
#include <string.h>
#include <uriscv/liburiscv.h>

#define NUM_PROGRAMS 3

// struttura per associare il nome del comando al suo ASID
typedef struct command
{
  char *name;
  int asid;
} command;

// array dei comandi/programmi disponibili
command programs[] = {{"calc", 2}, {"test2", 3}, {"test3", 4}};

// ritorna la lunghezza di arg
int myStrlen(char *arg)
{
  int len = 0;
  while (arg[len] != '\0' && arg[len] != '\n')
  {
    len++;
  }
  return len;
}

// ritorna 0 se arg1 == arg2
int myStrcmp(char *s1, char *s2)
{
  while (*s1 && (*s1 == *s2))
  {
    s1++;
    s2++;
  }
  return *(unsigned char *)s1 - *(unsigned char *)s2;
}

int main()
{
  char prompt[12] = "PandOSsh>> ";
  char inputBuf[128]; // buffer in cui verrà scritto l'input dell'utente
  while (1)
  {
    // stampa il prompt
    SYSCALL(WRITETERMINAL, *prompt, strlen(prompt), 0);

    // legge l'input inserito dall'utente
    // la SYSCALL blocca la shell finchè l'utente non preme invio
    int charsRead = SYSCALL(READTERMINAL, (unsigned int)inputBuf, 0, 0);

    if (charsRead > 0)
    {
      inputBuf[charsRead - 1] = '\0';
    }

    if (myStrcmp(inputBuf, "exit") == 0)
    {
      SYSCALL(TERMINATE, 0, 0, 0);
    }
    else
    {
      int found = 0;

      for (int i = 0; i < NUM_PROGRAMS; i++)
      {
        if (myStrcmp(programs[i].name, inputBuf) == 0)
        {
          found = 1;
          SYSCALL(EXECUTE, programs[i].asid, 0, 0);
          break;
        }
      }
      if (!found && myStrlen(inputBuf) > 0)
      {
        char errMsg[] = "Comando non trovato.\n";
        SYSCALL(WRITETERMINAL, (unsigned int)errMsg, myStrlen(errMsg), 0);
      }
    }
  }
}
