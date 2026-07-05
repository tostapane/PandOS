#include "headers/initProc.h"
#include "../headers/const.h"
#include "headers/vmSupport.h"
#include <uriscv/aout.h>
#include <uriscv/liburiscv.h>
#include <uriscv/types.h>

/* Sezione 4.1: Swap Pool - tabella che traccia il contenuto di ogni frame
 * RAM riservato alla memoria virtuale. Ogni entry registra l'ASID del
 * processo proprietario, il numero di pagina logica e un puntatore alla
 * corrispondente entry nella Page Table. Un valore di -1 in sw_asid indica
 * che il frame è libero. La dimensione è 2 * UPROCMAX. */
swap_t swapPool[POOLSIZE];

/* Sezione 4.1: Semaforo binario (mutex) per la mutua esclusione sulla
 * Swap Pool. Inizializzato a 1 poiché viene usato per mutua esclusione
 * (non per sincronizzazione). */
int swapSemaphore;

/* Sezione 9: Semaforo su cui l'Instantiator Process si blocca dopo
 * aver lanciato la shell. Quando la shell termina, esegue una V su
 * questo semaforo sbloccando l'Instantiator che provvederà allo shutdown
 * del sistema. Inizializzato a 0 (sincronizzazione). */
int masterSemaphore;

/* Lista delle support struct libere, gestita come una pila/stack
 * seguendo il pattern di allocazione/deallocazione della Phase 1
 * (Sezione 10, ottimizzazione gestione Support Structure). */
LIST_HEAD(supStructs_freeList);

/* Sezione 5.3: Contiene l'ASID del processo che attualmente detiene
 * la mutua esclusione sulla Swap Pool (swapSemaphore). Utilizzato
 * per evitare un self-deadlock nel ProgramTrapHandler quando il Pager
 * lo invoca dopo aver già acquisito il mutex. Valore -1 = nessuno. */
int page_mutex_holder;

/* Sezione 9/7.4: Gestisce la concorrenza tra la shell e un suo processo
 * figlio. La shell si blocca su questo semaforo dopo aver lanciato un
 * figlio tramite EXECUTE (SYS6), e verrà sbloccata quando il figlio
 * termina eseguendo una V. Inizializzato a 0 (sincronizzazione). */
int shellSemaphore;

/* Sezione 9: Array di semafori binari (mutex, inizializzati a 1) per
 * la mutua esclusione sull'accesso ai dispositivi periferici di I/O:
 * 8 disk + 8 flash + 8 network + 8 printer + 8 terminal TX + 8 terminal RX
 * = 48 semafori totali (NSUPPSEM). */
int devSemaphores[NSUPPSEM];

/* Sezione 9.1/10: Pool statico di strutture di supporto per gli U-proc.
 * Gestito tramite una free list (supStructs_freeList) con funzioni
 * allocateSupport() e deallocateSupport() seguendo il pattern di
 * allocazione/deallocazione della Phase 1. */
support_t supStructs[UPROCMAX];

/* Buffer statico per leggere l'header a.out della shell senza utilizzare
 * lo stack del kernel. Dichiarato 'static' affinché il compilatore lo
 * collochi nella sezione .data del kernel (allocata staticamente) anziché
 * sullo stack, che potrebbe non avere spazio sufficiente per un'intera
 * pagina. */
static unsigned int shellHeaderBuf[PAGESIZE / sizeof(unsigned int)];

/* Dichiarazioni esterne dei gestori di eccezioni del Livello di Supporto,
 * implementati rispettivamente in vmSupport.c e sysSupport.c */
extern void Pager();
extern void GeneralExceptionHandler();

/**
 * @brief Alloca una struttura di supporto dalla free list.
 *
 * Seguendo l'ottimizzazione descritta nella Sezione 10 delle specifiche,
 * le Support Structure vengono gestite tramite una free list (pila)
 * anziché accedendo direttamente ad un array statico. Riutilizza il
 * pattern list_head della Phase 1.
 *
 * @return Puntatore alla support_t allocata, oppure NULL se non ve ne
 *         sono di disponibili.
 */
support_t *allocateSupport() {
  if (list_empty(&supStructs_freeList))
    return NULL;
  // Estrae la prima struttura disponibile dalla testa della free list
  struct list_head *l = supStructs_freeList.next;
  list_del(l);
  return container_of(l, support_t, s_list);
}

/**
 * @brief Restituisce una struttura di supporto alla free list.
 *
 * Rende nuovamente disponibile una support_t reinserendola nella
 * free list. Viene invocata quando un U-proc termina per riciclare
 * la sua struttura (Sezione 10, ottimizzazione).
 *
 * @param s Puntatore alla struttura da deallocare.
 */
