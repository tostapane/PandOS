#include "headers/initProc.h"
#include <uriscv/liburiscv.h>

/* Global variables definition */
swap_t swapPool[POOLSIZE];
int swapSemaphore;
int masterSemaphore;

// gestisce la concorrenza tra la shell e un suo processo figlio.
// la shell rimarrà bloccata finche il processo figlio non termina.
int shellSemaphore;
int devSemaphores[NSUPPSEM];
support_t supStructs[UPROCMAX];

/* Extern declarations for your exception handlers (assuming you name them
 * logically in sysSupport.c and vmSupport.c) */
extern void Pager();
extern void GeneralExceptionHandler();

void test() {
  /* 1. Initialize the Swap Pool and its semaphore [cite: 291] */
  swapSemaphore = 1; /* Mutual exclusion, start at 1  */
  for (int i = 0; i < POOLSIZE; i++) {
    swapPool[i].sw_asid = -1; /* -1 indicates unoccupied  */
  }

  /* 2. Initialize device semaphores [cite: 292] */
  for (int i = 0; i < NSUPPSEM; i++) {
    devSemaphores[i] = 1; /* Mutual exclusion for I/O [cite: 293] */
  }

  /* 3. Initialize synchronization semaphores  */
  masterSemaphore = 0;
  shellSemaphore = 0;

  /* 4. Prepare the initial processor state for the shell U-proc [cite: 314] */
  state_t shellState;
  shellState.pc_epc = UPROCSTARTADDR; /* 0x8000.00B0 [cite: 316] */
  shellState.reg_sp = USERSTACKTOP;   /* 0xC000.0000 [cite: 317] */
  /* User-mode, interrupts enabled, local timer enabled [cite: 318] */
  shellState.status = USERPON | IEPON | IMON | TEBITON;
  /* Shell ASID is 1 (0 is for kernel daemons) [cite: 48, 319] */
  shellState.entry_hi = (1 << ASIDSHIFT);

  /* 5. Initialize the Support Structure for the shell [cite: 321] */
  support_t *shellSup = &supStructs[0];
  shellSup->sup_asid = 1; /* [cite: 327] */

  /* Context 0: TLB Exception Handler (The Pager) [cite: 337] */
  shellSup->sup_exceptContext[0].pc = (memaddr)Pager;
  shellSup->sup_exceptContext[0].stackPtr =
      (memaddr) &
      (shellSup->sup_stackTLB[499]); /* Stacks grow down [cite: 340] */
  shellSup->sup_exceptContext[0].status =
      IEPON | IMON | TEBITON; /* Kernel mode, ints on [cite: 338] */

  /* Context 1: General Exception Handler [cite: 337] */
  shellSup->sup_exceptContext[1].pc = (memaddr)GeneralExceptionHandler;
  shellSup->sup_exceptContext[1].stackPtr =
      (memaddr) & (shellSup->sup_stackGen[499]);
  shellSup->sup_exceptContext[1].status =
      IEPON | IMON | TEBITON; /* Kernel mode, ints on [cite: 338] */

  /* Initialize the Page Table for the shell [cite: 331] */
  for (int i = 0; i < MAXPAGES - 1; i++) {
    /* VPN from 0x80000 to 0x8001E, ASID = 1 [cite: 80, 83] */
    shellSup->sup_privatePgTbl[i].pte_entryHI =
        ((0x80000 + i) << VPNSHIFT) | (1 << ASIDSHIFT);
    /* V=0, D=1, G=0  */
    shellSup->sup_privatePgTbl[i].pte_entryLO = DIRTYON;
  }
  /* Stack page initialization [cite: 81] */
  shellSup->sup_privatePgTbl[MAXPAGES - 1].pte_entryHI =
      (0xBFFFF << VPNSHIFT) | (1 << ASIDSHIFT);
  shellSup->sup_privatePgTbl[MAXPAGES - 1].pte_entryLO = DIRTYON;

  /* 6. Launch the shell process [cite: 305] */
  SYSCALL(CREATEPROCESS, (unsigned int)&shellState, (unsigned int)shellSup, 0);

  /* 7. Wait for the shell to terminate (Blocking P operation) [cite: 298] */
  SYSCALL(PASSEREN, (unsigned int)&masterSemaphore, 0, 0);

  /* 8. Shell is dead, now kill yourself (SYS2) to trigger a HALT [cite: 300] */
  SYSCALL(TERMPROCESS, 0, 0, 0);
}
