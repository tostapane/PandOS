#include "./headers/initial.h"
#include <uriscv/const.h>
#include <uriscv/types.h>

// 2.1

// the number of started, but not yet terminated processes
unsigned int processCount;

// the number of started, but not terminated processes
// that are in the "blocked" state due to an I/O or timer request
unsigned int softBlockCount;

// a queue of PCBs that are in "ready" state
struct list_head readyQueue;

// the current executing process
pcb_t *currProc;

// semaphore for external (sub)device
unsigned int subDevice[NRSEMAPHORES];

// semaphore to support the pseudo clock
unsigned int pseudoClock;

int main() {

  // 2.2

  /* Struttura che serve a gestire le eccezioni incluse quelle relative alla
   * TLB. L'hardware sa di dovere condultare questa locazione di memoria
   * (predefinita) quando si verificano eccezioni.
   * */
  passupvector_t *passupvector = (passupvector_t *)
      PASSUPVECTOR; // indirizzo di memoria definito in headers/const.h

  // this code will be replaced when the support level is implemented
  // indirizzo della funzione che deve gestire i TLB miss
  passupvector->tlb_refill_handler = (memaddr)uTLB_RefillHandler;

  /* Assegnazione di un area di memoria sicura e privata (dedicata al KERNEL)
   * contenente lo stack dedicato alla gestione del TLB-Refill.
   * Quando avviene un TLB miss, la funzione viene eseguita all'interno di
   * questa area di memoria. Questo stack è come un'area di lavoro. I processi
   * normali non possono ne leggere ne scrivere nel KERNELSTACK.
   *
   * Memoria utilizzata dalla funzione uTLB_RefillHandler sopra.*/
  passupvector->tlb_refill_stackPtr = (memaddr)KERNELSTACK;

  /* Indirizzo della funzione che deve gestire eccezioni di altro tipo. */
  passupvector->exception_handler = (memaddr)exceptionHandler;

  /* uTLB_RefillHandler e exceptionHandler usano la stessa area di memoria.
   * Ovviamente questa area viene acceduta in mutua esclusione; quando la CPU
   * sta gestendo un TLB-miss, le interruzioni sono disabilitate (non possono
   * verificarsi eccezioni di altro tipo).
   * In particolare durante la gestione di una eccezione:
   * - Le interruzioni (esterne al kernel), vengono "prenotate" e gestite al
   * termine dell'eccezione.
   * - Le eccezioni (interne al kernel), sono errori fatali che causano il crash
   * del sistema. Si assume che non avvengano errori fatali all'interno del
   * kernel. Esso non è progettato per gestire una eccezione dentro l'altra
   * tutte sullo stesso stack. Sollevare una eccezione (interna) comporterebbe
   * richiamare una funzione che utilizza lo stesso stack che stiamo usando al
   * momento, sovrascrivendolo!!*/
  passupvector->exception_stackPtr = (memaddr)KERNELSTACK;

  // 2.3
  // inizializzazione di strutture dati e delle variabili
  // dichiarate precedentemente
  // inizializza la coda dei pcb
  initPcbs();

  // inizializza la coda dei semafori
  // lista di semafori attivi, che hanno almeno un processo in attesa
  // (Active Semaphore List)
  initASL();

  // 2.4
  processCount = 0;
  softBlockCount = 0;
  mkEmptyProcQ(&readyQueue);
  currProc = NULL;
  for (int i = 0; i < NRSEMAPHORES; i++)
    subDevice[i] = 0;
  pseudoClock = 0;

  // 2.5
  // load the system-wide Interval Timer with 100 milliseconds (constant
  // PSECOND)   (MACRO)
  LDIT(PSECOND);

  // 2.6
  // alloca un nuovo processo
  pcb_t *proc = allocPcb();
  // inserisce il processo nella readyQueue
  insertProcQ(&readyQueue, proc);

  processCount++;

  // to enable interrupts
  proc->p_s.mie = MIE_ALL;
  // to enable interrupt and kernel mode
  proc->p_s.status = MSTATUS_MPIE_MASK | MSTATUS_MPP_M;

  // set the SP (stack pointer) to RAMTOP
  RAMTOP(proc->p_s.reg_sp);

  // set the PC to the address of 'test'
  proc->p_s.pc_epc = (memaddr)test;

  // set all the process tree fields on NULL
  INIT_LIST_HEAD(&proc->p_child);
  INIT_LIST_HEAD(&proc->p_sib);
  proc->p_parent = NULL;

  proc->p_time = 0;
  proc->p_semAdd = NULL;
  proc->p_supportStruct = NULL;

  // 2.7
  // call the scheduler
  // questa funzione deve ancora essere implementata
  scheduler();
}
