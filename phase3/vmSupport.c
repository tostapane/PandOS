#include "vmSupport.h"
#include <uriscv/liburiscv.h>

/* * Module-local data structures. 
 * We keep these static so other modules can't touch them directly and ruin our day.
 */
static swap_t swapPool[POOLSIZE];
static int fifo_ptr; /* For the highly advanced FIFO page replacement algorithm */

/* Extern semaphore defined in initProc.c to protect this exact Swap Pool */
extern int swapSemaphore;

void initSwapStructs() {
    /* Initialize the swap pool to empty */
    for (int i = 0; i < POOLSIZE; i++) {
        swapPool[i].sw_asid = -1; /* -1 means this frame is wonderfully unoccupied */
        swapPool[i].sw_pageNo = -1;
        swapPool[i].sw_pte = NULL;
    }
    fifo_ptr = 0; /* Start the FIFO pointer at the beginning */
}

/* * The Pager
 * Handle page faults, swap out old pages, swap in new ones, try not to panic.
 */
void Pager() {
    /* 1. Get the current process's Support Structure via SYSCALL 8 */
    support_t *supStruct = (support_t *)SYSCALL(GETSUPPORTPTR, 0, 0, 0);

    /* 2. Determine the cause of the exception */
    unsigned int cause = supStruct->sup_exceptState[0].cause;
    unsigned int excCode = (cause & GETEXECCODE) >> CAUSESHIFT;

    /* 3. If it's a TLB-Modification exception, you're supposed to treat it as a Program Trap */
    if (excCode == 1) { /* 1 is usually Mod, double check your architecture docs */
        /* Pass control to your Program Trap handler here. 
           Do NOT continue paging. */
    }

    /* 4. Gain mutual exclusion over the Swap Pool table */
    SYSCALL(PASSEREN, (unsigned int)&swapSemaphore, 0, 0);

    /* 5. Determine the missing page number (p) from EntryHi */
    unsigned int missingPageEntryHi = supStruct->sup_exceptState[0].entry_hi;
    unsigned int missingPageNumber = (missingPageEntryHi & GETPAGENO) >> VPNSHIFT;

    /* 6. Pick a frame using your 'state-of-the-art' FIFO algorithm */
    int frameIndex = fifo_ptr;
    fifo_ptr = (fifo_ptr + 1) % POOLSIZE; /* Increment mod pool size */

    /* 7 & 8. Check if the chosen frame is occupied. */
    if (swapPool[frameIndex].sw_asid != -1) {
        /* Uh oh, it's occupied. You need to kick someone out.
         *
         * a) Mark the current owner's Page Table entry as invalid.
         * b) Atomically update the TLB using TLBCLR (or TLBP/TLBWI).
         * To do this atomically, you MUST disable interrupts via the STATUS register, 
         * do the update, and re-enable them. 
         * c) Write the contents of this frame back to the owner's backing store (flash device).
         */
         
         /* YOUR KICK-OUT LOGIC GOES HERE */
    }

    /* 9. Read the missing page from the Current Process's flash device into the frame. */
    /* * To find the actual RAM address to write to:
     * memaddr frameAddress = FRAMEPOOLSTART + (frameIndex * PAGESIZE);
     * Note: You'll need to calculate FRAMEPOOLSTART based on your OS size.
     */

    /* YOUR FLASH READ LOGIC GOES HERE */

    /* 10. Update the Swap Pool table entry to reflect its new, proud owner */
    swapPool[frameIndex].sw_asid = supStruct->sup_asid;
    swapPool[frameIndex].sw_pageNo = missingPageNumber;
    /* You'll need to figure out the exact index in sup_privatePgTbl for this pointer */
    // swapPool[frameIndex].sw_pte = &supStruct->sup_privatePgTbl[...]; 

    /* 11. Update the Current Process's Page Table entry.
     * Turn the Valid (V) bit ON, set the PFN field to the newly acquired frame.
     */

    /* 12. Atomically update the TLB (again, disable/enable interrupts).
     * If you don't do this, the processor will keep looking at stale cache data. 
     */

    /* 13. Release mutual exclusion over the Swap Pool table */
    SYSCALL(VERHOGEN, (unsigned int)&swapSemaphore, 0, 0);

    /* 14. Return control to the Current Process to retry the instruction */
    LDST(&(supStruct->sup_exceptState[0]));
}