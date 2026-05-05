#include "headers/sysSupport.h"
#include "../headers/const.h"
#include "headers/initProc.h" /* For the semaphores */
#include <uriscv/const.h>
#include <uriscv/liburiscv.h>
#include <uriscv/types.h>

/* Assuming you have swapSemaphore defined in initProc.c
extern int swapSemaphore;
extern int masterSemaphore;
extern int shellSemaphore;
*/
void GeneralExceptionHandler() {
  /* 1. Get the current process's Support Structure via SYSCALL 8 */
  support_t *supStruct = (support_t *)SYSCALL(GETSUPPORTPTR, 0, 0, 0);

  /* 2. Determine the cause of the exception.
   * Note: Non-TLB exceptions use context 1 (GENERALEXCEPT)[cite: 337].
   */
  unsigned int cause = supStruct->sup_exceptState[1].cause;
  unsigned int excCode = (cause & GETEXECCODE) >> CAUSESHIFT;

  /* 3. Route the exception */
  if (excCode == SYSEXCEPTION) {
    SyscallExceptionHandler(supStruct, excCode);
  } else {
    /* Everything else (illegal instructions, address errors, etc.) is a Trap */
    ProgramTrapHandler(supStruct);
  }
}

void SyscallExceptionHandler(support_t *supStruct, unsigned int excCode) {
  /* Retrieve the arguments from the saved state's registers
   * a0 = syscall number, a1-a3 = parameters
   */
  unsigned int syscallNum = supStruct->sup_exceptState[GENERALEXCEPT].reg_a0;

  // Increment PC by 4
  supStruct->sup_exceptState[1].pc_epc += WORDLEN;

  switch (syscallNum) {
  case TERMINATE: /* SYS2 (2) */
    /* Treat an intentional termination exactly like a Program Trap[cite: 283].
     */
    ProgramTrapHandler(supStruct);
    break;

  case WRITETERMINAL: /* SYS4 (4) */
    /* YOUR CODE HERE:
     * Validate that the string in a1 and length in a2 are within the
     * process's logical address space (kuseg)[cite: 256].
     * If it's outside, or len < 0 or len > 128, call ProgramTrapHandler![cite:
     * 256]. Otherwise, write it out character by character.
     */
    break;

  case READTERMINAL: {
    // seleziono il semaforo del terminal 0 in lettura
    unsigned int readMutex = (unsigned int)&(devSemaphores[40]);
    SYSCALL(PASSEREN, readMutex, 0, 0);
    unsigned int vAddr =
        (unsigned int)supStruct->sup_exceptState[GENERALEXCEPT].reg_a1;
    char *virtAddr = (char *)supStruct->sup_exceptState[GENERALEXCEPT].reg_a1;
    unsigned int nrecvd = 0;
    while (1) {
      // controllo che legga dalla parte di memoria corretta
      if (vAddr < KUSEG || vAddr + nrecvd >= USERSTACKTOP) {
        SYSCALL(VERHOGEN, readMutex, 0, 0);
        ProgramTrapHandler(supStruct);
        return;
      }
      // si usa sempre il terminal 0
      volatile memaddr term0base = START_ADDR + (4 * 0x80) + (0 * 0x10);
      volatile termreg_t *term_register = (volatile termreg_t *)term0base;
      unsigned int commAddr = (unsigned int)&term_register->recv_command;
      int ioStatus = SYSCALL(DOIO, commAddr, RECEIVECHAR, 0);
      if ((ioStatus & 0xFF) != READTERMINAL) {
        supStruct->sup_exceptState[GENERALEXCEPT].reg_a0 = -(ioStatus & 0xFF);
        break;
      }
      // uso una maschera opposta a quella usata per isolare lo status
      char c = (ioStatus & 0xFF00) >> 8;
      virtAddr[nrecvd] = c;
      nrecvd++;
      if (c == '\n') {
        supStruct->sup_exceptState[GENERALEXCEPT].reg_a0 = nrecvd;
        break;
      }
    }
    SYSCALL(VERHOGEN, readMutex, 0, 0);
    break;
  }
  case EXECUTE: {
    if (supStruct->sup_asid != 1) {
      ProgramTrapHandler(supStruct);
      return;
    }
    unsigned int asid = supStruct->sup_exceptState[GENERALEXCEPT].reg_a1;

    // inizializzo status del nuovo processo
    state_t newState;
    newState.pc_epc = UPROCSTARTADDR;
    newState.reg_sp = USERSTACKTOP;
    newState.status = USERPON | IEPON | IMON | TEBITON;
    newState.entry_hi = asid << ASIDSHIFT;

    // inizializzo sup_struct del nuovo processo
    support_t *newSupport = &supStructs[asid - 1];

    // asid
    newSupport->sup_asid = asid;

    // tlb handler
    newSupport->sup_exceptContext[0].pc = (memaddr)Pager;
    newSupport->sup_exceptContext[0].stackPtr =
        (memaddr) & (newSupport->sup_stackTLB[499]);
    newSupport->sup_exceptContext[0].status = IEPON | IMON | TEBITON;
    // general exception handler
    newSupport->sup_exceptContext[1].pc = (memaddr)GeneralExceptionHandler;
    newSupport->sup_exceptContext[1].stackPtr =
        (memaddr) & (newSupport->sup_stackGen[499]);
    newSupport->sup_exceptContext[1].status = IEPON | IMON | TEBITON;

    // private page table
    for (int i = 0; i < 31; i++) {
      newSupport->sup_privatePgTbl[i].pte_entryHI =
          ((0x80000 + i) << VPNSHIFT) | (asid << ASIDSHIFT);
      newSupport->sup_privatePgTbl[i].pte_entryLO = DIRTYON;
    }
    newSupport->sup_privatePgTbl[31].pte_entryHI =
        (0xBFFFF << VPNSHIFT) | (asid << ASIDSHIFT);
    newSupport->sup_privatePgTbl[31].pte_entryLO = DIRTYON;

    // creo il nuovo processo a priorita' 1
    SYSCALL(CREATEPROCESS, (unsigned int)&newState, 1,
            (unsigned int)newSupport);

    // fermo la shell
    SYSCALL(PASSEREN, (unsigned int)&shellSemaphore, 0, 0);

    break;
  }

  default:
    /* A user-proc requested a SYSCALL we don't support. Kill it. */
    ProgramTrapHandler(supStruct);
    break;
  }

  /* If we didn't terminate, return control to the process */
  LDST(&(supStruct->sup_exceptState[1]));
}

void ProgramTrapHandler(support_t *supStruct) {
  // The process is going to die. We need to clean up its mess.

  // controllo se ha la mutua esclusione sulla page table
  if (page_mutex_holder == supStruct->sup_asid) {
    page_mutex_holder = -1;
    SYSCALL(VERHOGEN, (unsigned int)&swapSemaphore, 0, 0);
  }
  // cerco il semaforo corretto
  if (supStruct->sup_asid == 1) {
    /* When the shell terminates, either normally or abnormally, it should
      perform a V on the masterSemaphore */
    SYSCALL(VERHOGEN, (unsigned int)&masterSemaphore, 0, 0);
  } else {
    // se e' un figlio della shell, sveglia la shell
    SYSCALL(VERHOGEN, (unsigned int)&shellSemaphore, 0, 0);
  }

  // termina il proceso
  SYSCALL(TERMPROCESS, 0, 0, 0);
}