void deallocateSupport(support_t *s) {
  list_add(&(s->s_list), &supStructs_freeList);
}

/**
 * SEZIONE 9: Instantiator Process
 *
 * @brief Processo Istanziatore - punto di ingresso del Livello di Supporto.
 *
 * Questa funzione è il processo radice lanciato dal Nucleo (Phase 2).
 * Si occupa di inizializzare tutte le strutture dati del Livello di
 * Supporto, preparare e lanciare il processo shell (ASID 1), e infine
 * attendere la terminazione della shell per spegnere il sistema.
 */
void test() {
  // Sezione 4.1: Il semaforo è usato per mutua esclusione, quindi
  // inizializzato a 1 (un solo processo alla volta può accedere alla Swap Pool)
  swapSemaphore = 1;

  // Inizializza la tabella della Swap Pool: tutti i frame marcati come
  // liberi (sw_asid = -1) e calcola l'indirizzo base della Swap Pool
  initSwapStructs();

  // Sezione 9: Ogni semaforo di device è usato per mutua esclusione
  // sull'accesso alla periferica, quindi inizializzato a 1
  for (int i = 0; i < NSUPPSEM; i++) {
    devSemaphores[i] = 1;
  }

  // Sezione 9.1/10: Inizializza la lista e popola con tutte le strutture
  // dell'array statico, rendendole disponibili per l'allocazione.
  INIT_LIST_HEAD(&supStructs_freeList);
  for (int i = 0; i < UPROCMAX; i++) {
    deallocateSupport(&supStructs[i]);
  }

  // Inizializzati a 0 perché usati per sincronizzazione (non mutua esclusione):
  // l'Instantiator e la shell si bloccheranno su di essi in attesa di un evento
  masterSemaphore = 0;
  shellSemaphore = 0;

  // Nessun processo detiene attualmente la mutua esclusione sulla Swap Pool
  page_mutex_holder = -1;

  // Sezione 9/2.1: La shell è un U-proc con ASID 1 che esegue in modalità
  // utente con tutti gli interrupt abilitati.
  state_t shellState;

  // Program Counter: indirizzo iniziale dello spazio logico degli U-proc
  shellState.pc_epc = UPROCSTARTADDR; // 0x8000.00B0

  // Stack Pointer: cima dello stack utente (lo stack cresce verso il basso)
  shellState.reg_sp =
      USERSTACKTOP; // 0xC000.0000

  // Stato: modalità utente (MPP_U) con interrupt precedentemente abilitati
  // (MPIE), così che al ritorno da un'eccezione gli interrupt vengano
  // riattivati
  shellState.status = MSTATUS_MPIE_MASK | MSTATUS_MPP_U;

  // Tutti gli interrupt (incluso il PLT, bit MTIE) abilitati
  shellState.mie = MIE_ALL;

  // Sezione 2.1: L'ASID della shell è 1 (0 è riservato al kernel).
  // Il campo entry_hi contiene l'ASID shiftato nella posizione corretta.
  shellState.entry_hi = (1 << ASIDSHIFT);

  // Sezione 9.1: Alloca una struttura dal pool e configura i contesti
  // d'eccezione per il Pager (TLB) e il GeneralExceptionHandler.
  support_t *shellSup = allocateSupport();
  if (shellSup == NULL)
    // Se non ci sono strutture disponibili, impossibile avviare: terminazione
    SYSCALL(TERMPROCESS, 0, 0, 0);

  // La shell ha ASID 1 (il primo U-proc, distinto dal kernel con ASID 0)
  shellSup->sup_asid = 1;

  /* Contesto 0 (PGFAULTEXCEPT): Gestore delle eccezioni TLB (il Pager).
   * Quando si verifica un page fault, il Nucleo "passa su" l'eccezione
   * al Pager tramite questo contesto. */
  shellSup->sup_exceptContext[0].pc = (memaddr)Pager;

  /* Sezione 10, ottimizzazione 10.6: Allocazione degli stack per i gestori
   * di eccezioni direttamente dalla RAM (sotto RAMTOP) anziché come campi
   * nella Support Structure. Il primo stack (Context 0, Pager) si trova
   * a RAMTOP - PAGESIZE. */
  memaddr ramtop;
  RAMTOP(ramtop);
  shellSup->sup_exceptContext[0].stackPtr = ramtop - PAGESIZE;

  // Modalità kernel con interrupt abilitati: il Pager opera in kernel-mode
  shellSup->sup_exceptContext[0].status =
      MSTATUS_MPIE_MASK | MSTATUS_MPP_M;

  /* Contesto 1 (GENERALEXCEPT): Gestore delle eccezioni generali
   * (SYSCALL positive, Program Trap, ecc.). Stack allocato a
   * RAMTOP - 2*PAGESIZE per non sovrapporsi a quello del Pager. */
  shellSup->sup_exceptContext[1].pc = (memaddr)GeneralExceptionHandler;
  shellSup->sup_exceptContext[1].stackPtr = ramtop - (2 * PAGESIZE);
  shellSup->sup_exceptContext[1].status =
      MSTATUS_MPIE_MASK | MSTATUS_MPP_M;

  // Sezione 10.3/10.4: Leggiamo l'header del programma shell dal
  // dispositivo flash 0 (associato all'ASID 1) per determinare la
  // dimensione della sezione .text e distinguere le pagine di sola
  // lettura da quelle scrivibili.
  volatile memaddr flash0Base =
      START_DEVREG + ((INTLINE_FLASH - INTLINE_DISK) * 0x80) + (0 * 0x10);
  volatile dtpreg_t *flash0 = (volatile dtpreg_t *)flash0Base;

  // Impostiamo il registro data0 del flash per indicare dove scrivere
  // il blocco letto (il nostro buffer statico)
  flash0->data0 = (memaddr)shellHeaderBuf;

  // Leggiamo il blocco 0 (header) tramite DOIO (NSYS5)
  int shellHeaderStatus =
      SYSCALL(DOIO, (unsigned int)&(flash0->command), FLASHREAD, 0);

  /* Se non riusciamo a leggere l'header della shell il sistema non può
   * avviarsi: errore fatale di inizializzazione. */
  if (shellHeaderStatus != 1)
    PANIC();

  /* Sezione 10.4: Estrazione della dimensione della sezione .text
   * dall'header a.out per determinare quali pagine sono di sola lettura
   * (codice) e quali sono scrivibili (dati). */
  unsigned int textSize =
      *((unsigned int *)shellHeaderBuf + AOUT_HE_TEXT_MEMSZ);
  unsigned int numTextPages = textSize / PAGESIZE;
  if ((textSize % PAGESIZE) != 0) {
    numTextPages++; // Arrotondamento per eccesso
  }

  /* Sezione 2.1: Per ogni entry della page table (eccetto lo stack):
   * - VPN: da 0x80000 a 0x8001E (31 pagine text/data)
   * - ASID: 1 (shell)
   * - V (Valid): 0 (la pagina non è ancora in RAM)
   * - D (Dirty): 0 per .text (sola lettura), 1 per .data (scrivibile) */
  for (int i = 0; i < MAXPAGES - 1; i++) {
    // Imposta EntryHI: VPN (Virtual Page Number) + ASID
    shellSup->sup_privatePgTbl[i].pte_entryHI =
        ((0x80000 + i) << VPNSHIFT) | (1 << ASIDSHIFT);

    /* Sezione 10.4 (ottimizzazione): Le pagine della sezione .text
     * vengono inizializzate in sola lettura (entryLO = 0, D bit = 0),
     * mentre le pagine .data/.bss sono scrivibili (DIRTYON, D bit = 1). */
    if (i < numTextPages) {
      shellSup->sup_privatePgTbl[i].pte_entryLO = 0; // sola lettura
    } else {
      shellSup->sup_privatePgTbl[i].pte_entryLO = DIRTYON; // scrivibile
    }
  }

  /* Sezione 2.1: Inizializzazione della pagina di stack (entry 31).
   * Il VPN dello stack è 0xBFFFF (indirizzo logico 0xBFFFF000), la
   * cui estremità superiore arriva a 0xC000.0000 (USERSTACKTOP).
   * Lo stack è sempre scrivibile (DIRTYON). */
  shellSup->sup_privatePgTbl[MAXPAGES - 1].pte_entryHI =
      (0xBFFFF << VPNSHIFT) | (1 << ASIDSHIFT);
  shellSup->sup_privatePgTbl[MAXPAGES - 1].pte_entryLO = DIRTYON;

   /* Sezione 9: Crea il processo shell come figlio dell'Instantiator
   * tramite NSYS1 (CreateProcess). La shell ha priorità 1 e la sua
   * Support Structure appena configurata viene passata come parametro. */
  SYSCALL(CREATEPROCESS, (unsigned int)&shellState, 1, (unsigned int)shellSup);

   /* L'Instantiator si blocca su masterSemaphore (operazione P) in attesa
   * che la shell esegua una V al momento della sua terminazione. */
  SYSCALL(PASSEREN, (unsigned int)&masterSemaphore, 0, 0);

   /* La shell è terminata: l'Instantiator si auto-termina tramite NSYS2
   * (TerminateProcess). Essendo l'unico processo rimasto, la terminazione
   * porterà processCount a 0, causando una HALT da parte dello scheduler. */
  SYSCALL(TERMPROCESS, 0, 0, 0);
}
