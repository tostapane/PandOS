#include "headers/vmSupport.h"
#include "../headers/const.h"
#include "headers/sysSupport.h"
#include <uriscv/liburiscv.h>

/* Module-local data structures */
static swap_t swapPool[POOLSIZE];
static int fifo_ptr; /* For the FIFO page replacement algorithm */

/* Extern semaphore defined in initProc.c to protect this exact Swap Pool */
extern int swapSemaphore;

void initSwapStructs() {
  /* Initialize the swap pool to empty */
  for (int i = 0; i < POOLSIZE; i++) {
    swapPool[i].sw_asid = -1; /* -1 means this frame is unoccupied */
    swapPool[i].sw_pageNo = -1;
    swapPool[i].sw_pte = NULL;
  }
  fifo_ptr = 0;
}

void Pager() {
  /* 1. Get the current process's Support Structure via SYSCALL GETSUPPORTPTR */
  support_t *supStruct = (support_t *)SYSCALL(GETSUPPORTPTR, 0, 0, 0);

  /* 2. Determine the cause of the exception */
  unsigned int cause = supStruct->sup_exceptState[0].cause;
  unsigned int excCode = (cause & GETEXECCODE) >> CAUSESHIFT;

  /* 3. If it's a TLB-Modification exception, treat it as a Program Trap */
  if (excCode == EXC_TLBMOD) {
    ProgramTrapHandler(supStruct);
    return;
  }

  /* 4. Gain mutual exclusion over the Swap Pool table */
  SYSCALL(PASSEREN, (unsigned int)&swapSemaphore, 0, 0);

  /* 5. Determine the missing page number from EntryHi */
  unsigned int missingPageEntryHi = supStruct->sup_exceptState[0].entry_hi;
  unsigned int missingPageNumber = (missingPageEntryHi & GETPAGENO) >> VPNSHIFT;

  /* Determine the pageIndex (p) based on the VPN field */
  int pageIndex = -1;
  if (missingPageNumber == 0x3FFFF) {
    /* It is the stack page, map it to the last index (31) */
    pageIndex = MAXPAGES - 1;

  } else if (missingPageNumber < (MAXPAGES - 1)) {
    /* Normal page */
    pageIndex = missingPageNumber;

  } else {
    /* Bus error */
    SYSCALL(VERHOGEN, (unsigned int)&swapSemaphore, 0, 0);
    ProgramTrapHandler(supStruct);
    return;
  }

  /* 6. Pick a frame using a FIFO algorithm */
  int frameIndex = fifo_ptr;
  fifo_ptr = (fifo_ptr + 1) % POOLSIZE; /* Increment mod pool size */

  /* Calculate the frame address */
  memaddr swapPoolBase = RAMSTART + (OSFRAMES * PAGESIZE);
  memaddr frameAddr = swapPoolBase + (frameIndex * PAGESIZE);

  /* 7-8. Check if the chosen frame is occupied. */
  if (swapPool[frameIndex].sw_asid != -1) {
    /* Temporarily disable interrupts to ensure atomicity */
    setSTATUS(getSTATUS() & (~MSTATUS_MIE_MASK));

    /* Set the current page entry as invalid */
    swapPool[frameIndex].sw_pte->pte_entryLO &= ~VALIDON;

    /* Update the TLB. TODO: caching improvement*/
    //    TLBCLR();
    // Search the TLB for the exact page we just invalidated
    setENTRYHI(swapPool[frameIndex].sw_pte->pte_entryHI);
    TLBP();

    // If the PRESENTFLAG is 0, the page is in the TLB. Overwrite it.
    if ((getINDEX() & PRESENTFLAG) == 0) {
      setENTRYLO(swapPool[frameIndex].sw_pte->pte_entryLO);
      TLBWI();
    }

    /* Re-enable interrupts */
    setSTATUS(getSTATUS() | MSTATUS_MIE_MASK);

    /* Find the correct device register */
    unsigned int oldAsid = swapPool[frameIndex].sw_asid;
    memaddr oldDevRegBase = START_DEVREG +
                            ((INTLINE_FLASH - INTLINE_DISK) * 0x80) +
                            ((oldAsid - 1) * 0x10);
    volatile dtpreg_t *flashDev = (dtpreg_t *)oldDevRegBase;

    /* Tell the device the current data location */
    flashDev->data0 = frameAddr;

    /* Tell the device where to put the data */
    unsigned int oldPageNo = swapPool[frameIndex].sw_pageNo;
    unsigned int flashCmd = (oldPageNo << 8) | FLASHWRITE;

    /* Execute the IO operation */
    int iostatus =
        SYSCALL(DOIO, (unsigned int)&(flashDev->command), flashCmd, 0);

    /* If IO fails, treat it as program trap*/
    if (iostatus != 1) {
      SYSCALL(VERHOGEN, (unsigned int)&swapSemaphore, 0, 0);
      ProgramTrapHandler(supStruct);
      return;
    }
  }

  /* 9. Read the missing page from the Current Process's flash device into the
   * frame. */
  /* Find the correct device register */
  memaddr devRegBase = START_DEVREG + ((INTLINE_FLASH - INTLINE_DISK) * 0x80) +
                       ((supStruct->sup_asid - 1) * 0x10);
  volatile dtpreg_t *flashDev = (dtpreg_t *)devRegBase;

  /* Tell the device where to write the new data */
  flashDev->data0 = frameAddr;
  unsigned int flashCmd = (pageIndex << 8) | FLASHREAD;

  /* Execute the IO operation */
  int iostatus = SYSCALL(DOIO, (unsigned int)&(flashDev->command), flashCmd, 0);

  /* If IO fails, treat it as program trap*/
  if (iostatus != 1) {
    SYSCALL(VERHOGEN, (unsigned int)&swapSemaphore, 0, 0);
    ProgramTrapHandler(supStruct);
    return;
  }

  /* 10. Update the Swap Pool table entry to reflect to reflect the new data */
  swapPool[frameIndex].sw_asid = supStruct->sup_asid;
  swapPool[frameIndex].sw_pageNo = pageIndex;
  swapPool[frameIndex].sw_pte = &supStruct->sup_privatePgTbl[pageIndex];

  /* 11. Update the Current Process's Page Table entry. */
  /* Temporarily disable interrupts to ensure atomicity */
  setSTATUS(getSTATUS() & (~MSTATUS_MIE_MASK));

  /* Turn the Valid and Dirty bit ON, set the PFN field to the newly acquired
   * frame.*/
  supStruct->sup_privatePgTbl[pageIndex].pte_entryLO =
      frameAddr | VALIDON | DIRTYON;

  /* 12. Atomically update the TLB */
  /* Update the TLB. TODO: caching improvement*/
  // TLBCLR();
  // Search the TLB for the page we just brought in
  setENTRYHI(supStruct->sup_privatePgTbl[pageIndex].pte_entryHI);
  TLBP();

  // If the old invalid entry is still cached, update it to the valid one
  if ((getINDEX() & PRESENTFLAG) == 0) {
    setENTRYLO(supStruct->sup_privatePgTbl[pageIndex].pte_entryLO);
    TLBWI();
  }

  /* Re-enable interrupts */
  setSTATUS(getSTATUS() | MSTATUS_MIE_MASK);

  /* 13. Release mutual exclusion over the Swap Pool table */
  SYSCALL(VERHOGEN, (unsigned int)&swapSemaphore, 0, 0);

  /* 14. Return control to the Current Process to retry the instruction */
  LDST(&(supStruct->sup_exceptState[0]));
}
