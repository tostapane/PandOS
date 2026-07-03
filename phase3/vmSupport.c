#include "headers/vmSupport.h"
#include "../headers/const.h"
#include "headers/initProc.h"
#include "headers/sysSupport.h"
#include <uriscv/cpu.h>
#include <uriscv/liburiscv.h>

/* Module-local data structures */
static int fifo_ptr; /* For the FIFO page replacement algorithm */

// indirizzo inizio swapPool
// calcolato in funzione della dimensione del file
// .core (il quale contiene il SO)
memaddr swapPoolBase;

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

  // Seguendo le specifiche della Phase 3, invece di cercare di indovinare
  // la dimensione del kernel leggendo un header che non è in memoria,
  // sovrastimiamo la grandezza del kernel a 32 frame (0x20000 byte).
  // Quindi lo Swap Pool inizia a 0x20020000.
  swapPoolBase = 0x20020000;
}

void Pager() {
  support_t *supStruct = (support_t *)SYSCALL(GETSUPPORTPTR, 0, 0, 0);

  unsigned int cause = supStruct->sup_exceptState[0].cause;
  unsigned int excCode = (cause & CAUSE_EXCCODE_MASK);

  if (excCode == EXC_TLBMOD) {
    ProgramTrapHandler(supStruct);
    return;
  }

  SYSCALL(PASSEREN, (unsigned int)&swapSemaphore, 0, 0);
  page_mutex_holder = supStruct->sup_asid;

  unsigned int missingPageEntryHi = supStruct->sup_exceptState[0].entry_hi;
  unsigned int missingPageNumber =
      (missingPageEntryHi & 0xFFFFF000) >> VPNSHIFT;

  int pageIndex = -1;
  if (missingPageNumber == 0xBFFFF) {
    pageIndex = MAXPAGES - 1; // pagina di stack (0xBFFFF000)
  } else if (missingPageNumber >= 0x80000 &&
             missingPageNumber < 0x80000 + (MAXPAGES - 1)) {
    pageIndex = missingPageNumber - 0x80000; // pagine text/data
  } else {
    // indirizzo fuori dallo spazio logico del U-proc -> program trap
    page_mutex_holder = -1;
    SYSCALL(VERHOGEN, (unsigned int)&swapSemaphore, 0, 0);
    ProgramTrapHandler(supStruct);
    return;
  }

  int frameIndex = -1;
  for (int i = 0; i < POOLSIZE; i++) {
    if (swapPool[i].sw_asid == -1) {
      frameIndex = i;
      break;
    }
  }

  if (frameIndex == -1) {
    frameIndex = fifo_ptr;
    fifo_ptr = (fifo_ptr + 1) % POOLSIZE;
  }

  memaddr frameAddr = swapPoolBase + (frameIndex * PAGESIZE);

  if (swapPool[frameIndex].sw_asid != -1) {
    setSTATUS(getSTATUS() & (~MSTATUS_MIE_MASK));

    swapPool[frameIndex].sw_pte->pte_entryLO &= ~VALIDON;

    setENTRYHI(swapPool[frameIndex].sw_pte->pte_entryHI);
    TLBP();

    if ((getINDEX() & PRESENTFLAG) == 0) {
      setENTRYLO(swapPool[frameIndex].sw_pte->pte_entryLO);
      TLBWI();
    }

    setSTATUS(getSTATUS() | MSTATUS_MIE_MASK);

    unsigned int oldAsid = swapPool[frameIndex].sw_asid;
    memaddr oldDevRegBase = START_DEVREG +
                            ((INTLINE_FLASH - INTLINE_DISK) * 0x80) +
                            ((oldAsid - 1) * 0x10);
    volatile dtpreg_t *flashDev = (dtpreg_t *)oldDevRegBase;

    // mutua esclusione
    int oldFlashSem = 8 + (oldAsid - 1);
    SYSCALL(PASSEREN, (unsigned int)&devSemaphores[oldFlashSem], 0, 0);

    flashDev->data0 = frameAddr;

    unsigned int oldPageNo = swapPool[frameIndex].sw_pageNo;
    unsigned int flashCmd = (oldPageNo << 8) | FLASHWRITE;

    int iostatus =
        SYSCALL(DOIO, (unsigned int)&(flashDev->command), flashCmd, 0);

    // rilacio mutua esclusione
    SYSCALL(VERHOGEN, (unsigned int)&devSemaphores[oldFlashSem], 0, 0);
    // In PandOS OK is 1 for READY
    if (iostatus != 1) {
      page_mutex_holder = -1;
      SYSCALL(VERHOGEN, (unsigned int)&swapSemaphore, 0, 0);
      ProgramTrapHandler(supStruct);
      return;
    }
  }

  memaddr devRegBase = START_DEVREG + ((INTLINE_FLASH - INTLINE_DISK) * 0x80) +
                       ((supStruct->sup_asid - 1) * 0x10);

  int flashSem = 8 + (supStruct->sup_asid - 1); // NUOVO, dopo r.121
  SYSCALL(PASSEREN, (unsigned int)&devSemaphores[flashSem], 0, 0);

  volatile dtpreg_t *flashDev = (dtpreg_t *)devRegBase;

  flashDev->data0 = frameAddr;
  unsigned int flashCmd = (pageIndex << 8) | FLASHREAD;

  int iostatus = SYSCALL(DOIO, (unsigned int)&(flashDev->command), flashCmd, 0);
  SYSCALL(VERHOGEN, (unsigned int)&devSemaphores[flashSem], 0, 0);
  if (iostatus != 1) {
    page_mutex_holder = -1;
    SYSCALL(VERHOGEN, (unsigned int)&swapSemaphore, 0, 0);
    ProgramTrapHandler(supStruct);
    return;
  }

  swapPool[frameIndex].sw_asid = supStruct->sup_asid;
  swapPool[frameIndex].sw_pageNo = pageIndex;
  swapPool[frameIndex].sw_pte = &supStruct->sup_privatePgTbl[pageIndex];

  setSTATUS(getSTATUS() & (~MSTATUS_MIE_MASK));

  supStruct->sup_privatePgTbl[pageIndex].pte_entryLO =
      (supStruct->sup_privatePgTbl[pageIndex].pte_entryLO & DIRTYON) |
      frameAddr | VALIDON;

  setENTRYHI(supStruct->sup_privatePgTbl[pageIndex].pte_entryHI);
  TLBP();

  if ((getINDEX() & PRESENTFLAG) == 0) {
    setENTRYLO(supStruct->sup_privatePgTbl[pageIndex].pte_entryLO);
    TLBWI();
  }

  setSTATUS(getSTATUS() | MSTATUS_MIE_MASK);

  page_mutex_holder = -1;
  SYSCALL(VERHOGEN, (unsigned int)&swapSemaphore, 0, 0);

  LDST(&(supStruct->sup_exceptState[0]));
}
