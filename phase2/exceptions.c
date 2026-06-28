#include "headers/exceptions.h"
#include "headers/initial.h"
#include "headers/interrupts.h"
#include <uriscv/liburiscv.h>

/**
 * @brief Copia un blocco di memoria da una sorgente a una destinazione.
 *
 * @param dest Puntatore alla destinazione della copia.
 * @param src Puntatore alla sorgente della copia.
 * @param n Numero di byte da copiare.
 * @return Puntatore alla destinazione.
 */
void *memcpy(void *dest, const void *src, unsigned int n) {
  char *d = (char *)dest;
  const char *s = (const char *)src;
  for (unsigned int i = 0; i < n; i++) {
    d[i] = s[i];
  }
  return dest;
}

/**
 * @brief Funzione ricorsiva per cercare un PCB dato un PID, partendo da una
 * radice.
 * @param root Il PCB da cui iniziare la ricerca (tipicamente la radice
 * dell'albero).
 * @param pid Il Process ID da cercare.
 * @return Puntatore al PCB se trovato, altrimenti NULL.
 */
static pcb_t *find_pcb_recursive(pcb_t *root, int pid) {
  // Caso base 1: la sub-radice passata non esiste (fine del ramo)
  if (root == NULL)
    return NULL;

  // Caso base 2: abbiamo scovato esattamente il processo cercato
  if (root->p_pid == pid)
    return root;

  pcb_t *found = NULL;
  struct list_head *pos;

  // Applica una ricerca in profondità (DFS):
  // Itera lungo tutti i figli diretti legati in &root->p_child iterando p_sib.
  list_for_each(pos, &root->p_child) {
    // Ricava l'effettivo PCB decodificando il container tramite la macro kernel
    pcb_t *child = container_of(pos, pcb_t, p_sib);

    // Auto-richiamo per scandagliare l'eventuale discendenza autonoma di questo
    // "figlio"
    found = find_pcb_recursive(child, pid);

    // Se in fondo a questo tracciato è stato trovato il PCB, arresta il ciclo e
    // propaga la risposta
    if (found)
      return found;
  }

  // Se tutti i figli e relative discendenze non ritornano nulla, il processo
  // non è in quest'albero
  return NULL;
}

/**
 * @brief Trova un PCB nella pcbFree list dato un PID, partendo da currProc.
 * @param pid Il Process ID da cercare.
 * @return Puntatore al PCB se trovato, altrimenti NULL.
 */
static pcb_t *find_pcb(int pid) {
  // 1. Dal momento che il `currProc` è il focus attuale della CPU, esso farà
  // parte inevitabilmente di una qualche ramificazione legittima.
  pcb_t *root = currProc;

  // Risale la catena dei padri ritroso finché non scova il progenitore assoluto
  // (il processo fondatore inizializzato in initial.c con p_parent a NULL)
  while (root->p_parent != NULL)
    root = root->p_parent;

  // 2. Trovata la vetta della piramide, esegue il metodo ricorsivo
  // che attraverserà in discesa ogni singolo nodo generato nel tempo dal
  // sistema
  return find_pcb_recursive(root, pid);
}

/**
 * @brief Termina ricorsivamente un processo e i suoi figli (Sezione 10).
 *
 * @param proc Il PCB del processo da terminare.
 */
static void recursive_terminate(pcb_t *proc) {
  if (proc == NULL)
    return;

  // 1. Termina ricorsivamente tutti i figli
  while (!emptyChild(proc)) {
    recursive_terminate(removeChild(proc));
  }

  // 2. Scollega sempre dal genitore (safe anche se già rimosso da removeChild)
  outChild(proc);

  // 3. Rimuovi dallo stato di esecuzione
  if (proc == currProc) {
    // currProc non è in nessuna lista, basta non ricaricarlo (lo farà lo
    // scheduler)
    currProc = NULL;
  } else {
    // Prova a rimuoverlo dalla Ready Queue
    if (outProcQ(&readyQueue, proc) == NULL) {
      // Se non era in Ready, potrebbe essere bloccato
      int *sem_addr = proc->p_semAdd; // Salva l'indirizzo PRIMA di outBlocked
      if (outBlocked(proc) != NULL) {
        // Se era un semaforo di device o pseudo-clock, aggiorna softBlockCount
        if ((sem_addr >= (int *)&subDevice[0] &&
             sem_addr <= (int *)&subDevice[NRSEMAPHORES - 1])) {
          softBlockCount--;
        }
      }
    }
  }

  // 4. Restituisci il PCB e aggiorna il conteggio totale
  freePcb(proc);
  processCount--;
}

