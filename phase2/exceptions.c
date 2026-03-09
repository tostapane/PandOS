#include "headers/exceptions.h"

/**
 * SEZIONE 5: Exception Handling
 *
 * Punto di ingresso principale per la gestione di tutte le eccezioni.
 *
 * Questa funzione viene chiamata dal BIOS ogni volta che si verifica un'eccezione
 * (esclusi i TLB-Refill). Il suo compito è determinare la causa dell'eccezione
 * e delegare il lavoro al gestore appropriato.
 *
 * Istruzioni:
 * 1. Ottieni lo stato del processore salvato dal BIOS (`BIOSDATAPAGE`).
 * 2. Leggi il registro `Cause` da questo stato per capire il motivo dell'eccezione.
 * 3. Usa la macro `CAUSE_IS_INT` per distinguere tra Interrupt e altre eccezioni.
 *    - Se è un interrupt, chiama il gestore degli interrupt (`interruptHandler` in interrupts.c).
 *    - Altrimenti, estrai l'Exception Code (`ExcCode`).
 * 4. In base all'Exception Code, chiama la funzione specifica:
 *    - `ExcCode` 8 o 11: chiama `syscallHandler()`.
 *    - `ExcCode` da 24 a 28 (TLB exceptions): chiama `tlbHandler()`.
 *    - Tutti gli altri `ExcCode` (Program Traps): chiama `programTrapHandler()`.
 */
