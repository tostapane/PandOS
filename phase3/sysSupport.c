#include "sysSupport.h"
#include "initProc.h" /* For the semaphores */
#include <uriscv/liburiscv.h>

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
    unsigned int syscallNum = supStruct->sup_exceptState[1].reg_a0;
    
    /* CRITICAL: Increment PC by 4 to avoid an infinite loop! */
    supStruct->sup_exceptState[1].pc_epc += 4;

    switch (syscallNum) {
        case TERMINATE: /* SYS2 (2) */
            /* Treat an intentional termination exactly like a Program Trap[cite: 283]. */
            ProgramTrapHandler(supStruct);
            break;

        case WRITETERMINAL: /* SYS4 (4) */
            /* YOUR CODE HERE:
             * Validate that the string in a1 and length in a2 are within the 
             * process's logical address space (kuseg)[cite: 256].
             * If it's outside, or len < 0 or len > 128, call ProgramTrapHandler![cite: 256].
             * Otherwise, write it out character by character.
             */
            break;

        case READTERMINAL: /* SYS5 (5) */
            /* YOUR CODE HERE:
             * Validate address space. Read character by character[cite: 265].
             */
            break;

        case EXECUTE: /* SYS6 (6) */
            /* The shell is asking to spawn a child[cite: 270]. 
             * a1 contains the ASID.
             * Block the shell with a P operation on shellSemaphore[cite: 272].
             */
            break;

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
     * If this is ASID 1 (the shell), we V the masterSemaphore to wake up `test`[cite: 245, 299].
     * If this is ASID > 1 (a child program), we V the shellSemaphore to wake up the shell[cite: 245, 304].
     */
    if (supStruct->sup_asid == 1) {
        SYSCALL(VERHOGEN, (unsigned int)&masterSemaphore, 0, 0);
    } else {
        SYSCALL(VERHOGEN, (unsigned int)&shellSemaphore, 0, 0);
    }

    /* 3. Put it out of its misery via the Nucleus NSYS2 service[cite: 282, 300]. */
    SYSCALL(TERMPROCESS, 0, 0, 0); 
    
    /* We never reach this point. */
}