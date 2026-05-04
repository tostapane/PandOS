#include "headers/sysSupport.h"
#include "../headers/const.h"
#include "headers/initProc.h" /* For the semaphores */
#include <uriscv/const.h>
#include <uriscv/liburiscv.h>
#include <uriscv/types.h>

/* Assuming you have swapSemaphore defined in initProc.c */
extern int swapSemaphore;
extern int masterSemaphore;
extern int shellSemaphore;

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
  /* Retrieve the arguments from the saved state's registers.
   * a0 = syscall number, a1-a3 = parameters[cite: 237, 238].
   */
  // e' giusto usare general except?
  unsigned int syscallNum = supStruct->sup_exceptState[GENERALEXCEPT].reg_a0;

  /* CRITICAL: Increment PC by 4 to avoid an infinite loop! */
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

    // creo il nuovo processo a priorita' 1
    SYSCALL(CREATEPROCESS, (unsigned int)&newState, 1,
            (unsigned int)newSupport);

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
  /* * The process is going to die. We need to clean up its mess.
   */

  /* 1. Did this idiot process die while holding the Swap Pool semaphore?
   * If so, we MUST release it (V operation) before terminating,
   * or the whole virtual memory system deadlocks.
   */

  /* Note: You need a way to track if THIS specific process holds the lock.
   * A simple global boolean flag set in the Pager before the P operation
   * and cleared after the V operation works wonders for Phase 3.
   */
  // if (process_holds_swap_mutex) {
  //     SYSCALL(VERHOGEN, (unsigned int)&swapSemaphore, 0, 0);
  //     process_holds_swap_mutex = 0;
  // }

  /* 2. Signal the appropriate synchronization semaphore[cite: 245].
   * If this is ASID 1 (the shell), we V the masterSemaphore to wake up
   * `test`[cite: 245, 299]. If this is ASID > 1 (a child program), we V the
   * shellSemaphore to wake up the shell[cite: 245, 304].
   */
  if (supStruct->sup_asid == 1) {
    SYSCALL(VERHOGEN, (unsigned int)&masterSemaphore, 0, 0);
  } else {
    SYSCALL(VERHOGEN, (unsigned int)&shellSemaphore, 0, 0);
  }

  /* 3. Put it out of its misery via the Nucleus NSYS2 service[cite: 282, 300].
   */
  SYSCALL(TERMPROCESS, 0, 0, 0);

  /* We never reach this point. */
}
