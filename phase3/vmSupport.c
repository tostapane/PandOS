#include "headers/vmSupport.h"
#include "../headers/const.h"
#include "headers/initProc.h"
#include "headers/sysSupport.h"
#include <uriscv/cpu.h>
#include <uriscv/liburiscv.h>

/**
 * Gestione della Memoria Virtuale e Page Replacement.
 * Questo modulo implementa l'algoritmo FIFO per la gestione della Swap Pool,
 * gestendo i page fault e il caricamento/scaricamento delle pagine dai
 * dispositivi flash.
 */

/* Puntatore circolare per l'algoritmo di rimpiazzo FIFO */
static int fifo_ptr;

/* Indirizzo fisico di base della Swap Pool in RAM */
memaddr swapPoolBase;

/* Semaforo esterno definito in initProc.c per la protezione della Swap Pool */
extern int swapSemaphore;

/**
 * Inizializza le strutture dati per la gestione della Swap Pool.
 * Configura tutti i frame come liberi e imposta l'indirizzo di base
 * sovrastimando il kernel.
 */
void initSwapStructs() {
  for (int i = 0; i < POOLSIZE; i++) {
    swapPool[i].sw_asid = -1; /* -1 indica che il frame e' libero */
    swapPool[i].sw_pageNo = -1;
    swapPool[i].sw_pte = NULL;
  }
  fifo_ptr = 0;

  /* Lo Swap Pool inizia dopo lo spazio riservato al kernel (32 frame) */
  swapPoolBase = 0x20020000;
}

/**
 * Handler per i Page Fault (TLB Invalidation/Miss).
 * Carica le pagine mancanti dai dispositivi Flash nella Swap Pool, applicando
 * il rimpiazzo FIFO se necessario.
 */
