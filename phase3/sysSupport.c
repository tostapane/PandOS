#include "headers/sysSupport.h"
#include "../headers/const.h"
#include "headers/initProc.h"
#include "headers/vmSupport.h"
#include <uriscv/const.h>
#include <uriscv/cpu.h>
#include <uriscv/liburiscv.h>
#include <uriscv/types.h>

/**
 * Gestione delle eccezioni di sistema e delle chiamate di servizio per i
 * processi utente. Implementa il supporto per l'I/O su terminale, l'esecuzione
 * di processi (EXECUTE) e la gestione della terminazione (Program Trap).
 */

/* Buffer statico per la lettura dell'header ELF dei processi lanciati via
 * EXECUTE */
static unsigned int execHeaderBuf[PAGESIZE / sizeof(unsigned int)];

/**
 * Entry point per la gestione delle eccezioni generali (non TLB) dei processi
 * utente. Determina se l'eccezione e' una SYSCALL o un errore di programma
 * (Trap).
 */
void GeneralExceptionHandler() {
  support_t *supStruct = (support_t *)SYSCALL(GETSUPPORTPTR, 0, 0, 0);

  unsigned int cause = supStruct->sup_exceptState[1].cause;
  unsigned int excCode = (cause & CAUSE_EXCCODE_MASK);

  if (excCode == SYSEXCEPTION) {
    SyscallExceptionHandler(supStruct, excCode);
  } else {
    /* Tratta ogni altra eccezione come un errore fatale del processo */
    ProgramTrapHandler(supStruct);
  }
}

/**
 * Handler specifico per le system call invocate in modalita' utente.
 * @param supStruct Puntatore alla struttura di supporto del processo corrente.
 * @param excCode Codice dell'eccezione (SYSEXCEPTION).
 */
