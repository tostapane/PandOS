#include "headers/initProc.h"
#include "../headers/const.h"
#include <uriscv/liburiscv.h>

/* Global variables definition */
swap_t swapPool[POOLSIZE];
int swapSemaphore;
int masterSemaphore;

int page_mutex_holder;

// gestisce la concorrenza tra la shell e un suo processo figlio.
// la shell rimarrà bloccata finche il processo figlio non termina.
int shellSemaphore;
int devSemaphores[NSUPPSEM];
support_t supStructs[UPROCMAX];

/* Extern declarations for your exception handlers (assuming you name them
 * logically in sysSupport.c and vmSupport.c) */
extern void Pager();
extern void GeneralExceptionHandler();

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
  support_t *shellSup = &supStructs[0];
  // la shell ha asid 1
  shellSup->sup_asid = 1;

  // Context 0: TLB Exception Handler (The Pager)
  shellSup->sup_exceptContext[0].pc = (memaddr)Pager;
  shellSup->sup_exceptContext[0].stackPtr =
      (memaddr) & (shellSup->sup_stackTLB[499]);
  shellSup->sup_exceptContext[0].status =
      MSTATUS_MPIE_MASK | MSTATUS_MPP_M; // Kernel mode

  /* Context 1: General Exception Handler */
  shellSup->sup_exceptContext[1].pc = (memaddr)GeneralExceptionHandler;
  shellSup->sup_exceptContext[1].stackPtr =
      (memaddr) & (shellSup->sup_stackGen[499]);
  shellSup->sup_exceptContext[1].status =
      MSTATUS_MPIE_MASK | MSTATUS_MPP_M; // Kernel mode

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

  // Launch the shell process
  SYSCALL(CREATEPROCESS, (unsigned int)&shellState, 1, (unsigned int)shellSup);

  // Wait for the shell to terminate (Blocking P operation)
  SYSCALL(PASSEREN, (unsigned int)&masterSemaphore, 0, 0);

  // 8. Shell is dead, now kill yourself (SYS2) to trigger a HALT
  SYSCALL(TERMPROCESS, 0, 0, 0);
}