void Pager() {
  support_t *supStruct = (support_t *)SYSCALL(GETSUPPORTPTR, 0, 0, 0);

  unsigned int cause = supStruct->sup_exceptState[0].cause;
  unsigned int excCode = (cause & CAUSE_EXCCODE_MASK);

  /* Se l'eccezione e' un tentativo di modifica su una pagina read-only, il
   * processo viene terminato */
  if (excCode == EXC_TLBMOD) {
    ProgramTrapHandler(supStruct);
    return;
  }

  /* Acquisizione della mutua esclusione sulla Swap Pool */
  SYSCALL(PASSEREN, (unsigned int)&swapSemaphore, 0, 0);
  page_mutex_holder = supStruct->sup_asid;

  /* Identificazione della pagina virtuale che ha causato il fault */
  unsigned int missingPageEntryHi = supStruct->sup_exceptState[0].entry_hi;
  unsigned int missingPageNumber =
      (missingPageEntryHi & 0xFFFFF000) >> VPNSHIFT;

  /* Calcolo dell'indice della pagina all'interno della Page Table privata */
  int pageIndex = -1;
  if (missingPageNumber == 0xBFFFF || missingPageNumber == 0x3FFFF) {
    pageIndex = MAXPAGES - 1; // Pagina Stack
  } else if (missingPageNumber >= 0x80000 &&
             missingPageNumber < 0x80000 + (MAXPAGES - 1)) {
    pageIndex = missingPageNumber - 0x80000; // Pagine Text/Data
  } else if (missingPageNumber < (MAXPAGES - 1)) {
    pageIndex = missingPageNumber;
  } else {
    /* Indirizzo fuori dai limiti consentiti */
    page_mutex_holder = -1;
    SYSCALL(VERHOGEN, (unsigned int)&swapSemaphore, 0, 0);
    ProgramTrapHandler(supStruct);
    return;
  }

  /* Ricerca di un frame libero nella Swap Pool */
  int frameIndex = -1;
  for (int i = 0; i < POOLSIZE; i++) {
    if (swapPool[i].sw_asid == -1) {
      frameIndex = i;
      break;
    }
  }

  /* Se non ci sono frame liberi, applica l'algoritmo FIFO per sceglierne uno da
   * rimpiazzare */
  if (frameIndex == -1) {
    frameIndex = fifo_ptr;
    fifo_ptr = (fifo_ptr + 1) % POOLSIZE;
  }

  memaddr frameAddr = swapPoolBase + (frameIndex * PAGESIZE);

  /* Se il frame scelto era occupato, deve essere scaricato (se necessario) */
  if (swapPool[frameIndex].sw_asid != -1) {
    /* Disabilitazione atomica degli interrupt durante la manipolazione del TLB
     */
    setSTATUS(getSTATUS() & (~MSTATUS_MIE_MASK));

    /* Marcatura della vecchia pagina come non valida */
    swapPool[frameIndex].sw_pte->pte_entryLO &= ~VALIDON;

    /* Aggiornamento del TLB hardware per riflettere l'invalidazione della
     * pagina rimpiazzata */
    setENTRYHI(swapPool[frameIndex].sw_pte->pte_entryHI);
    TLBP();
    if ((getINDEX() & PRESENTFLAG) == 0) {
      setENTRYLO(swapPool[frameIndex].sw_pte->pte_entryLO);
      TLBWI();
    }

    setSTATUS(getSTATUS() | MSTATUS_MIE_MASK);

    /* Scrittura della pagina rimpiazzata sul backing store (Flash) */
    unsigned int oldAsid = swapPool[frameIndex].sw_asid;
    memaddr oldDevRegBase = START_DEVREG +
                            ((INTLINE_FLASH - INTLINE_DISK) * 0x80) +
                            ((oldAsid - 1) * 0x10);
    volatile dtpreg_t *flashDev = (dtpreg_t *)oldDevRegBase;

    flashDev->data0 = frameAddr;
    unsigned int oldPageNo = swapPool[frameIndex].sw_pageNo;
    unsigned int flashCmd = (oldPageNo << 8) | FLASHWRITE;

    int iostatus =
        SYSCALL(DOIO, (unsigned int)&(flashDev->command), flashCmd, 0);

    if (iostatus != 1) {
      page_mutex_holder = -1;
      SYSCALL(VERHOGEN, (unsigned int)&swapSemaphore, 0, 0);
      ProgramTrapHandler(supStruct);
      return;
    }
  }

  /* Lettura della pagina richiesta dal dispositivo Flash */
  memaddr devRegBase = START_DEVREG + ((INTLINE_FLASH - INTLINE_DISK) * 0x80) +
                       ((supStruct->sup_asid - 1) * 0x10);
  volatile dtpreg_t *flashDev = (dtpreg_t *)devRegBase;

  flashDev->data0 = frameAddr;
  unsigned int flashCmd = (pageIndex << 8) | FLASHREAD;

  int iostatus = SYSCALL(DOIO, (unsigned int)&(flashDev->command), flashCmd, 0);

  if (iostatus != 1) {
    page_mutex_holder = -1;
    SYSCALL(VERHOGEN, (unsigned int)&swapSemaphore, 0, 0);
    ProgramTrapHandler(supStruct);
    return;
  }

  /* Aggiornamento delle strutture dati della Swap Pool con le informazioni
   * della nuova pagina */
  swapPool[frameIndex].sw_asid = supStruct->sup_asid;
  swapPool[frameIndex].sw_pageNo = pageIndex;
  swapPool[frameIndex].sw_pte = &supStruct->sup_privatePgTbl[pageIndex];

  setSTATUS(getSTATUS() & (~MSTATUS_MIE_MASK));

  /* Configurazione della entry nella Page Table con l'indirizzo fisico del
   * frame e bit VALID attivo */
  supStruct->sup_privatePgTbl[pageIndex].pte_entryLO =
      (supStruct->sup_privatePgTbl[pageIndex].pte_entryLO & DIRTYON) |
      frameAddr | VALIDON;

  /* Caricamento immediato della nuova entry nel TLB hardware */
  setENTRYHI(supStruct->sup_privatePgTbl[pageIndex].pte_entryHI);
  TLBP();
  if ((getINDEX() & PRESENTFLAG) == 0) {
    setENTRYLO(supStruct->sup_privatePgTbl[pageIndex].pte_entryLO);
    TLBWI();
  }

  setSTATUS(getSTATUS() | MSTATUS_MIE_MASK);

  /* Rilascio della mutua esclusione e ritorno al processo che ha causato il
   * fault */
  page_mutex_holder = -1;
  SYSCALL(VERHOGEN, (unsigned int)&swapSemaphore, 0, 0);

  LDST(&(supStruct->sup_exceptState[0]));
}