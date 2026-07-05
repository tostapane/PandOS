#include "headers/vmSupport.h"
#include "../headers/const.h"
#include "headers/initProc.h"
#include "headers/sysSupport.h"
#include <uriscv/aout.h>
#include <uriscv/cpu.h>
#include <uriscv/liburiscv.h>

/* Sezione 5.4: Puntatore circolare per l'algoritmo di rimpiazzamento FIFO.
 * Ad ogni page fault che richiede lo sfratto di un frame, questo indice
 * viene incrementato modulo POOLSIZE, implementando un round-robin tra
 * i frame della Swap Pool. */
static int fifo_ptr;

/* Sezione 4.1/10.5: Indirizzo fisico di inizio della Swap Pool in RAM.
 * Calcolato dinamicamente in initSwapStructs() a partire dall'header a.out
 * del kernel, posizionando la Swap Pool immediatamente dopo le sezioni
 * .text e .data del sistema operativo (evitando di sovrastimare la
 * dimensione del kernel a un numero fisso di frame). */
memaddr swapPoolBase;

/* Semaforo di mutua esclusione per la Swap Pool, definito in initProc.c */
extern int swapSemaphore;

/**
 * SEZIONE 4.1: Inizializzazione della Swap Pool
 *
 * @brief Inizializza la tabella della Swap Pool e calcola l'indirizzo
 *        base della Swap Pool in RAM.
 *
 * Marca tutti i frame come liberi (sw_asid = -1) e azzera il puntatore
 * FIFO. Tramite l'ottimizzazione 10.5, calcola dinamicamente l'indirizzo
 * base della Swap Pool leggendo l'header a.out del kernel per posizionarla
 * subito dopo il codice del sistema operativo.
 */
void initSwapStructs() {
  /* Inizializza ogni entry della Swap Pool come libera: sw_asid = -1 indica
   * che il frame corrispondente non è occupato da alcun U-proc. */
  for (int i = 0; i < POOLSIZE; i++) {
    swapPool[i].sw_asid = -1;
    swapPool[i].sw_pageNo = -1;
    swapPool[i].sw_pte = NULL;
  }

  // Azzera l'indice FIFO per l'algoritmo di rimpiazzamento
  fifo_ptr = 0;

  /* Sezione 4.1: Seguendo le specifiche della Phase 3,
   * sovrastimiamo la grandezza del kernel a 32 frame (0x20000 byte).
   * Quindi lo Swap Pool inizierebbe a 0x20020000.
   * swapPoolBase = 0x20020000; */

  /* Sezione 10.5 (ottimizzazione): Invece di sovrastimare, leggiamo
   * l'header a.out del kernel (caricato dal file .core a RAMSTART+PAGESIZE)
   * per situare la Swap Pool subito dopo le sezioni .text e .data,
   * evitando di sprecare memoria. */
  memaddr *hdr =
      (memaddr *)(RAMSTART + PAGESIZE); // la prima pagina 'PAGESIZE' è
                                        // riservata al BIOS/kernel stack

  /* Calcoliamo la fine della sezione .data: indirizzo virtuale di caricamento
   * dei dati + la loro dimensione in memoria = fine del codice del SO */
  memaddr dataEnd =
      hdr[AOUT_HE_DATA_VADDR] +
      hdr[AOUT_HE_DATA_MEMSZ];

  /* Se la dimensione su file eccede quella in memoria (caso raro),
   * usiamo il valore più grande per sicurezza */
  if (hdr[AOUT_HE_DATA_FILESZ] > hdr[AOUT_HE_DATA_MEMSZ])
    dataEnd = hdr[AOUT_HE_DATA_VADDR] + hdr[AOUT_HE_DATA_FILESZ];

  /* Allinea l'indirizzo base della Swap Pool al prossimo confine di pagina
   * (PAGESIZE) tramite arrotondamento per eccesso con maschera di bit */
  swapPoolBase = (dataEnd + PAGESIZE - 1) & ~(PAGESIZE - 1);
}