void SyscallExceptionHandler(support_t *supStruct, unsigned int excCode) {
  /* Recupero degli argomenti dai registri salvati nello stato dell'eccezione:
   * a0 = numero syscall, a1-a3 = parametri. */
  unsigned int syscallNum = supStruct->sup_exceptState[GENERALEXCEPT].reg_a0;

  /* Incremento del PC per passare all'istruzione successiva alla SYSCALL */
  supStruct->sup_exceptState[1].pc_epc += WORDLEN;

  switch (syscallNum) {
  case TERMINATE: /* SYS2 (2) */
    /* La terminazione volontaria e' gestita come una trap programmata */
    ProgramTrapHandler(supStruct);
    break;

  case WRITETERMINAL: { /* SYS4 (4) */
    char *addr = (char *)supStruct->sup_exceptState[GENERALEXCEPT].reg_a1;
    int len = (int)supStruct->sup_exceptState[GENERALEXCEPT].reg_a2;

    /* Validazione dei parametri: lunghezza e appartenenza allo spazio utente */
    if (len < 0 || len > 128 || (unsigned)addr < KUSEG ||
        (unsigned)addr + len > USERSTACKTOP) {
      ProgramTrapHandler(supStruct);
      return;
    }

    /* Mutua esclusione sul terminale (utilizzando il semaforo dedicato) */
    SYSCALL(PASSEREN, (int)&devSemaphores[40], 0, 0);

    /* Scrittura carattere per carattere sul registro del terminale 0 */
    volatile memaddr term0base = START_ADDR + (4 * 0x80) + (0 * 0x10);
    volatile termreg_t *term_reg = (volatile termreg_t *)term0base;
    unsigned commandAddr = (unsigned)&term_reg->transm_command;
    int nsent = 0;
    unsigned i;
    for (i = 0; i < len; ++i) {
      unsigned cmd = (addr[i] << 8) | TRANSMITCHAR;
      int ioStatus = SYSCALL(DOIO, commandAddr, cmd, 0);

      /* Verifica dell'esito della trasmissione */
      if ((ioStatus & 0xFF) != OKCHARTRANS) {
        supStruct->sup_exceptState[GENERALEXCEPT].reg_a0 = -(ioStatus & 0xFF);
        break;
      }
      nsent++;
    }

    if (nsent == len) {
      supStruct->sup_exceptState[GENERALEXCEPT].reg_a0 = nsent;
    }

    /* Rilascio del terminale */
    SYSCALL(VERHOGEN, (int)&devSemaphores[40], 0, 0);
    break;
  }

  case READTERMINAL: { /* SYS5 (5) */
    /* Acquisizione del semaforo per la lettura dal terminale 0 (Receiver) */
    unsigned int readMutex = (unsigned int)&(devSemaphores[32]);
    SYSCALL(PASSEREN, readMutex, 0, 0);

    unsigned int vAddr =
        (unsigned int)supStruct->sup_exceptState[GENERALEXCEPT].reg_a1;
    char *virtAddr = (char *)supStruct->sup_exceptState[GENERALEXCEPT].reg_a1;
    unsigned int nrecvd = 0;

    while (1) {
      /* Controllo continuo dei limiti di memoria per evitare buffer overflow o
       * violazioni */
      if (vAddr < KUSEG || vAddr + nrecvd >= USERSTACKTOP) {
        SYSCALL(VERHOGEN, readMutex, 0, 0);
        ProgramTrapHandler(supStruct);
        return;
      }

      /* Interazione con il registro di ricezione del terminale 0 */
      volatile memaddr term0base = START_ADDR + (4 * 0x80) + (0 * 0x10);
      volatile termreg_t *term_register = (volatile termreg_t *)term0base;
      unsigned int commAddr = (unsigned int)&term_register->recv_command;
      int ioStatus = SYSCALL(DOIO, commAddr, RECEIVECHAR, 0);
      if ((ioStatus & 0xFF) != CHARRECV) {
        supStruct->sup_exceptState[GENERALEXCEPT].reg_a0 = -(ioStatus & 0xFF);
        break;
      }

      /* Estrazione del carattere dai bit alti dello status */
      char c = (ioStatus & 0xFF00) >> 8;
      virtAddr[nrecvd] = c;
      nrecvd++;

      /* La lettura termina alla ricezione di un newline */
      if (c == '\n') {
        supStruct->sup_exceptState[GENERALEXCEPT].reg_a0 = nrecvd;
        break;
      }
    }
    SYSCALL(VERHOGEN, readMutex, 0, 0);
    break;
  }

  case EXECUTE: { /* SYS6 (6) */
    /* Solo la shell (ASID 1) e' autorizzata a lanciare altri processi */
    if (supStruct->sup_asid != 1) {
      ProgramTrapHandler(supStruct);
      return;
    }
    unsigned int asid = supStruct->sup_exceptState[GENERALEXCEPT].reg_a1;

    /* Validazione ASID: deve essere nel range [1..UPROCMAX] e non coincidere con
     * la shell */
    if (asid <= 1 || asid > UPROCMAX) {
      supStruct->sup_exceptState[GENERALEXCEPT].reg_a0 = -1;
      break;
    }

    /* Preparazione dello stato iniziale per il nuovo processo */
    state_t newState;
    newState.pc_epc = UPROCSTARTADDR;
    newState.reg_sp = USERSTACKTOP;
    newState.status = MSTATUS_MPIE_MASK | MSTATUS_MPP_U;
    newState.entry_hi = asid << ASIDSHIFT;

    support_t *newSupport = allocateSupport();
    if (newSupport == NULL) {
      supStruct->sup_exceptState[GENERALEXCEPT].reg_a0 = -1;
      break;
    }

    newSupport->sup_asid = asid;

    memaddr ramtop;
    RAMTOP(ramtop);

    /* Configurazione dei contesti per Pager e General Exception Handler del
     * nuovo processo */
    newSupport->sup_exceptContext[0].pc = (memaddr)Pager;
    newSupport->sup_exceptContext[0].stackPtr =
        ramtop - ((asid * 2 - 1) * PAGESIZE);
    newSupport->sup_exceptContext[0].status = MSTATUS_MPIE_MASK | MSTATUS_MPP_M;

    newSupport->sup_exceptContext[1].pc = (memaddr)GeneralExceptionHandler;
    newSupport->sup_exceptContext[1].stackPtr =
        ramtop - ((asid * 2) * PAGESIZE);
    newSupport->sup_exceptContext[1].status = MSTATUS_MPIE_MASK | MSTATUS_MPP_M;

    /* Lettura dell'header ELF dal dispositivo Flash corrispondente all'ASID */
    volatile memaddr flashDevBase = START_DEVREG +
                                    ((INTLINE_FLASH - INTLINE_DISK) * 0x80) +
                                    ((asid - 1) * 0x10);
    volatile dtpreg_t *flashDev = (volatile dtpreg_t *)flashDevBase;

    flashDev->data0 = (memaddr)execHeaderBuf;
    SYSCALL(DOIO, (unsigned int)&(flashDev->command), FLASHREAD, 0);

    /* Determinazione della dimensione del segmento text dalle informazioni
     * dell'header */
    unsigned int textSize = *((unsigned int *)execHeaderBuf + 1);
    unsigned int numTextPages = textSize / PAGESIZE;
    if ((textSize % PAGESIZE) != 0) {
      numTextPages++;
    }

    /* Inizializzazione della Page Table privata del nuovo processo */
    for (int i = 0; i < MAXPAGES - 1; i++) {
      newSupport->sup_privatePgTbl[i].pte_entryHI =
          ((0x80000 + i) << VPNSHIFT) | (asid << ASIDSHIFT);

      if (i < numTextPages) {
        newSupport->sup_privatePgTbl[i].pte_entryLO = 0; // Sola lettura (Text)
      } else {
        newSupport->sup_privatePgTbl[i].pte_entryLO =
            DIRTYON; // Scrivibile (Data)
      }
    }

    /* Configurazione della pagina di stack */
    newSupport->sup_privatePgTbl[MAXPAGES - 1].pte_entryHI =
        (0xBFFFF << VPNSHIFT) | (asid << ASIDSHIFT);
    newSupport->sup_privatePgTbl[MAXPAGES - 1].pte_entryLO = DIRTYON;

    /* Creazione effettiva del processo a livello di Nucleus */
    SYSCALL(CREATEPROCESS, (unsigned int)&newState, 1,
            (unsigned int)newSupport);

    /* Sospensione della shell finche' il processo figlio non termina */
    SYSCALL(PASSEREN, (unsigned int)&shellSemaphore, 0, 0);

    break;
  }

  default:
    /* Se viene richiesta una SYSCALL non supportata, il processo viene
     * terminato */
    ProgramTrapHandler(supStruct);
    break;
  }

  /* Ripristino dello stato e ritorno al processo utente se non terminato */
  LDST(&(supStruct->sup_exceptState[1]));
}