/**
 * SEZIONE 5: Exception Handling
 *
 * Punto di ingresso principale per la gestione di tutte le eccezioni.
 *
 * Questa funzione viene chiamata dal BIOS ogni volta che si verifica
 * un'eccezione (esclusi i TLB-Refill). Il suo compito è determinare la causa
 * dell'eccezione e delegare il lavoro al gestore appropriato.
 */
void exceptionHandler(void) {

  // id del processore che ha causato l'eccezione
  unsigned int procsrID = getPRID();

  // Stato del processore al momento dell'eccezione
  // Otteniamo un puntatore allo stato di quel processore
  state_t *exceptionState = GET_EXCEPTION_STATE_PTR(procsrID);

  // causa dello stato salvato
  unsigned int cause = exceptionState->cause;

  if (CAUSE_IS_INT(cause)) {
    // gestore interrupt
    interruptHandler();
  } else {
    // estrazione excode dallo stato salvato
    unsigned int exCode = (cause & CAUSE_EXCCODE_MASK); // a CAUSESHIFT was here

    // Indirizzamento al gestore dell'eccezione corretto
    if (exCode == 8 || exCode == 11) {
      syscallHandler();
    } else if (exCode >= 24 && exCode <= 28) {
      tlbHandler();
    } else {
      programTrapHandler();
    }
  }
}

/**
 * SEZIONE 6: SYSCALL Exception Handling
 * Le SYSCALL che devi implementare qui dentro sono:
 * - 6.1: CreateProcess (NSYS1)
 * - 6.2: TerminateProcess (NSYS2)
 * - 6.8: GetSupportData (NSYS8)
 * - 6.9: GetProcessID (NSYS9)
 * - 6.10: Yield (NSYS10)
 * - 6.11: Controllo per SYSCALL in User-Mode.
 * - 6.12: Gestione del PC per il ritorno corretto dalla SYSCALL.
 *
 * Gestore per le eccezioni di tipo SYSCALL.
 *
 * Questa funzione gestisce le richieste di servizi del Nucleo fatte dai
 * processi.
 */
