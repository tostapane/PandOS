#include "./headers/scheduler.h"
#include "../headers/const.h"
#include "headers/initial.h"
#include <uriscv/liburiscv.h>

void scheduler(void) {
  if (!emptyProcQ(&readyQueue)) {

    // 3.1
    // Estrae il primo processo dalla coda dei processi pronti all'esecuzione
    currProc = removeProcQ(&readyQueue);

    // 3.2 Imposta il Process Local Timer (PLT) a 5 millisecondi [sezione 7.2].
    // Questo definisce il quanto di tempo concesso al processo.
    setTIMER(TIMESLICE);

    // Salva il tempo di inizio per calcolare in seguito quanto tempo di CPU
    // viene utilizzato dal processo
    STCK(processTimer);

    // 3.3
    // Carica lo stato del processo per avviarne (o riprenderne) l'esecuzione
    LDST(&currProc->p_s);

  } else {
    // Se non ci sono processi pronti, gestiamo i casi limite
    if (processCount == 0) {
      // Nessun processo nel sistema: fine dell'esecuzione
      HALT();
    }
    if (processCount > 0 && softBlockCount > 0) {
      // Ci sono processi, ma sono tutti bloccati in attesa di un evento (I/O,
      // timer, ecc.) Abilitiamo gli interrupt, ma escludiamo il process local
      // timer (PLT).
      setMIE(MIE_ALL & ~MIE_MTIE_MASK);
      unsigned int status = getSTATUS();
      status |= MSTATUS_MIE_MASK;
      setSTATUS(status);

      // Sospende l'esecuzione in attesa di un interrupt esterno
      WAIT();
    }
    if (processCount > 0 && softBlockCount == 0) {
      // I processi esistono, non c'è nulla in readyQueue, e nessuno sta
      // aspettando nulla È uno stato irreversibile di deadlock
      PANIC();
    }
  }
}