void exceptionHandler(void)
{
  // Implementare la logica di dispatching qui.
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
 * Questa funzione gestisce le richieste di servizi del Nucleo fatte dai processi.
 *
 * Istruzioni:
 * 1. Controlla se la SYSCALL è stata chiamata da un processo in user-mode (Sez. 6.11).
 *    - Se sì, e se il numero della SYSCALL (in `a0`) è negativo, simula un'eccezione
 *      di tipo "Program Trap" (PRIVINSTR) e passa il controllo a `programTrapHandler()`.
 * 2. Se il controllo dei privilegi passa, usa uno `switch` sul valore del registro `a0`
 *    dello stato salvato per determinare quale SYSCALL eseguire (da -1 a -10).
 * 3. Per ogni SYSCALL, implementa la logica descritta nella documentazione.
 * 4. Gestisci correttamente il ritorno dalla SYSCALL (Sez. 6.12):
 *    - Per chiamate non bloccanti: incrementa il PC di 4, metti il valore di ritorno in `a0`
 *      dello stato salvato e fai `LDST` su quello stato.
 *    - Per chiamate bloccanti: salva lo stato nel PCB del processo (dopo aver incrementato il PC),
 *      aggiorna il tempo di CPU, esegui l'azione bloccante e chiama lo `scheduler()`.
 */
// Funzioni di supporto statiche per la gestione dei processi
static pcb_t *find_pcb(int pid);
static void recursive_terminate(pcb_t *proc);

void syscallHandler(void)
{
  // Puntatore allo stato del processore al momento dell'eccezione
  state_t *exception_state = (state_t *)BIOSDATAPAGE;

  // Estrae il numero della SYSCALL dal registro a0
  int syscall_number = exception_state->reg_a0;

  // Flag per gestire il ritorno dalle SYSCALL
  short int is_blocking = 0;

  // 6.11: Controllo per SYSCALL in User-Mode
  if (syscall_number < 0 && (exception_state->status & MSTATUS_MPP_MASK) == MSTATUS_MPP_U)
  {
    /**
     * L'utente ha chiamato una SYSCALL privilegiata (-1..-10) da user-mode.
     * Dato che non è permesso, come da Sezione 6.11 delle specifiche, bisogna
     * "simulare" un'eccezione di tipo "Program Trap".
     * L'operazione seguente modifica il codice dell'eccezione nel registro 'Cause':
     * 1. (exception_state->cause & ~GETEXECCODE): L'operatore NOT crea una
     *    maschera per azzerare i bit del codice eccezione corrente.
     *    L'operatore AND applica questa maschera, "pulendo" lo spazio.
     * 2. (PRIVINSTR << CAUSESHIFT): Prende il nuovo codice di eccezione per
     *    "Istruzione Privilegiata" (PRIVINSTR) e lo shifta nella posizione
     *    corretta all'interno del registro.
     * 3. L'operatore OR (|) combina i due risultati, inserendo
     *    il nuovo codice di eccezione nello spazio che era stato pulito.
     */
    exception_state->cause = (exception_state->cause & ~GETEXECCODE) | (PRIVINSTR << CAUSESHIFT);
    programTrapHandler();
    return;
  }

  // 6.12: Incremento del PC per evitare loop.
  // Per le chiamate bloccanti, questo stato aggiornato verrà salvato nel PCB.
  exception_state->pc_epc += WORDLEN;

  switch (syscall_number)
  {
  
  // 6.1: CreateProcess
  case CREATEPROCESS:
  {
    pcb_t *new_proc = allocPcb();
    if (new_proc == NULL)
    { // Codice di errore: No More PCB
      exception_state->reg_a0 = -1;
    }
    else
    {
      state_t *new_state = (state_t *)exception_state->reg_a1;
      new_proc->p_s = *new_state;
      new_proc->p_supportStruct = (support_t *)exception_state->reg_a2;
      new_proc->p_time = 0;
      new_proc->p_semAdd = NULL;

      insertChild(currProc, new_proc);
      insertProcQ(&readyQueue, new_proc);
      processCount++;

      // Ritorna il PID del nuovo processo
      exception_state->reg_a0 = new_proc->p_pid;
    }
    break;
  }

  // 6.2: TerminateProcess
  case TERMPROCESS:
  {
    pcb_t *target = (exception_state->reg_a1 == 0) ? currProc : find_pcb(exception_state->reg_a1);
    if (target)
    {
      recursive_terminate(target);
    }
    // La terminazione è sempre un'operazione che blocca il flusso normale
    // e richiede di chiamare lo scheduler.
    is_blocking = 1;
    scheduler();
    break;
  }

  // 6.8: GetSupportData
  case GETSUPPORTPTR:
  {
    exception_state->reg_a0 = (unsigned int)currProc->p_supportStruct;
    break;
  }

  // 6.9: GetProcessID
  case GETPROCESSID:
  {
    if (exception_state->reg_a1 == 0)
    { // PID del processo corrente
      exception_state->reg_a0 = currProc->p_pid;
    }
    else
    { // PID del genitore
      exception_state->reg_a0 = (currProc->p_parent) ? currProc->p_parent->p_pid : 0;
    }
    break;
  }

  // 6.10: Yield
  case YIELD:
  {
    // Salva lo stato corrente nel PCB
    currProc->p_s = *exception_state;
    // Rimette il processo in coda
    insertProcQ(&readyQueue, currProc);
    is_blocking = 1;
    scheduler();
    break;
  }

  // SYSCALL non di competenza o non valide
  default:
  { // 6.11: Tratta le SYSCALL non esistenti come Program Trap, stessa logica precedente
    exception_state->cause = (exception_state->cause & ~GETEXECCODE) | (PRIVINSTR << CAUSESHIFT);
    programTrapHandler();
    return;
  }
  }

  // 6.12: Ritorno da una SYSCALL non bloccante
  if (!is_blocking)
  { // Ricarica lo stato per riprendere l'esecuzione
    LDST(exception_state);
  }
}

// TODO: sicuramente da spostare alla parte della fase 1 perché implementa funzioni del livello precedente 
/**
 * @brief Trova un PCB nella pcbFree list dato un PID.
 * @param pid Il Process ID da cercare.
 * @return Puntatore al PCB se trovato, altrimenti NULL.
 */
static pcb_t *find_pcb(int pid)
{
  struct list_head *pos;
  list_for_each(pos, &pcbFree_h)
  {
    pcb_t *p = container_of(pos, pcb_t, p_list);
    if (p->p_pid == pid)
    {
      return p;
    }
  }
  return NULL;
}

/**
 * @brief Termina ricorsivamente un processo e i suoi figli (Sezione 10).
 */
static void recursive_terminate(pcb_t *proc)
{
  // Termina ricorsivamente tutti i figli
  while (!emptyChild(proc))
  {
    recursive_terminate(removeChild(proc));
  }

  // Rimuove il processo da qualunque stato si trovi
  if (proc == currProc)
  {
    outChild(proc);
  }
  else if (outProcQ(&readyQueue, proc) == NULL)
  {
    pcb_t *unblocked = outBlocked(proc);
    if (unblocked != NULL)
    { // Controlla se era bloccato su un semaforo di I/O
      int *sem_addr = unblocked->p_semAdd;
      if (sem_addr >= &subDevice[0] && sem_addr < &subDevice[SEMDEVLEN])
      {
        softBlockCount--;
      }
    }
  }
  else
  { // Se era sulla ready queue, outProcQ ha già fatto il suo lavoro
    outChild(proc);
  }

  freePcb(proc);
  processCount--;
}

/**
 * SEZIONE 8.2: Program Trap Exception Handling
 * SEZIONE 10: Process Termination
 *
 * Gestore per le eccezioni di tipo Program Trap.
 *
 * Gestisce errori di programma come istruzioni illegali, errori di indirizzamento, ecc.
 * Implementa la logica "Pass Up or Die".
 *
 * Istruzioni:
 * 1. Accedi al PCB del `CurrentProcess`.
 * 2. Controlla se il suo puntatore `p_supportStruct` è `NULL`.
 *    - Se è `NULL` ("Die"): termina il processo e tutta la sua progenie. Questo
 *      richiede di chiamare la logica di terminazione (Sez. 10) che implementa NSYS2.
 *      Infine, chiama lo `scheduler()`.
 *    - Se NON è `NULL` ("Pass Up"):
 *      a. Copia lo stato d'eccezione dal `BIOSDATAPAGE` in `sup_exceptState[GENERALEXCEPT]`
 *         nella `support_t` del processo.
 *      b. Esegui un `LDCXT` usando il contesto salvato in `sup_exceptContext[GENERALEXCEPT]`
 *         per passare il controllo al gestore di eccezioni del Livello di Supporto.
 */
void programTrapHandler(void)
{
  // Implementare la logica "Pass Up or Die" per Program Trap qui.
}

/**
 * SEZIONE 8.3: TLB Exception Handling
 *
 * Gestore per le eccezioni relative alla TLB (Translation Lookaside Buffer).
 *
 * Gestisce errori di traduzione degli indirizzi. Implementa la logica "Pass Up or Die".
 *
 * Istruzioni:
 * 1. La logica è quasi identica a `programTrapHandler`.
 * 2. L'unica differenza è che, nel caso "Pass Up", devi usare gli indici `PGFAULTEXCEPT`
 *    invece di `GENERALEXCEPT` per accedere ai campi `sup_exceptState` e `sup_exceptContext`.
 *      a. Copia lo stato d'eccezione in `sup_exceptState[PGFAULTEXCEPT]`.
 *      b. Esegui `LDCXT` con il contesto `sup_exceptContext[PGFAULTEXCEPT]`.
 */
void tlbHandler(void)
{
  // Implementare la logica "Pass Up or Die" per TLB exception qui.
}

/**
 * SEZIONE 4: TLB-Refill
 *
 * Gestore "placeholder" per gli eventi di TLB-Refill.
 *
 * Questa funzione è un gestore speciale che viene chiamato solo per eventi di
 * TLB-Refill. Per la Fase 2, il suo codice è fisso e fornito dalla documentazione.
 */
void uTLB_RefillHandler(void)
{
  setENTRYHI(0x80000000);
  setENTRYLO(0x00000000);
  TLBWR();
  LDST((state_t *)BIOSDATAPAGE);
}