void syscallHandler(void) {
  state_t *exception_state = GET_EXCEPTION_STATE_PTR(getPRID());

  // Estrae il numero della SYSCALL dal registro a0
  int syscall_number = exception_state->reg_a0;

  // Flag per gestire il ritorno dalle SYSCALL
  unsigned int is_blocking = 0;

  // 6.11: Controllo per SYSCALL in User-Mode
  if (syscall_number < 0 &&
      (exception_state->status & MSTATUS_MPP_MASK) == MSTATUS_MPP_U) {
    exception_state->cause =
        (exception_state->cause & ~GETEXECCODE) | (PRIVINSTR << CAUSESHIFT);
    programTrapHandler();
    return;
  }

  // 6.12: Incremento del PC per evitare loop.
  // Per le chiamate bloccanti, questo stato aggiornato verrà salvato nel PCB.
  // if (syscall_number <= -1 && syscall_number >= -10)
  exception_state->pc_epc += WORDLEN;

  switch (syscall_number) {

  // 6.1: CreateProcess (NSYS1)
  // Crea un nuovo processo come figlio del processo chiamante (currProc).
  case CREATEPROCESS: {

    // Tenta di allocare un nuovo Process Control Block (PCB) dalla lista
    // pcbFree
    pcb_t *new_proc = allocPcb();
    if (new_proc == NULL) {
      // Se non ci sono più PCB liberi (out of resources), ritorna il codice di
      // errore -1
      exception_state->reg_a0 = -1;
    } else {
      // In base alle specifiche, a1 contiene un puntatore allo stato processore
      // iniziale
      state_t *new_state = (state_t *)exception_state->reg_a1;
      new_proc->p_s = *new_state; // Copia per valore dello stato

      // a2 contiene la priorità schedulata, a3 il puntatore optionale al
      // livello di supporto
      new_proc->p_prio = exception_state->reg_a2;
      new_proc->p_supportStruct = (support_t *)exception_state->reg_a3;

      // Inquadra il nuovo PCB nell'albero parentale sotto l'attuale processo
      // esecutivo
      insertChild(currProc, new_proc);
      // Inserisce il figlio nell'elenco dei processi pronti (Ready Queue)
      insertProcQ(&readyQueue, new_proc);
      processCount++; // Registra nel totale globale la presenza di un nuovo
                      // processo

      // Ritorna il Process ID (PID) generato automaticamente durante allocPcb
      exception_state->reg_a0 = new_proc->p_pid;
    }
    break;
  }

  // 6.2: TerminateProcess (Terminazione processo - NSYS2)
  // Distrugge se stesso (se a1 = 0) o un processo specfico determinato dal PID,
  // assieme alla relativa progenie.
  case TERMPROCESS: {

    // Se reg_a1 == 0, l'operazione è un "suicidio" (viene terminato currProc).
    // Altrimenti, cerca ricorsivamente nell'albero il processo bersaglio.
    pcb_t *target = (exception_state->reg_a1 == 0)
                        ? currProc
                        : find_pcb(exception_state->reg_a1);
    // Se trova il processo (non era già deallocato), innesca l'eradicazione
    // dall'architettura
    if (target) {
      recursive_terminate(target);
    }

    // Se il chiamante non si è appena suicidato, rientrerà nei processi pronti
    if (currProc != NULL) {
      // Aggiorna lo stato nel PCB poiché stava gestendo una trap
      currProc->p_s = *exception_state;
      insertProcQ(&readyQueue, currProc);
    }

    // La terminazione richiede sempre una "pausa di valutazione" (blocca o
    // resetta il flusso logico attuale) Cederà forzatamente il comando allo
    // scheduler per pulire la CPU e ridistribuirla.
    is_blocking = 1;
    break;
  }

  // 6.3: Passeren (NSYS3 - P)
  // Diminuisce la capacità di un semaforo per il Sync Control, bloccando se in
  // attesa (valore 0).
  case PASSEREN: {
    int *sem = (int *)exception_state->reg_a1;
    if (*sem == 0) {
      // Semaforo esaurito, blocca forzatamente il processo in attesa che
      // qualcun altro faccia Verhogen.
      is_blocking = 1;

      // Salva lo stato nel suo descrittore per poterlo riattivare in un secondo
      // tempo
      currProc->p_s = *exception_state;
      currProc->p_semAdd =
          sem; // Ricorda a quale "sportello" si era messo in coda
      insertBlocked(sem, currProc);
    } else {
      // Il semaforo ha disponibilità di token: decrementa di uno la
      // disponibilità senza bloccarsi.
      (*sem)--;
    }
    break;
  }

  // 6.4: Verhogen (NSYS4 - V)
  // Aumenta la capacità/restituisce un token al semaforo risvegliando un
  // processo se presente.
  case VERHOGEN: {
    int *sem = (int *)exception_state->reg_a1;

    // Controlla se la disponibilità globale del mutex era crollata a zero
    // E (importante) vi sono processi in attesa su tale locazione
    if (*sem == 0 && headBlocked(sem)) {
      // Invece di incrementare e subito decrementare un token, esegui un
      // passaggio del testimone: Risveglia un processo (sarà pronto a tornare
      // su)
      pcb_t *p = removeBlocked(sem);

      // Azzera l'indicatore di attesa del processo svegliato,
      // rimuovendone il vincolo col semaforo da cui è stato appena estratto.
      p->p_semAdd = NULL;

      // Trasferisce il processo svegliato ai ranghi d'eleggibilità alla
      // Ready Queue
      insertProcQ(&readyQueue, p);
    } else {
      // Se nessun processo stava attendendo, la capacità locale torna a salire
      // regolarmente.
      (*sem)++;
    }
    break;
  }

  // 6.5: DoIO
  case DOIO: {
    unsigned int cmd_addr = (unsigned int)exception_state->reg_a1;

    // 1. Distanza esatta in byte dalla base di tutti i dispositivi hardware
    unsigned int total_offset = cmd_addr - START_ADDR;

    // 2. Ogni registro è grande 0x10 (16) byte.
    // Dividendo, otteniamo l'indice del dispositivo come se fosse un array
    // unico da 0 a 47.
    unsigned int flat_device_index = total_offset / 0x10;
    unsigned int index;

    if (flat_device_index < 32) {
      // Dispositivi normali (Disk, Flash, Net, Printer) occupano i primi 32
      // posti
      index = flat_device_index;
    } else {
      // Terminali (gli indici piatti vanno da 32 a 39).
      // Essendo due sotto-dispositivi, dobbiamo capire se è trasmissione o
      // ricezione.
      unsigned int devNo = flat_device_index - 32;

      // Il resto della divisione ci dà l'offset del registro specifico

      if (total_offset % 0x10 == 0x0C) { // Offset Comando Trasmissione
        index = 32 + devNo;
      } else { // Offset Comando Ricezione
        index = 40 + devNo;
      }
    }

    // Individua il semaforo corretto dall'indicizzazione calcolata poc'anzi
    int *sem_ptr = &subDevice[index];

    // Aggiorna lo stato nel PCB del chiamante: questo gli permetterà
    // di riprendere l'esecuzione perfettamente da dove si era interrotto.
    currProc->p_s = *exception_state;

    // Salva il semaforo associato al suo blocco fisico e lo inserisce nella
    // coda d'attesa (ASL)
    currProc->p_semAdd = (int *)sem_ptr;
    insertBlocked(sem_ptr, currProc);

    // Incrementa il conteggio dei processi fermi "virtualmente" per operazioni
    // sincrone (I/O)
    softBlockCount++;
    is_blocking = 1; // Forza il blocco: verrà invocato lo scheduler

    *(unsigned int *)cmd_addr = (unsigned int)exception_state->reg_a2;

    break;
  }

  // 6.6: GetCPUTime (NSYS6)
  // Restituisce al processo che ne fa richiesta il quantitativo esatto di tempo
  // speso finora sulla CPU.
  case GETTIME: {
    cpu_t currTime;
    // Campiona dal processore il valore temporale attuale
    STCK(currTime);

    // Il tempo di CPU di un processo non è solo la storicità in `p_time` ma ci
    // va sommato il "delta" corrente accumulato dall'ultimo avvio (differenza
    // tra timer attuale e timer iniziale)
    exception_state->reg_a0 = currProc->p_time + (currTime - processTimer);
    break;
  }

  // 6.7: WaitForClock (NSYS7)
  // Addormenta il processo per una frazione di tempo "sincronizzandolo" allo
  // pseudo-clock.
  case CLOCKWAIT: {
    // Il finto orologio logico ha a disposizione l'ultimo semaforo dell'array
    // come da specifiche (NRSEMAPHORES - 1)
    int *sem_ptr = &subDevice[NRSEMAPHORES - 1];

    // Tratta l'operazione letteralmente come una 'Passeren', congelando il
    // processo in attesa della V che avverrà nell'interruptHandler alla
    // scivolata del timer.
    currProc->p_s = *exception_state;
    currProc->p_semAdd = sem_ptr;
    insertBlocked(sem_ptr, currProc);

    // Aumenta softBlockCount per non riscontrare falsi positivi di Deadlock e
    // innesca il cambio CPU
    softBlockCount++;
    is_blocking = 1;
    break;
  }

  // 6.8: GetSupportData (NSYS8)
  case GETSUPPORTPTR: {
    // Utilizzato dai layers superiori
    exception_state->reg_a0 = (unsigned int)currProc->p_supportStruct;
    break;
  }

  // 6.9: GetProcessID (NSYS9)
  // Restituisce al chiamante il PID per indagini gerarchiche proprie o
  // ascendenti.
  case GETPROCESSID: {
    if (exception_state->reg_a1 == 0) {
      // Se il payload dice 0, la richiesta del PID riguarda se stesso.
      exception_state->reg_a0 = currProc->p_pid;
    } else {
      // Restituisce il PID del genitore se esiste,
      // altrimenti ritorna 0
      exception_state->reg_a0 =
          (currProc->p_parent) ? currProc->p_parent->p_pid : 0;
    }
    break;
  }

  // Il processo cede un quantitativo volontario di controllo ritornando
  // passivamente al fondo dello smistatore Round-Robin.
  // 6.10: Yield
  case YIELD: {
    // Salva lo stato corrente nel PCB
    currProc->p_s = *exception_state;

    insertProcQ(&readyQueue, currProc);

    is_blocking = 1;
    break;
  }

  // SYSCALL non di competenza o non valide
  default: { // 6.11: Tratta le SYSCALL non esistenti come Program Trap, stessa
    // logica precedente

    if (syscall_number <= 0) {
      // Se syscall_number < -10 o una syscall negativa sconosciuta, viene
      // segnalata come Trap per istruzione privilegiata (Privileged
      // Instruction) Generando l'Exception Code appropriato da inviare al
      // programTrapHandler.
      exception_state->cause = (exception_state->cause & ~CAUSE_EXCCODE_MASK) |
                               (PRIVINSTR << CAUSESHIFT);

      // IMPORTANTE: in questo caso specifico di errore,
      // il PC non dovrebbe essere avanzato perché l'istruzione è illegale.
      exception_state->pc_epc -= WORDLEN;
    }
    programTrapHandler();
  }
  }

  //   6.12: Ritorno da una SYSCALL non bloccante
  if (is_blocking == 0) { // Ricarica lo stato per riprendere l'esecuzione
    LDST(exception_state);
  } else { // 6.13: ritorno da una SYSCALL bloccante
    // Prima di uscire, carichiamo il tempo speso nel kernel sul processo
    cpu_t currTime;
    STCK(currTime);
    if (currProc != NULL) {
      currProc->p_time += (currTime - processTimer);
    }
    currProc = NULL;
    scheduler();
  }
}

