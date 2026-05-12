#include "./headers/initial.h"
#include "../headers/const.h"
#include "../headers/types.h"
#include <uriscv/liburiscv.h>

// Numero di processi avviati ma non ancora terminati
unsigned int processCount;

// Numero di processi avviati, ma non terminati,
// che si trovano nello stato "bloccato" a causa di un'operazione di I/O o di
// una richiesta al timer
unsigned int softBlockCount;

// Coda dei PCB (Process Control Block) che si trovano nello stato "ready"
// (pronti per l'esecuzione)
struct list_head readyQueue;

// Il processo attualmente in esecuzione
pcb_t *currProc;

// Semafori per i dispositivi esterni
// L'ultimo elemento dell'array è dedicato allo pseudoclock
int subDevice[NRSEMAPHORES];

// Variabile utilizzata per salvare il tempo di inizio dell'esecuzione di un
// processo
cpu_t processTimer;

/**
 * SECTION 3: The TLB-Refill event handler
 */
void uTLB_RefillHandler() {
  // 1. Get the pointer to the saved state from the BIOS data page
  state_t *saved_state = (state_t *)BIOSDATAPAGE;

  // 2. Read the register EntryHi that caused the TLB Miss
  unsigned int missing_EntryHi = saved_state->entry_hi;

  // 3. Extract the Virtual Page Number (VPN).
  // The logical address is in the upper bits. It depends on the macros in
  // liburiscv.h, but conceptually you need to isolate the page index.
  unsigned int vpn = (missing_EntryHi & GETPAGENO) >> VPNSHIFT;
  int p;
  if (vpn >= 0xBFFFF)
    p = 31; // stack page
  else
    p = vpn - 0x80000; // text/data pages

  // 4. Take the entry from the Page Table in the Support Structure of the
  // Current Process, at index 'p'.
  pteEntry_t missing_pte = currProc->p_supportStruct->sup_privatePgTbl[p];

  // 5. Load the values into the registers
  setENTRYHI(missing_pte.pte_entryHI);
  setENTRYLO(missing_pte.pte_entryLO);

  // 6. Write the entry to the TLB
  TLBWR();

  // 7. Reload the state saved at the beginning
  LDST(saved_state);
}

int main() {

  /* Struttura che serve a gestire le eccezioni incluse quelle relative alla
   * TLB. L'hardware sa di dovere condultare questa locazione di memoria
   * (predefinita) quando si verificano eccezioni.
   * */
  passupvector_t *passupvector = (passupvector_t *)
      PASSUPVECTOR; // indirizzo di memoria definito in headers/const.h

  // Questo codice verrà rimpiazzato quando sarà implementato il livello di
  // supporto (Support Level). Indirizzo della funzione che deve gestire i TLB
  // miss.
  passupvector->tlb_refill_handler = (memaddr)uTLB_RefillHandler;

  /* Stack kernel isolato e protetto, riservato all'esecuzione di
   * uTLB_RefillHandler. */
  passupvector->tlb_refill_stackPtr = (memaddr)KERNELSTACK;

  /* Indirizzo della funzione che deve gestire eccezioni di altro tipo. */
  passupvector->exception_handler = (memaddr)exceptionHandler;

  /* KERNELSTACK condiviso con exceptionHandler. Sicuro via interruzioni
   * disabilitate. Eccezioni kernel annidate non sono supportate
   * (corromperebbero lo stack). */
  passupvector->exception_stackPtr = (memaddr)KERNELSTACK;

  // Inizializza la coda dei pcb
  initPcbs();

  // Inizializza la coda dei semafori
  // Lista di semafori attivi, che hanno almeno un processo in attesa
  initASL();

  // 2.4 Inizializzazione delle variabili globali
  // Nessun processo è ancora stato creato, quindi contatori e clock a zero
  processCount = 0;
  softBlockCount = 0;

  // Svuota/inizializza la coda dei processi pronti (Ready Queue)
  mkEmptyProcQ(&readyQueue);

  // Nessun processo è attualmente in esecuzione
  currProc = NULL;

  // Inizializza a zero l'array di semafori dei dispositivi e lo pseudoclock
  for (int i = 0; i < NRSEMAPHORES; i++)
    subDevice[i] = 0;

  // 2.5
  // Carica nell'Interval Timer di sistema il valore di 100 millisecondi
  // L'Interval Timer genererà un interrupt allo scadere del tempo.
  LDIT(PSECOND);

  // 2.6 Istanziazione e inizializzazione del primo processo
  pcb_t *proc = allocPcb(); // Alloca un nuovo Process Control Block

  // Inserisce il processo appena allocato nella coda dei processi pronti
  insertProcQ(&readyQueue, proc);

  // Incrementa il contatore globale per includere il processo appena creato
  processCount++;

  // Abilita tutti i tipi di interrupt per questo processo
  proc->p_s.mie = MIE_ALL;

  // Imposta lo stato del processo: mantiene la modalità Kernel (MPP_M) e
  // abilita gli interrupt precedenti (MPIE)
  proc->p_s.status = MSTATUS_MPIE_MASK | MSTATUS_MPP_M;

  // Imposta lo Stack Pointer (SP) alla cima della RAM (RAMTOP), poiché lo stack
  // cresce verso il basso
  RAMTOP(proc->p_s.reg_sp);

  // Imposta il Program Counter (PC) all'indirizzo iniziale della funzione
  // 'test'
  proc->p_s.pc_epc = (memaddr)test;

  // L'avvio finale: chiama lo scheduler per passare il controllo alla CPU in
  // favore del primo processo
  scheduler();
}