/**
 * Gestore delle terminazioni anomale e cleanup delle risorse.
 * Libera i frame occupati nella Swap Pool e segnala la terminazione ai semafori
 * di sincronizzazione.
 * @param supStruct Puntatore alla struttura di supporto del processo morente.
 */
void ProgramTrapHandler(support_t *supStruct) {
  /* Garantiamo l'accesso esclusivo alla Swap Pool per evitare race conditions
   * durante il cleanup */
  if (page_mutex_holder != supStruct->sup_asid) {
    SYSCALL(PASSEREN, (unsigned int)&swapSemaphore, 0, 0);
  }

  /* OTTIMIZZAZIONE: Identifichiamo tutti i frame della Swap Pool appartenenti
   * al processo e li marchiamo come liberi. Questo evita scritture inutili sul
   * backing store (Flash). */
  for (int i = 0; i < POOLSIZE; i++) {
    if (swapPool[i].sw_asid == supStruct->sup_asid) {
      swapPool[i].sw_asid = -1;
      swapPool[i].sw_pageNo = -1;
      swapPool[i].sw_pte = NULL;
    }
  }

  /* Rilascio del semaforo della Swap Pool */
  page_mutex_holder = -1;
  SYSCALL(VERHOGEN, (unsigned int)&swapSemaphore, 0, 0);

  /* Gestione della segnalazione di terminazione */
  if (supStruct->sup_asid == 1) {
    /* Se la shell muore, sblocca il processo master per lo shutdown del sistema
     */
    SYSCALL(VERHOGEN, (unsigned int)&masterSemaphore, 0, 0);
  } else {
    /* Se muore un figlio della shell, sblocca la shell stessa */
    SYSCALL(VERHOGEN, (unsigned int)&shellSemaphore, 0, 0);
  }

  /* Restituzione della struttura di supporto alla lista delle disponibili */
  deallocateSupport(supStruct);

  /* Chiamata finale al Nucleus per eliminare il PCB del processo corrente */
  SYSCALL(TERMPROCESS, 0, 0, 0);
}