/**
 * SEZIONE 4.2: Il Pager (Gestore delle eccezioni TLB)
 *
 * @brief Gestore di page fault - implementa la paginazione su richiesta
 *        (demand paging) per gli U-proc.
 *
 * Questa funzione viene invocata quando un U-proc tenta di accedere a una
 * pagina di memoria virtuale che non è attualmente presente in RAM (TLB
 * miss non risolvibile dal TLB-Refill handler perché la pagina non è valida).
 *
 * L'algoritmo del Pager:
 * 1. Verifica se l'eccezione è una TLB-Modification (scrittura su pagina
 *    di sola lettura) che causa un errore
 * 2. Acquisisce mutua esclusione sulla Swap Pool
 * 3. Identifica la pagina mancante
 * 4. Seleziona un frame (libero o da sfrattare con FIFO)
 * 5. Se necessario, sfratta la pagina vittima scrivendola sul flash
 * 6. Carica la pagina richiesta dal flash al frame selezionato
 * 7. Aggiorna la Swap Pool, la Page Table e la TLB
 * 8. Rilascia il mutex e riprende l'esecuzione dell'U-proc
 */
void Pager() {
  /* Ottiene la Support Structure dell'U-proc corrente tramite NSYS8
   * per accedere alla sua Page Table e allo stato d'eccezione salvato */
  support_t *supStruct = (support_t *)SYSCALL(GETSUPPORTPTR, 0, 0, 0);

  /* Estrae il codice d'eccezione dal registro cause dello stato salvato
   * nel contesto TLB (PGFAULTEXCEPT = indice 0) */
  unsigned int cause = supStruct->sup_exceptState[0].cause;
  unsigned int excCode = (cause & CAUSE_EXCCODE_MASK);

  /* Sezione 4.2, punto 1: Se l'eccezione è una TLB-Modification (excCode 24),
   * significa che l'U-proc ha tentato di scrivere su una pagina di sola
   * lettura (sezione .text). Questo è un errore irrecuperabile. */
  if (excCode == EXC_TLBMOD) {
    ProgramTrapHandler(supStruct);
    return;
  }

  /* Sezione 4.2, punto 2: Acquisisce la mutua esclusione sulla Swap Pool.
   * Nessun altro U-proc può modificare la Swap Pool o le Page Table
   * contemporaneamente. Registra l'ASID in page_mutex_holder per evitare
   * un self-deadlock nel ProgramTrapHandler. */
  SYSCALL(PASSEREN, (unsigned int)&swapSemaphore, 0, 0);
  page_mutex_holder = supStruct->sup_asid;

  /* Sezione 4.2, punto 3: Determina quale pagina logica è mancante.
   * Estrae il VPN (Virtual Page Number) dal registro EntryHI dello stato
   * salvato al momento dell'eccezione TLB. */
  unsigned int missingPageEntryHi = supStruct->sup_exceptState[0].entry_hi;
  unsigned int missingPageNumber =
      (missingPageEntryHi & 0xFFFFF000) >> VPNSHIFT;

  /* Converte il VPN in un indice nella Page Table dell'U-proc:
   * - VPN 0xBFFFF - pagina di stack (ultimo indice, MAXPAGES-1)
   * - VPN 0x80000..0x8001E - pagine text/data (indici 0..30) */
  int pageIndex = -1;
  if (missingPageNumber == 0xBFFFF) {
    pageIndex = MAXPAGES - 1; // pagina di stack (0xBFFFF000)
  } else if (missingPageNumber >= 0x80000 &&
             missingPageNumber < 0x80000 + (MAXPAGES - 1)) {
    pageIndex = missingPageNumber - 0x80000; // pagine text/data
  } else {
    /* Indirizzo fuori dallo spazio logico dell'U-proc: errore.
     * Rilascia il mutex prima di terminare. */
    page_mutex_holder = -1;
    SYSCALL(VERHOGEN, (unsigned int)&swapSemaphore, 0, 0);
    ProgramTrapHandler(supStruct);
    return;
  }

  /* Sezione 10.3 (ottimizzazione): Selezione del frame.
   * Prima cerca un frame libero nella Swap Pool (sw_asid == -1).
   * Questo trasforma un'operazione O(1) in O(n) ma evita sfratti
   * inutili e le relative operazioni di I/O sul flash. */
  int frameIndex = -1;
  for (int i = 0; i < POOLSIZE; i++) {
    if (swapPool[i].sw_asid == -1) {
      frameIndex = i;
      break;
    }
  }

  /* Sezione 5.4: Se nessun frame è libero, utilizza l'algoritmo FIFO.
   * Il puntatore circolare fifo_ptr seleziona la vittima e viene
   * incrementato modulo POOLSIZE per il prossimo page fault. */
  if (frameIndex == -1) {
    frameIndex = fifo_ptr;
    fifo_ptr = (fifo_ptr + 1) % POOLSIZE;
  }

  /* Calcola l'indirizzo fisico del frame selezionato nella Swap Pool:
   * base della Swap Pool + (indice * dimensione pagina) */
  memaddr frameAddr = swapPoolBase + (frameIndex * PAGESIZE);

  /* Sfratto della pagina vittima (se il frame è occupato) */
  if (swapPool[frameIndex].sw_asid != -1) {

    /* Sezione 5.3: L'aggiornamento della Page Table e della TLB deve
     * avvenire atomicamente. Disabilitiamo gli interrupt tramite il
     * bit MIE del registro STATUS per evitare che un context switch
     * lasci la TLB in uno stato inconsistente. */
    setSTATUS(getSTATUS() & (~MSTATUS_MIE_MASK));

    /* Invalida l'entry nella Page Table della vittima: azzera il bit
     * Valid per indicare che la pagina non è più presente in RAM */
    swapPool[frameIndex].sw_pte->pte_entryLO &= ~VALIDON;

    /* Sezione 5.2/10.1 (ottimizzazione): Aggiornamento selettivo della TLB.
     * Invece di invalidare l'intera TLB (TLBCLR), cerchiamo se l'entry
     * della vittima è cachata nella TLB tramite TLBP. Se presente
     * (INDEX.P == 0, ovvero PRESENTFLAG non settato), la aggiorniamo
     * con TLBWI. Questo è più efficiente del TLBCLR. */
    setENTRYHI(swapPool[frameIndex].sw_pte->pte_entryHI);
    TLBP();

    if ((getINDEX() & PRESENTFLAG) == 0) {
      setENTRYLO(swapPool[frameIndex].sw_pte->pte_entryLO);
      TLBWI();
    }

    // Riabilita gli interrupt dopo l'aggiornamento atomico
    setSTATUS(getSTATUS() | MSTATUS_MIE_MASK);

    /* Sezione 4.2: Scrive la pagina vittima sul dispositivo flash del
     * suo proprietario (backing store) prima di sovrascrivere il frame.
     * Calcola l'indirizzo del registro del flash device associato
     * all'ASID della vittima. */
    unsigned int oldAsid = swapPool[frameIndex].sw_asid;
    memaddr oldDevRegBase = START_DEVREG +
                            ((INTLINE_FLASH - INTLINE_DISK) * 0x80) +
                            ((oldAsid - 1) * 0x10);
    volatile dtpreg_t *flashDev = (dtpreg_t *)oldDevRegBase;

    /* Mutua esclusione sul dispositivo flash della vittima tramite
     * il semaforo dedicato (indice 8 + (asid-1) nell'array devSemaphores) */
    int oldFlashSem = 8 + (oldAsid - 1);
    SYSCALL(PASSEREN, (unsigned int)&devSemaphores[oldFlashSem], 0, 0);

    // Imposta l'indirizzo sorgente da cui leggere i dati da scrivere sul flash
    flashDev->data0 = frameAddr;

    /* Compone il comando flash: il numero di blocco (pagina logica) nei
     * 3 byte alti e il comando FLASHWRITE nel byte basso */
    unsigned int oldPageNo = swapPool[frameIndex].sw_pageNo;
    unsigned int flashCmd = (oldPageNo << 8) | FLASHWRITE;

    // Esegue l'operazione di I/O tramite DOIO (NSYS5)
    int iostatus =
        SYSCALL(DOIO, (unsigned int)&(flashDev->command), flashCmd, 0);

    // Rilascia la mutua esclusione sul flash della vittima
    SYSCALL(VERHOGEN, (unsigned int)&devSemaphores[oldFlashSem], 0, 0);

    /* In PandOS il valore 1 (READY) indica successo dell'operazione I/O.
     * Un errore durante la scrittura è fatale: rilascia il mutex della
     * Swap Pool e termina l'U-proc. */
    if (iostatus != 1) {
      page_mutex_holder = -1;
      SYSCALL(VERHOGEN, (unsigned int)&swapSemaphore, 0, 0);
      ProgramTrapHandler(supStruct);
      return;
    }
  }

  /* Sezione 5.3: L'operazione di lettura deve avvenire PRIMA di
   * aggiornare la Page Table e la TLB (ordine delle operazioni
   * prescritto dalle specifiche). */

  /* Calcola l'indirizzo del registro del flash device associato
   * all'ASID dell'U-proc corrente */
  memaddr devRegBase = START_DEVREG + ((INTLINE_FLASH - INTLINE_DISK) * 0x80) +
                       ((supStruct->sup_asid - 1) * 0x10);

  /* Mutua esclusione sul flash device dell'U-proc corrente */
  int flashSem = 8 + (supStruct->sup_asid - 1);
  SYSCALL(PASSEREN, (unsigned int)&devSemaphores[flashSem], 0, 0);

  volatile dtpreg_t *flashDev = (dtpreg_t *)devRegBase;

  // Imposta la destinazione in RAM dove scrivere i dati letti dal flash
  flashDev->data0 = frameAddr;

  /* Compone il comando flash: numero del blocco (pageIndex) nei 3 byte
   * alti e FLASHREAD nel byte basso */
  unsigned int flashCmd = (pageIndex << 8) | FLASHREAD;

  // Esegue la lettura dal flash tramite DOIO (NSYS5)
  int iostatus = SYSCALL(DOIO, (unsigned int)&(flashDev->command), flashCmd, 0);

  // Rilascia la mutua esclusione sul flash dell'U-proc corrente
  SYSCALL(VERHOGEN, (unsigned int)&devSemaphores[flashSem], 0, 0);

  /* Errore di lettura dal flash: rilascia il mutex e termina */
  if (iostatus != 1) {
    page_mutex_holder = -1;
    SYSCALL(VERHOGEN, (unsigned int)&swapSemaphore, 0, 0);
    ProgramTrapHandler(supStruct);
    return;
  }

  /* Registra il nuovo proprietario del frame, la pagina logica e
   * il puntatore alla corrispondente entry nella Page Table. */
  swapPool[frameIndex].sw_asid = supStruct->sup_asid;
  swapPool[frameIndex].sw_pageNo = pageIndex;
  swapPool[frameIndex].sw_pte = &supStruct->sup_privatePgTbl[pageIndex];

  /* Sezione 5.3: Dopo la lettura, aggiorniamo la Page Table marcando
   * la pagina come valida (VALIDON) con l'indirizzo fisico del frame.
   * L'aggiornamento deve essere atomico (interrupt disabilitati). */
  setSTATUS(getSTATUS() & (~MSTATUS_MIE_MASK));

  /* Imposta entryLO: preserva il bit DIRTY dall'inizializzazione della
   * page table, aggiunge l'indirizzo fisico del frame e setta il bit
   * VALID per indicare che la pagina è in RAM */
  supStruct->sup_privatePgTbl[pageIndex].pte_entryLO =
      (supStruct->sup_privatePgTbl[pageIndex].pte_entryLO & DIRTYON) |
      frameAddr | VALIDON;

  /* Sezione 5.2/10.1 (ottimizzazione): Aggiornamento selettivo della TLB.
   * Se l'entry appena aggiornata è già cachata nella TLB, la sovrascriviamo
   * con il nuovo valore per garantire la consistenza della cache. */
  setENTRYHI(supStruct->sup_privatePgTbl[pageIndex].pte_entryHI);
  TLBP();

  if ((getINDEX() & PRESENTFLAG) == 0) {
    setENTRYLO(supStruct->sup_privatePgTbl[pageIndex].pte_entryLO);
    TLBWI();
  }

  // Riabilita gli interrupt dopo l'aggiornamento atomico
  setSTATUS(getSTATUS() | MSTATUS_MIE_MASK);

  // Azzera page_mutex_holder e rilascia il semaforo della Swap Pool
  page_mutex_holder = -1;
  SYSCALL(VERHOGEN, (unsigned int)&swapSemaphore, 0, 0);

  /* Ricarica lo stato processore salvato al momento del page fault
   * per riprendere l'esecuzione dell'U-proc: l'istruzione che aveva
   * causato l'eccezione TLB verrà rieseguita con successo poiché
   * la pagina è ora presente in RAM e nella TLB. */
  LDST(&(supStruct->sup_exceptState[0]));
}
