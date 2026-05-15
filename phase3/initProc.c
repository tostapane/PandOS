#include "headers/initProc.h"
#include "../headers/const.h"
#include "headers/vmSupport.h"
#include <uriscv/liburiscv.h>
#include <uriscv/types.h>

/* Global variables definition */
swap_t swapPool[POOLSIZE];
int swapSemaphore;
int masterSemaphore;

// lista delle supprt struct libere
LIST_HEAD(supStructs_freeList);

int page_mutex_holder;

// gestisce la concorrenza tra la shell e un suo processo figlio.
// la shell rimarrà bloccata finche il processo figlio non termina.
int shellSemaphore;
int devSemaphores[NSUPPSEM];
support_t supStructs[UPROCMAX];

// buffer che usiamo per leggere l'header senza usare lo stack del kernel
// static poiché viene usato solo in initProc.c
// usando 'static' la variabile non va nello stack ma nella sezione (.data) del
// kernel allocata dal compilatore ancora prima che il SO parta
static char shellHeaderBuf[PAGESIZE];

/* Extern declarations for your exception handlers (assuming you name them
 * logically in sysSupport.c and vmSupport.c) */
extern void Pager();
extern void GeneralExceptionHandler();

// restituisce una struttura libera se esiste, NULL altrimenti
support_t *allocateSupport() {
  if (list_empty(&supStructs_freeList))
    return NULL;
  struct list_head *l = supStructs_freeList.next;
  list_del(l);
  return container_of(l, support_t, s_list);
}

// rende una struttura nuovamente disponibile
void deallocateSupport(support_t *s) {
  list_add(&(s->s_list), &supStructs_freeList);
}

// Instantiator Process
void test() {

  // Initialize the Swap Pool and its semaphore
  swapSemaphore = 1; // Mutual exclusion, start at
  for (int i = 0; i < POOLSIZE; i++) {
    swapPool[i].sw_asid = -1; // -1 == unoccupied
  }

  // Initialize device semaphores
  for (int i = 0; i < NSUPPSEM; i++) {
    devSemaphores[i] = 1;
  }

  /* Initialize supStructs_freeList and support structs */
  INIT_LIST_HEAD(&supStructs_freeList);
  for (int i = 0; i < UPROCMAX; i++) {
    deallocateSupport(&supStructs[i]);
  }

  // Initialize synchronization semaphores
  masterSemaphore = 0;
  shellSemaphore = 0;

  // contiene l'asid del processo che ha la mutua esclusione sulla page table
  page_mutex_holder = -1;

  // Prepare the initial processor state for the shell U-proc
  state_t shellState;
  shellState.pc_epc = UPROCSTARTADDR; // 0x8000.00B0
  shellState.reg_sp = USERSTACKTOP;   // 0xC000.0000
  // User-mode, interrupts enabled, local timer enabled
  shellState.status = MSTATUS_MPIE_MASK | MSTATUS_MPP_U;
  // Shell ASID is 1 (0 is for kernel daemons)
  shellState.entry_hi = (1 << ASIDSHIFT);

  // Initialize the Support Structure for the shell
  support_t *shellSup = allocateSupport();
  if (shellSup == NULL)
    SYSCALL(TERMPROCESS, 0, 0, 0);

  // la shell ha asid 1
  shellSup->sup_asid = 1;

  // Context 0: TLB Exception Handler (The Pager)
  shellSup->sup_exceptContext[0].pc = (memaddr)Pager;

  // estrazione di ramtop
  memaddr ramtop = RAMTOP(ramtop);
  // TODO: delete it
  // shellSup->sup_exceptContext[0].stackPtr =
  // (memaddr) & (shellSup->sup_stackTLB[499]);
  shellSup->sup_exceptContext[0].stackPtr = ramtop - PAGESIZE;

  shellSup->sup_exceptContext[0].status =
      MSTATUS_MPIE_MASK | MSTATUS_MPP_M; // Kernel mode

  /* Context 1: General Exception Handler */
  shellSup->sup_exceptContext[1].pc = (memaddr)GeneralExceptionHandler;
  // TODO: delete it
  // shellSup->sup_exceptContext[1].stackPtr =
  //    (memaddr) & (shellSup->sup_stackGen[499]);
  shellSup->sup_exceptContext[1].stackPtr = ramtop - (2 * PAGESIZE);
  shellSup->sup_exceptContext[1].status =
      MSTATUS_MPIE_MASK | MSTATUS_MPP_M; // Kernel mode

  // TODO: delete it
  /*
  // Initialize the Page Table for the shell
  for (int i = 0; i < MAXPAGES - 1; i++) {
    shellSup->sup_privatePgTbl[i].pte_entryHI =
        ((0x80000 + i) << VPNSHIFT) | (1 << ASIDSHIFT);
    shellSup->sup_privatePgTbl[i].pte_entryLO = DIRTYON;
  }
  // Stack page initialization
  shellSup->sup_privatePgTbl[MAXPAGES - 1].pte_entryHI =
      (0xBFFFF << VPNSHIFT) | (1 << ASIDSHIFT);
  shellSup->sup_privatePgTbl[MAXPAGES - 1].pte_entryLO = DIRTYON;
  */

  // 10.3
  volatile memaddr flash0Base =
      START_DEVREG + ((INTLINE_FLASH - INTLINE_DISK) * 0x80) + (0 * 0x10);
  volatile dtpreg_t *flash0 = (volatile dtpreg_t *)flash0Base;

  flash0->data0 = (memaddr)shellHeaderBuf;
  SYSCALL(DOIO, (unsigned int)&(flash0->command), FLASHREAD, 0);

  unsigned int textSize = *((unsigned int *)shellHeaderBuf + 1);
  unsigned int numTextPages = textSize / PAGESIZE;
  if ((textSize & PAGESIZE) != 0) {
    numTextPages++;
  }

  // initialize the page table for the shell
  for (int i = 0; i < MAXPAGES - 1; i++) {
    shellSup->sup_privatePgTbl[i].pte_entryHI =
        ((0x80000 + i) << VPNSHIFT) | (1 << ASIDSHIFT);

    if (i < numTextPages) {
      shellSup->sup_privatePgTbl[i].pte_entryLO = 0; // sola lettura

    } else {
      shellSup->sup_privatePgTbl[i].pte_entryLO = DIRTYON; // scrivibile
    }
  }

  // stack page initialization
  shellSup->sup_privatePgTbl[MAXPAGES - 1].pte_entryHI =
      (0xBFFFF << VPNSHIFT) | (1 << ASIDSHIFT);
  shellSup->sup_privatePgTbl[MAXPAGES - 1].pte_entryLO = DIRTYON;

  // Launch the shell process
  SYSCALL(CREATEPROCESS, (unsigned int)&shellState, 1, (unsigned int)shellSup);

  // Wait for the shell to terminate (Blocking P operation)
  SYSCALL(PASSEREN, (unsigned int)&masterSemaphore, 0, 0);

  // 8. Shell is dead, now kill yourself (SYS2) to trigger a HALT
  SYSCALL(TERMPROCESS, 0, 0, 0);
}
