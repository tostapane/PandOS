#include "headers/initProc.h"
#include "../headers/const.h"
#include "headers/vmSupport.h"
#include <uriscv/liburiscv.h>
#include <uriscv/types.h>

/**
 * Gestione delle strutture di supporto e inizializzazione del sistema per la
 * Fase 3. Questo file contiene l'Instantiator Process (test) che prepara
 * l'ambiente per i processi utente e l'uTLB Refill Handler per la gestione
 * degli indirizzi virtuali.
 */

/* Variabili globali per la gestione della Swap Pool e sincronizzazione */
swap_t swapPool[POOLSIZE];
int swapSemaphore;
int masterSemaphore;

/* Lista delle strutture di supporto (support_t) attualmente non in uso */
LIST_HEAD(supStructs_freeList);

/* ASID del processo che detiene attualmente l'accesso esclusivo alla page table
 */
int page_mutex_holder;

/* Semaforo per la sincronizzazione tra la shell e i suoi processi figli.
 * Garantisce che la shell resti in attesa della terminazione del figlio
 * lanciato. */
int shellSemaphore;

/* Semafori per i dispositivi periferici (terminali, stampanti, etc.) */
int devSemaphores[NSUPPSEM];

/* Array statico delle strutture di supporto per i processi utente */
support_t supStructs[UPROCMAX];

/* Buffer statico per la lettura degli header dei file ELF senza gravare sullo
 * stack del kernel. Posizionato nella sezione .data per essere disponibile
 * all'avvio. */
static unsigned int shellHeaderBuf[PAGESIZE / sizeof(unsigned int)];

/* Dichiarazioni esterne per gli handler definiti in sysSupport.c, vmSupport.c e exceptions.c */
extern void Pager();
extern void GeneralExceptionHandler();
extern void uTLB_RefillHandler();

/**
 * Estrae una struttura di supporto libera dalla lista.
 * @return Puntatore a una support_t disponibile o NULL se esaurite.
 */
support_t *allocateSupport() {
  if (list_empty(&supStructs_freeList))
    return NULL;
  struct list_head *l = supStructs_freeList.next;
  list_del(l);
  return container_of(l, support_t, s_list);
}

/**
 * Reinserisce una struttura di supporto nella lista delle disponibili.
 * @param s Puntatore alla struttura da rilasciare.
 */
void deallocateSupport(support_t *s) {
  list_add(&(s->s_list), &supStructs_freeList);
}

/**
 * Processo Istanziatore (test).
 * Inizializza le strutture dati della Fase 3, prepara la shell e attende
 * la conclusione delle operazioni di sistema.
 */