/**
 * SEZIONE 8.2: Program Trap Exception Handling
 * SEZIONE 10: Process Termination
 *
 * Gestore per le eccezioni di tipo Program Trap.
 *
 * Gestisce errori di programma come istruzioni illegali, errori di
 * indirizzamento, ecc. Implementa la logica "Pass Up or Die".
 */
void programTrapHandler(void) {
  // 1. Controlla se il processo corrente possiede una struttura di supporto
  support_t *support = currProc->p_supportStruct;
  if (support) {
    // CASO "Pass Up": Il processo sa come gestire questa eccezione a livello
    // utente.
    unsigned int cpuNum = getPRID();

    // Salva lo stato al momento dell'eccezione nella struttura di supporto
    // (area GENERALEXCEPT)
    support->sup_exceptState[GENERALEXCEPT] =
        *(GET_EXCEPTION_STATE_PTR(cpuNum));

    // Recupera il contesto (stack, status, pc) specificato per la gestione
    // dell'eccezione
    context_t exeptCon = support->sup_exceptContext[GENERALEXCEPT];

    // Salva il tempo, preparandosi al context switch in user mode
    STCK(processTimer);

    // Passa il controllo all'handler definito dall'utente caricandone il
    // contesto
    LDCXT(exeptCon.stackPtr, exeptCon.status, exeptCon.pc);
  } else {
    // CASO "Die": Il processo non ha una struttura di supporto per sopravvivere
    // alla trap. Viene terminato assieme a tutta la sua progenie.
    recursive_terminate(currProc);
    scheduler();
  }
}

