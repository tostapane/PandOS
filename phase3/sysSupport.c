#include "headers/sysSupport.h"
#include "../headers/const.h"
#include "headers/initProc.h"
#include "headers/vmSupport.h"
#include <uriscv/const.h>
#include <uriscv/cpu.h>
#include <uriscv/liburiscv.h>
#include <uriscv/types.h>

// buffer per leggere l'header dei nuovi processi
static unsigned int execHeaderBuf[PAGESIZE / sizeof(unsigned int)];

void GeneralExceptionHandler() {
  support_t *supStruct = (support_t *)SYSCALL(GETSUPPORTPTR, 0, 0, 0);

  unsigned int cause = supStruct->sup_exceptState[1].cause;
  unsigned int excCode = (cause & CAUSE_EXCCODE_MASK);

  if (excCode == SYSEXCEPTION) {
    SyscallExceptionHandler(supStruct, excCode);
  } else {
    ProgramTrapHandler(supStruct);
  }
}

void SyscallExceptionHandler(support_t *supStruct, unsigned int excCode) {
  /* Retrieve the arguments from the saved state's registers
   * a0 = syscall number, a1-a3 = parameters
   */
  unsigned int syscallNum = supStruct->sup_exceptState[GENERALEXCEPT].reg_a0;

  // Increment PC by 4
  supStruct->sup_exceptState[1].pc_epc += WORDLEN;

  switch (syscallNum) {
  case TERMINATE: /* SYS2 (2) */
    /* Treat an intentional termination exactly like a Program Trap.
     */
    ProgramTrapHandler(supStruct);
    break;

  /* SYS4 (4) */
  case WRITETERMINAL: {
    char *addr = (char *)supStruct->sup_exceptState[GENERALEXCEPT].reg_a1;
    int len = (int)supStruct->sup_exceptState[GENERALEXCEPT].reg_a2;

    // Address is out of range (<0x80000000 or >0xC0000000)
    if (len < 0 || len > 128 || (unsigned)addr < KUSEG ||
        (unsigned)addr + len > USERSTACKTOP) {
      ProgramTrapHandler(supStruct);
      return;
    }

    // P operation
    SYSCALL(PASSEREN, (int)&devSemaphores[40], 0, 0);

    // Write char by char
    volatile memaddr term0base = START_ADDR + (4 * 0x80) + (0 * 0x10);
    volatile termreg_t *term_reg = (volatile termreg_t *)term0base;
    unsigned commandAddr = (unsigned)&term_reg->transm_command;
    int nsent = 0;
    int i;
    for (i = 0; i < len; ++i) {
      unsigned cmd = (addr[i] << 8) | TRANSMITCHAR;
      int ioStatus = SYSCALL(DOIO, commandAddr, cmd, 0);
      if ((ioStatus & 0xFF) != OKCHARTRANS) {
        supStruct->sup_exceptState[GENERALEXCEPT].reg_a0 = -(ioStatus & 0xFF);
        break;
      }
      nsent++;
    }

    if (nsent == len) {
      supStruct->sup_exceptState[GENERALEXCEPT].reg_a0 = nsent;
    }

    // Release mutex
    SYSCALL(VERHOGEN, (int)&devSemaphores[40], 0, 0);
    break;
  }

  case READTERMINAL: {
    // semaforo del terminal 0 in lettura: il sub-device "receive" e' all'indice
    // 32, distinto da quello di "transmit" (40) usato dalla WRITETERMINAL.
    unsigned int readMutex = (unsigned int)&(devSemaphores[32]);
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
      if ((ioStatus & 0xFF) != CHARRECV) {
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

    /* L'ASID deve essere nel range [2..UPROCMAX]: 0 e' del kernel, 1 e' la
     * shell. Un valore fuori range renderebbe invalidi l'offset del device
     * flash, la page table e gli stack degli handler. */
    if (asid <= 1 || asid > UPROCMAX) {
      supStruct->sup_exceptState[GENERALEXCEPT].reg_a0 = -1;
      break;
    }

    // inizializzo status del nuovo processo
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

    // tlb handler
    newSupport->sup_exceptContext[0].pc = (memaddr)Pager;
    newSupport->sup_exceptContext[0].stackPtr =
        ramtop - ((asid * 2 - 1) * PAGESIZE);
    newSupport->sup_exceptContext[0].status = MSTATUS_MPIE_MASK | MSTATUS_MPP_M;
    // general exception handler
    newSupport->sup_exceptContext[1].pc = (memaddr)GeneralExceptionHandler;
    newSupport->sup_exceptContext[1].stackPtr =
        ramtop - ((asid * 2) * PAGESIZE);
    newSupport->sup_exceptContext[1].status = MSTATUS_MPIE_MASK | MSTATUS_MPP_M;

    volatile memaddr flashDevBase = START_DEVREG +
                                    ((INTLINE_FLASH - INTLINE_DISK) * 0x80) +
                                    ((asid - 1) * 0x10);
    volatile dtpreg_t *flashDev = (volatile dtpreg_t *)flashDevBase;

    flashDev->data0 = (memaddr)execHeaderBuf;
    int headerStatus =
        SYSCALL(DOIO, (unsigned int)&(flashDev->command), FLASHREAD, 0);
    /* Errore di lettura dell'header dal flash (spec 4.2/8): niente spawn, -1 al
     * chiamante. La support struct gia' allocata va restituita. */
    if (headerStatus != 1) {
      deallocateSupport(newSupport);
      supStruct->sup_exceptState[GENERALEXCEPT].reg_a0 = -1;
      break;
    }

    // estrazione dimensione del .text
    unsigned int textSize = *((unsigned int *)execHeaderBuf + 1);
    unsigned int numTextPages = textSize / PAGESIZE;
    if ((textSize % PAGESIZE) != 0) {
      numTextPages++;
    }

    // private page table
    for (int i = 0; i < MAXPAGES - 1; i++) {
      newSupport->sup_privatePgTbl[i].pte_entryHI =
          ((0x80000 + i) << VPNSHIFT) | (asid << ASIDSHIFT);

      // .text in readonly
      if (i < numTextPages) {
        newSupport->sup_privatePgTbl[i].pte_entryLO = 0; // sola lettura
      } else {
        newSupport->sup_privatePgTbl[i].pte_entryLO = DIRTYON; // scrivibile
      }
    }

    // stack page (sempre scrivibile)
    newSupport->sup_privatePgTbl[MAXPAGES - 1].pte_entryHI =
        (0xBFFFF << VPNSHIFT) | (asid << ASIDSHIFT);
    newSupport->sup_privatePgTbl[MAXPAGES - 1].pte_entryLO = DIRTYON;

    // creo il nuovo processo a priorita' 1
    SYSCALL(CREATEPROCESS, (unsigned int)&newState, 1,
            (unsigned int)newSupport);

    // fermo la shell
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

/* richiediamo mutua esclusione per la swap pool
 * che contiene lo stato degli indirizzi RAM usati dai processi in
 * quell'istante. Andiamo a modificare lo stato degli indirizzi che stiamo
 * liberando */

void ProgramTrapHandler(support_t *supStruct) {
  // The process is going to die. We need to clean up its mess.

  // acquisiamo mutua esclusione sulla swap pool
  // se non la abbiamo gia (controllo su page_mutex_holder).
  if (page_mutex_holder != supStruct->sup_asid) {
    SYSCALL(PASSEREN, (unsigned int)&swapSemaphore, 0, 0);
  }

  // OTTIMIZZAZIONE 10.2
  // mark all of the frame it occupies as unoccpied in order
  // to eliminate extraneous write to the backing store
  for (int i = 0; i < POOLSIZE; i++) {
    if (swapPool[i].sw_asid == supStruct->sup_asid) {
      swapPool[i].sw_asid = -1;
      swapPool[i].sw_pageNo = -1;
      swapPool[i].sw_pte = NULL;
    }
  }

  // rilasciamo il semaforo prima di morire
  page_mutex_holder = -1;
  SYSCALL(VERHOGEN, (unsigned int)&swapSemaphore, 0, 0);

  // cerco il semaforo corretto
  if (supStruct->sup_asid == 1) {
    /* When the shell terminates, either normally or abnormally, it should
      perform a V on the masterSemaphore */
    SYSCALL(VERHOGEN, (unsigned int)&masterSemaphore, 0, 0);
  } else {
    // se e' un figlio della shell, sveglia la shell
    SYSCALL(VERHOGEN, (unsigned int)&shellSemaphore, 0, 0);
  }

  // returning the struct to the freeList
  deallocateSupport(supStruct);

  // clear the TLB
  TLBCLR();

  // termina il processo
  SYSCALL(TERMPROCESS, 0, 0, 0);
}