void test() {
  /* Imposta l'handler per il refill del TLB nel vettore di pass-up */
  passupvector_t *passupvector = (passupvector_t *)PASSUPVECTOR;
  passupvector->tlb_refill_handler = (memaddr)uTLB_RefillHandler;

  /* Inizializzazione della Swap Pool e relativo semaforo di mutua esclusione */
  swapSemaphore = 1;
  initSwapStructs();

  /* Inizializzazione dei semafori per i dispositivi periferici */
  for (int i = 0; i < NSUPPSEM; i++) {
    devSemaphores[i] = 1;
  }

  /* Organizzazione delle strutture di supporto in una lista libera */
  INIT_LIST_HEAD(&supStructs_freeList);
  for (int i = 0; i < UPROCMAX; i++) {
    deallocateSupport(&supStructs[i]);
  }

  /* Inizializzazione dei semafori di sincronizzazione master e shell */
  masterSemaphore = 0;
  shellSemaphore = 0;
  page_mutex_holder = -1;

  /* Configurazione dello stato iniziale del processore per il processo Shell
   * (U-mode) */
  state_t shellState;
  shellState.pc_epc = UPROCSTARTADDR; // Punto di ingresso standard: 0x8000.00B0
  shellState.reg_sp = USERSTACKTOP;   // Cima dello stack utente: 0xC000.0000

  /* Modalita' utente, interrupt abilitati, timer locale attivo */
  shellState.status = MSTATUS_MPIE_MASK | MSTATUS_MPP_U;

  /* L'ASID 1 e' riservato alla shell (l'ASID 0 e' per i demoni del kernel) */
  shellState.entry_hi = (1 << ASIDSHIFT);

  /* Allocazione e configurazione della Support Structure per la shell */
  support_t *shellSup = allocateSupport();
  if (shellSup == NULL)
    SYSCALL(TERMPROCESS, 0, 0, 0);

  shellSup->sup_asid = 1;

  /* Configurazione dei contesti per la gestione delle eccezioni della shell */
  /* Contesto 0: TLB Exception Handler (Pager) */
  shellSup->sup_exceptContext[0].pc = (memaddr)Pager;
  memaddr ramtop = RAMTOP(ramtop);
  shellSup->sup_exceptContext[0].stackPtr = ramtop - PAGESIZE;
  shellSup->sup_exceptContext[0].status = MSTATUS_MPIE_MASK | MSTATUS_MPP_M;

  /* Contesto 1: General Exception Handler */
  shellSup->sup_exceptContext[1].pc = (memaddr)GeneralExceptionHandler;
  shellSup->sup_exceptContext[1].stackPtr = ramtop - (2 * PAGESIZE);
  shellSup->sup_exceptContext[1].status = MSTATUS_MPIE_MASK | MSTATUS_MPP_M;

  /* 10.3 - Lettura dell'header ELF del file shell dal dispositivo Flash 0 */
  volatile memaddr flash0Base =
      START_DEVREG + ((INTLINE_FLASH - INTLINE_DISK) * 0x80) + (0 * 0x10);
  volatile dtpreg_t *flash0 = (volatile dtpreg_t *)flash0Base;

  flash0->data0 = (memaddr)shellHeaderBuf;
  SYSCALL(DOIO, (unsigned int)&(flash0->command), FLASHREAD, 0);

  /* Calcolo del numero di pagine necessarie per il segmento text basandosi
   * sull'header */
  unsigned int textSize = *((unsigned int *)shellHeaderBuf + 1);
  unsigned int numTextPages = textSize / PAGESIZE;
  if ((textSize % PAGESIZE) != 0) {
    numTextPages++;
  }

  /* Inizializzazione della Page Table privata per la shell */
  for (int i = 0; i < MAXPAGES - 1; i++) {
    shellSup->sup_privatePgTbl[i].pte_entryHI =
        ((0x80000 + i) << VPNSHIFT) | (1 << ASIDSHIFT);

    /* Le pagine del segmento text sono impostate in sola lettura */
    if (i < numTextPages) {
      shellSup->sup_privatePgTbl[i].pte_entryLO = 0;
    } else {
      /* Le altre pagine sono scrivibili */
      shellSup->sup_privatePgTbl[i].pte_entryLO = DIRTYON;
    }
  }

  /* Inizializzazione della pagina dedicata allo stack (sempre scrivibile) */
  shellSup->sup_privatePgTbl[MAXPAGES - 1].pte_entryHI =
      (0xBFFFF << VPNSHIFT) | (1 << ASIDSHIFT);
  shellSup->sup_privatePgTbl[MAXPAGES - 1].pte_entryLO = DIRTYON;

  /* Creazione del processo Shell */
  SYSCALL(CREATEPROCESS, (unsigned int)&shellState, 1, (unsigned int)shellSup);

  /* Attesa della terminazione della shell tramite operazione P sul
   * masterSemaphore */
  SYSCALL(PASSEREN, (unsigned int)&masterSemaphore, 0, 0);

  /* Terminazione del sistema una volta che la shell ha concluso la sua
   * esecuzione */
  SYSCALL(TERMPROCESS, 0, 0, 0);
}