/**
 * SEZIONE 8.3: TLB Exception Handling
 *
 * Gestore per le eccezioni relative alla TLB (Translation Lookaside Buffer).
 *
 * Gestisce errori di traduzione degli indirizzi. Implementa la logica "Pass Up
 * or Die".
 */
void tlbHandler(void) {
  // 1. Verifica se è configurato un Support Level
  support_t *support = currProc->p_supportStruct;
  if (support) {
    // CASO "Pass Up": Il livello di supporto o l'OS ha un gestore per il Page
    // Fault.
    unsigned int cpuNum = getPRID();

    // Salva lo stato d'eccezione specifico per le anomalie paginazione
    // (PGFAULTEXCEPT)
    support->sup_exceptState[PGFAULTEXCEPT] =
        *(GET_EXCEPTION_STATE_PTR(cpuNum));

    // Estrae il contesto per mandare in esecuzione la routine di supporto
    // salvata
    context_t exeptCon = support->sup_exceptContext[PGFAULTEXCEPT];

    // Aggiorna il timing per lo scheduler
    STCK(processTimer);

    // Lancia e salta al gestore TLB del Support Level
    LDCXT(exeptCon.stackPtr, exeptCon.status, exeptCon.pc);
  } else {
    // CASO "Die": Senza modulo di supporto l'unica opzione è terminare e
    // rimuovere il processo per un errore irrimediabile.
    recursive_terminate(currProc);
    scheduler();
  }
}
