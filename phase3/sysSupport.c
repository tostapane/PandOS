#include "headers/sysSupport.h"
#include "../headers/const.h"
#include "headers/initProc.h"
#include "headers/vmSupport.h"
#include <uriscv/aout.h>
#include <uriscv/const.h>
#include <uriscv/cpu.h>
#include <uriscv/liburiscv.h>
#include <uriscv/types.h>

/* Buffer statico per leggere l'header a.out dei nuovi programmi durante
 * l'esecuzione di EXECUTE (SYS6). Dichiarato 'static' per collocarlo
 * nella sezione .data del kernel anziché sullo stack, evitando overflow
 * durante la lettura di un intero blocco flash. */
static unsigned int execHeaderBuf[PAGESIZE / sizeof(unsigned int)];

/**
 * SEZIONE 6: Gestore delle Eccezioni Generali del Livello di Supporto
 *
 * @brief Punto di ingresso per tutte le eccezioni non-TLB "passate su"
 *        dal Nucleo al Livello di Supporto.
 *
 * Viene invocata quando il Nucleo esegue un "Pass Up" verso il contesto
 * GENERALEXCEPT (indice 1) della Support Structure dell'U-proc corrente.
 * Lo stato processore al momento dell'eccezione si trova in
 * sup_exceptState[1]. Esamina il codice d'eccezione e smista al gestore
 * appropriato: SYSCALL o Program Trap.
 */
void GeneralExceptionHandler() {
  /* Ottiene la Support Structure dell'U-proc corrente tramite NSYS8
   * (GetSupportData) per accedere allo stato d'eccezione salvato */
  support_t *supStruct = (support_t *)SYSCALL(GETSUPPORTPTR, 0, 0, 0);

  /* Estrae il codice d'eccezione dal registro cause dello stato salvato
   * nel contesto GENERALEXCEPT (indice 1) */
  unsigned int cause = supStruct->sup_exceptState[1].cause;
  unsigned int excCode = (cause & CAUSE_EXCCODE_MASK);

  /* Smista in base alla causa: se è una SYSCALL (ecall), la gestiamo
   * con il SyscallExceptionHandler; altrimenti è un Program Trap */
  if (excCode == SYSEXCEPTION) {
    SyscallExceptionHandler(supStruct);
  } else {
    ProgramTrapHandler(supStruct);
  }
}

/**
 * SEZIONE 7: Gestore delle SYSCALL del Livello di Supporto
 *
 * @brief Gestisce le SYSCALL con numero positivo (≥ 1) per gli U-proc.
 *
 * Il Nucleo gestisce direttamente le SYSCALL negative (NSYS1-NSYS10).
 * Per le SYSCALL positive, il Nucleo esegue un "Pass Up" e il controllo
 * arriva qui. Le SYSCALL supportate dal Livello di Supporto sono:
 * - SYS2 (TERMINATE): Terminazione volontaria dell'U-proc
 * - SYS4 (WRITETERMINAL): Scrittura su terminale
 * - SYS5 (READTERMINAL): Lettura da terminale
 * - SYS6 (EXECUTE): Esecuzione di un programma (solo dalla shell)
 *
 * @param supStruct Puntatore alla Support Structure dell'U-proc corrente.
 */
void SyscallExceptionHandler(support_t *supStruct) {
  /* Estrae il numero della SYSCALL dal registro a0 dello stato
   * d'eccezione salvato nel contesto GENERALEXCEPT */
  unsigned int syscallNum = supStruct->sup_exceptState[GENERALEXCEPT].reg_a0;

  /* Sezione 7: Incrementa il PC di 4 byte (WORDLEN) per evitare un loop
   * infinito di SYSCALL al ritorno. Senza questo incremento, l'istruzione
   * ecall verrebbe rieseguita all'infinito. */
  supStruct->sup_exceptState[1].pc_epc += WORDLEN;

  switch (syscallNum) {

  /*
   * SEZIONE 7.1: Terminate (SYS2)
   *
   * Terminazione volontaria dell'U-proc. Viene trattata esattamente
   * come un Program Trap: invoca ProgramTrapHandler che si occupa della
   * pulizia ordinata (rilascio frame nella Swap Pool, segnalazione al
   * semaforo corretto, restituzione della Support Structure, ecc.).
   */
  case TERMINATE:
    ProgramTrapHandler(supStruct);
    break;

  /*
   * SEZIONE 7.2: WriteTerminal (SYS4)
   *
   * Scrive una stringa di caratteri sul Terminale 0 (sotto-dispositivo
   * di trasmissione). L'U-proc viene sospeso finché ogni carattere
   * non è stato trasmesso.
   *
   * Parametri (dallo stato salvato):
   *   a1 = indirizzo virtuale del primo carattere della stringa
   *   a2 = lunghezza della stringa
   *
   * Valore di ritorno in a0:
   *   Successo: numero di caratteri effettivamente trasmessi
   *   Errore: negativo del codice di stato del dispositivo
   */
  case WRITETERMINAL: {
    char *addr = (char *)supStruct->sup_exceptState[GENERALEXCEPT].reg_a1;
    int len = (int)supStruct->sup_exceptState[GENERALEXCEPT].reg_a2;

    /* Sezione 7.2: Validazione dei parametri. È un errore se:
     * - la lunghezza è negativa o superiore a 128
     * - l'indirizzo è fuori dallo spazio logico dell'U-proc
     *   (inferiore a KUSEG 0x80000000 o superiore a USERSTACKTOP 0xC0000000)
     * Qualsiasi errore causa la terminazione dell'U-proc. */
    if (len < 0 || len > 128 || (unsigned)addr < KUSEG ||
        (unsigned)addr + len > USERSTACKTOP) {
      ProgramTrapHandler(supStruct);
      return;
    }

    /* Acquisizione della mutua esclusione sul sotto-dispositivo di
     * trasmissione del Terminale 0 (indice 40 nell'array devSemaphores:
     * 32 dispositivi non-terminale + 8 terminali TX = indice 40) */
    SYSCALL(PASSEREN, (int)&devSemaphores[40], 0, 0);

    /* Calcolo dell'indirizzo base del registro del Terminale 0:
     * START_ADDR + (4 linee di offset per i terminali * 0x80) + (device 0) */
    volatile memaddr term0base = START_ADDR + (4 * 0x80) + (0 * 0x10);
    volatile termreg_t *term_reg = (volatile termreg_t *)term0base;
    unsigned commandAddr = (unsigned)&term_reg->transm_command;

    /* Trasmissione carattere per carattere tramite DOIO (NSYS5).
     * Il comando è composto dal carattere ASCII nei byte alti (shift di 8)
     * e dal codice operazione TRANSMITCHAR nel byte basso. */
    int nsent = 0;
    for (int i = 0; i < len; ++i) {
      unsigned cmd = (addr[i] << 8) | TRANSMITCHAR;
      int ioStatus = SYSCALL(DOIO, commandAddr, cmd, 0);

      /* Verifica dello stato: isoliamo il byte basso con la maschera 0xFF
       * (il byte alto contiene il carattere trasmesso/ricevuto). Se lo
       * stato non è OKCHARTRANS (5, "Character Transmitted"), la
       * trasmissione è fallita: restituiamo il negativo dello stato. */
      if ((ioStatus & 0xFF) != OKCHARTRANS) {
        supStruct->sup_exceptState[GENERALEXCEPT].reg_a0 = -(ioStatus & 0xFF);
        break;
      }
      nsent++;
    }

    /* Se tutti i caratteri sono stati trasmessi con successo,
     * restituisce il conteggio totale in a0 */
    if (nsent == len) {
      supStruct->sup_exceptState[GENERALEXCEPT].reg_a0 = nsent;
    }

    /* Rilascio della mutua esclusione sul terminale di trasmissione */
    SYSCALL(VERHOGEN, (int)&devSemaphores[40], 0, 0);
    break;
  }

  /*
   * SEZIONE 7.3: ReadTerminal (SYS5)
   *
   * Legge una riga di input dal Terminale 0 (sotto-dispositivo di
   * ricezione). L'U-proc viene sospeso finché non viene ricevuto un
   * carattere di newline ('\n') o si verifica un errore.
   *
   * Parametri (dallo stato salvato):
   *   a1 = indirizzo virtuale del buffer di destinazione
   *
   * Valore di ritorno in a0:
   *   Successo: numero di caratteri ricevuti (incluso '\n')
   *   Errore: negativo del codice di stato del dispositivo
   */
  case READTERMINAL: {
    /* Acquisizione della mutua esclusione sul sotto-dispositivo di
     * ricezione del Terminale 0 (indice 32 nell'array devSemaphores:
     * 32 dispositivi non-terminale = offset base per i terminali RX).
     * Questo è distinto dal semaforo di trasmissione (indice 40). */
    unsigned int readMutex = (unsigned int)&(devSemaphores[32]);
    SYSCALL(PASSEREN, readMutex, 0, 0);

    unsigned int vAddr =
        (unsigned int)supStruct->sup_exceptState[GENERALEXCEPT].reg_a1;
    char *virtAddr = (char *)supStruct->sup_exceptState[GENERALEXCEPT].reg_a1;
    unsigned int nrecvd = 0;

    while (1) {
      /* Sezione 7.3: Validazione dell'indirizzo ad ogni iterazione.
       * Se il buffer di destinazione esce dallo spazio logico dell'U-proc
       * (inferiore a KUSEG o superiore/uguale a USERSTACKTOP), è un errore
       * che causa la terminazione dell'U-proc. */
      if (vAddr < KUSEG || vAddr + nrecvd >= USERSTACKTOP) {
        SYSCALL(VERHOGEN, readMutex, 0, 0);
        ProgramTrapHandler(supStruct);
        return;
      }

      /* Calcolo dell'indirizzo base del Terminale 0 e invio del comando
       * RECEIVECHAR per ricevere un singolo carattere */
      volatile memaddr term0base = START_ADDR + (4 * 0x80) + (0 * 0x10);
      volatile termreg_t *term_register = (volatile termreg_t *)term0base;
      unsigned int commAddr = (unsigned int)&term_register->recv_command;
      int ioStatus = SYSCALL(DOIO, commAddr, RECEIVECHAR, 0);

      /* Verifica dello stato: se non è CHARRECV (5, "Character Received"),
       * la ricezione è fallita. Restituisce il negativo dello stato. */
      if ((ioStatus & 0xFF) != CHARRECV) {
        supStruct->sup_exceptState[GENERALEXCEPT].reg_a0 = -(ioStatus & 0xFF);
        break;
      }

      /* Estrazione del carattere ricevuto: si trova nel secondo byte
       * dello status (bit 8-15). Usiamo la maschera 0xFF00 e uno shift
       * a destra di 8 per isolarlo (maschera opposta a quella usata
       * per lo status code nei bit 0-7). */
      char c = (ioStatus & 0xFF00) >> 8;
      virtAddr[nrecvd] = c;
      nrecvd++;

      /* Se il carattere ricevuto è un newline, la riga è completa:
       * restituiamo il numero di caratteri ricevuti (incluso '\n') */
      if (c == '\n') {
        supStruct->sup_exceptState[GENERALEXCEPT].reg_a0 = nrecvd;
        break;
      }
    }

    /* Rilascio della mutua esclusione sul terminale di ricezione */
    SYSCALL(VERHOGEN, readMutex, 0, 0);
    break;
  }

  /*
   * SEZIONE 7.4: Execute (SYS6)
   *
   * Esecuzione di un programma utente. SYSCALL riservata esclusivamente
   * alla shell (ASID 1): spawna un nuovo U-proc figlio dal dispositivo
   * flash associato all'ASID specificato.
   *
   * Parametri (dallo stato salvato):
   *   a1 = ASID del programma da eseguire (deve essere in [2..UPROCMAX])
   *
   * Valore di ritorno in a0:
   *   Successo: PID del nuovo processo
   *   Errore: -1 (ASID invalido, risorse esaurite, errore I/O)
   *
   * La shell si blocca su shellSemaphore dopo aver lanciato il figlio
   * e viene sbloccata quando il figlio termina (tramite ProgramTrapHandler).
   */
  case EXECUTE: {
    /* Solo la shell (ASID 1) può eseguire EXECUTE. Se un altro U-proc
     * tenta di usarla, viene terminato come Program Trap. */
    if (supStruct->sup_asid != 1) {
      ProgramTrapHandler(supStruct);
      return;
    }

    unsigned int asid = supStruct->sup_exceptState[GENERALEXCEPT].reg_a1;

    /* Validazione dell'ASID: deve essere nel range [2..UPROCMAX].
     * L'ASID 0 è riservato al kernel, l'ASID 1 è la shell. Un valore
     * fuori range renderebbe invalidi l'offset del device flash, la
     * page table e gli stack degli handler. */
    if (asid <= 1 || asid > UPROCMAX) {
      supStruct->sup_exceptState[GENERALEXCEPT].reg_a0 = -1;
      break;
    }

    /* Inizializzazione dello stato processore del nuovo U-proc:
     * modalità utente, tutti gli interrupt abilitati, PC e SP
     * agli indirizzi standard dello spazio utente. */
    state_t newState;
    newState.pc_epc = UPROCSTARTADDR;   // 0x8000.00B0
    newState.reg_sp = USERSTACKTOP;     // 0xC000.0000
    newState.status = MSTATUS_MPIE_MASK | MSTATUS_MPP_U; // Modalità utente
    newState.entry_hi = asid << ASIDSHIFT; // ASID nel campo EntryHI
    // Tutti gli interrupt (incluso il PLT, bit MTIE) abilitati
    newState.mie = MIE_ALL;

    /* Allocazione di una Support Structure dalla free list per il
     * nuovo U-proc. Se non ce ne sono di disponibili, restituisce -1. */
    support_t *newSupport = allocateSupport();

    if (newSupport == NULL) {
      supStruct->sup_exceptState[GENERALEXCEPT].reg_a0 = -1;
      break;
    }

    // Assegna l'ASID alla struttura di supporto
    newSupport->sup_asid = asid;

    memaddr ramtop;
    RAMTOP(ramtop);

    /* Sezione 10.6 (ottimizzazione): Allocazione degli stack per i gestori
     * di eccezioni direttamente dalla RAM sotto RAMTOP.
     * Per l'ASID k: TLB handler stack a RAMTOP - ((k*2-1) * PAGESIZE)
     *               General handler stack a RAMTOP - ((k*2) * PAGESIZE)
     * Questo evita sovrapposizioni tra gli stack dei diversi U-proc. */

    // Contesto 0 (PGFAULTEXCEPT): Gestore TLB (Pager)
    newSupport->sup_exceptContext[0].pc = (memaddr)Pager;
    newSupport->sup_exceptContext[0].stackPtr =
        ramtop - ((asid * 2 - 1) * PAGESIZE);
    newSupport->sup_exceptContext[0].status = MSTATUS_MPIE_MASK | MSTATUS_MPP_M;

    // Contesto 1 (GENERALEXCEPT): Gestore eccezioni generali
    newSupport->sup_exceptContext[1].pc = (memaddr)GeneralExceptionHandler;
    newSupport->sup_exceptContext[1].stackPtr =
        ramtop - ((asid * 2) * PAGESIZE);
    newSupport->sup_exceptContext[1].status = MSTATUS_MPIE_MASK | MSTATUS_MPP_M;

    /* Sezione 10.3: Lettura dell'header a.out del programma dal dispositivo
     * flash associato all'ASID (flash device asid-1, poiché la shell usa
     * il flash 0 con ASID 1). L'header contiene la dimensione del .text
     * per determinare le pagine di sola lettura. */
    volatile memaddr flashDevBase = START_DEVREG +
                                    ((INTLINE_FLASH - INTLINE_DISK) * 0x80) +
                                    ((asid - 1) * 0x10);
    volatile dtpreg_t *flashDev = (volatile dtpreg_t *)flashDevBase;

    /* Mutua esclusione sul dispositivo flash tramite il semaforo dedicato.
     * L'indice del semaforo flash nell'array devSemaphores è 8 + (asid-1):
     * 8 semafori disk (0-7) + offset per il flash specifico. */
    int flashSem = 8 + (asid - 1);
    SYSCALL(PASSEREN, (unsigned int)&devSemaphores[flashSem], 0, 0);

    // Imposta il buffer di destinazione e legge il blocco 0 (header)
    flashDev->data0 = (memaddr)execHeaderBuf;
    int headerStatus =
        SYSCALL(DOIO, (unsigned int)&(flashDev->command), FLASHREAD, 0);

    // Rilascia il flash dopo la lettura
    SYSCALL(VERHOGEN, (unsigned int)&devSemaphores[flashSem], 0, 0);

    /* Errore di lettura dell'header dal flash (Sezione 4.2/8): impossibile
     * avviare il programma. Restituisce la Support Structure alla free list
     * e segnala errore (-1) al chiamante. */
    if (headerStatus != 1) {
      deallocateSupport(newSupport);
      supStruct->sup_exceptState[GENERALEXCEPT].reg_a0 = -1;
      break;
    }

    /* Sezione 10.4 (ottimizzazione): Estrazione della dimensione della
     * sezione .text dall'header a.out per distinguere le pagine di codice
     * (sola lettura) dalle pagine di dati (scrivibili). */
    unsigned int textSize =
        *((unsigned int *)execHeaderBuf + AOUT_HE_TEXT_MEMSZ);
    unsigned int numTextPages = textSize / PAGESIZE;
    if ((textSize % PAGESIZE) != 0) {
      numTextPages++; // Arrotondamento per eccesso
    }

    /* Sezione 2.1: Inizializzazione della Page Table privata del nuovo U-proc.
     * Le prime 31 entry coprono lo spazio text/data (VPN 0x80000..0x8001E),
     * l'ultima (entry 31) è riservata allo stack (VPN 0xBFFFF). */
    for (int i = 0; i < MAXPAGES - 1; i++) {
      // EntryHI: VPN + ASID del nuovo U-proc
      newSupport->sup_privatePgTbl[i].pte_entryHI =
          ((0x80000 + i) << VPNSHIFT) | (asid << ASIDSHIFT);

      /* Sezione 10.4: Pagine .text in sola lettura (entryLO = 0, D bit = 0).
       * Pagine .data/.bss scrivibili (DIRTYON, D bit = 1). */
      if (i < numTextPages) {
        newSupport->sup_privatePgTbl[i].pte_entryLO = 0; // sola lettura
      } else {
        newSupport->sup_privatePgTbl[i].pte_entryLO = DIRTYON; // scrivibile
      }
    }

    /* Pagina di stack (sempre scrivibile): VPN 0xBFFFF con D bit attivo */
    newSupport->sup_privatePgTbl[MAXPAGES - 1].pte_entryHI =
        (0xBFFFF << VPNSHIFT) | (asid << ASIDSHIFT);
    newSupport->sup_privatePgTbl[MAXPAGES - 1].pte_entryLO = DIRTYON;

    /* Crea il nuovo processo tramite NSYS1 (CreateProcess) con priorità 1
     * e la Support Structure appena configurata */
    int newPid = SYSCALL(CREATEPROCESS, (unsigned int)&newState, 1,
                         (unsigned int)newSupport);

    /* NSYS1 ritorna NOPROC (-1) se non ci sono PCB liberi: in questo caso
     * nessun figlio farà mai la V su shellSemaphore, quindi non dobbiamo
     * bloccarci. Restituiamo la Support Structure e segnaliamo errore. */
    if (newPid == NOPROC) {
      deallocateSupport(newSupport);
      supStruct->sup_exceptState[GENERALEXCEPT].reg_a0 = -1;
      break;
    }

    /* Sezione 7.4/9: La shell si blocca su shellSemaphore (operazione P)
     * in attesa che il processo figlio termini. Al termine del figlio,
     * il ProgramTrapHandler eseguirà una V su shellSemaphore,
     * sbloccando la shell che riprenderà il suo ciclo REPL. */
    SYSCALL(PASSEREN, (unsigned int)&shellSemaphore, 0, 0);

    break;
  }

  /*
   * Default: SYSCALL non riconosciuta
   *
   * Un U-proc ha richiesto una SYSCALL con numero positivo non supportato
   * dal Livello di Supporto. Viene trattata come errore fatale e
   * l'U-proc viene terminato.
   */
  default:
    ProgramTrapHandler(supStruct);
    break;
  }

  /* Sezione 7: Se la SYSCALL non ha causato la terminazione dell'U-proc,
   * restituisce il controllo ricaricando lo stato processore salvato
   * (con il PC già incrementato di 4). */
  LDST(&(supStruct->sup_exceptState[1]));
}

/**
 * SEZIONE 8: Gestore dei Program Trap del Livello di Supporto
 *
 * @brief Gestisce le eccezioni fatali per un U-proc, effettuando una
 *        pulizia ordinata prima della terminazione.
 *
 * Viene invocata sia per veri Program Trap (istruzioni illegali, errori
 * di indirizzamento, ecc.) sia per terminazioni volontarie (SYS2) e
 * SYSCALL non supportate. Esegue le seguenti operazioni di pulizia:
 *
 * 1. Pulizia della Swap Pool: marca come liberi tutti i frame occupati
 *    dal processo morente (Sezione 10.2, ottimizzazione)
 * 2. Rilascio dei semafori di I/O pendenti
 * 3. Segnalazione di sincronizzazione (masterSemaphore o shellSemaphore)
 * 4. Restituzione della Support Structure alla free list (Sezione 10.7)
 * 5. Invalidazione della TLB
 * 6. Terminazione del processo tramite NSYS2
 *
 * @param supStruct Puntatore alla Support Structure dell'U-proc morente.
 */
void ProgramTrapHandler(support_t *supStruct) {

  /* Acquisisce la mutua esclusione sulla Swap Pool, ma solo se non la
   * deteniamo già. Questo controllo tramite page_mutex_holder evita un
   * self-deadlock quando il Pager invoca ProgramTrapHandler dopo aver
   * già acquisito swapSemaphore (es. errore I/O durante paginazione). */
  if (page_mutex_holder != supStruct->sup_asid) {
    SYSCALL(PASSEREN, (unsigned int)&swapSemaphore, 0, 0);
  }

  /* Sezione 10.2 (ottimizzazione): Marca come liberi tutti i frame
   * nella Swap Pool occupati dall'ASID del processo morente. Questo
   * elimina scritture superflue sul backing store (flash), poiché
   * i dati del processo non saranno più necessari. */
  for (int i = 0; i < POOLSIZE; i++) {
    if (swapPool[i].sw_asid == supStruct->sup_asid) {
      swapPool[i].sw_asid = -1;
      swapPool[i].sw_pageNo = -1;
      swapPool[i].sw_pte = NULL;
    }
  }

  /* Rilascia il semaforo della Swap Pool prima di procedere
   * con la terminazione */
  page_mutex_holder = -1;
  SYSCALL(VERHOGEN, (unsigned int)&swapSemaphore, 0, 0);

  /* Caso limite: se il processo stava eseguendo una READTERMINAL (SYS5)
   * e muore durante l'operazione (es. per un TLB-Mod sulla scrittura
   * nel buffer utente), il semaforo devSemaphores[32] (terminale 0 RX)
   * rimarrebbe bloccato. Con al più un U-proc attivo (la shell si blocca
   * su shellSemaphore durante EXECUTE), sem[32] == 0 implica che a
   * detenerlo è il processo morente. Senza questa V, la prossima
   * READTERMINAL della shell rimarrebbe bloccata per sempre. */
  if (devSemaphores[32] == 0)
    SYSCALL(VERHOGEN, (unsigned int)&devSemaphores[32], 0, 0);

  /* Sezione 9: Identifica il semaforo corretto su cui eseguire la V
   * per sbloccare il processo che attende la terminazione. */
  if (supStruct->sup_asid == 1) {
    /* Se il processo morente è la shell (ASID 1), esegue una V su
     * masterSemaphore per sbloccare l'Instantiator Process, che
     * procederà con lo shutdown del sistema. */
    SYSCALL(VERHOGEN, (unsigned int)&masterSemaphore, 0, 0);
  } else {
    /* Se è un figlio della shell (ASID 2..UPROCMAX), esegue una V su
     * shellSemaphore per sbloccare la shell, che era in attesa della
     * terminazione del figlio dopo l'EXECUTE (SYS6). */
    SYSCALL(VERHOGEN, (unsigned int)&shellSemaphore, 0, 0);
  }

  /* Sezione 10.7 (ottimizzazione): Restituisce la Support Structure
   * alla free list tramite deallocateSupport, rendendola disponibile
   * per futuri U-proc. */
  deallocateSupport(supStruct);

  /* Cancella tutte le entry nella TLB relative all'ASID del processo
   * morente. Usiamo TLBCLR (cancellazione totale) poiché un'invalidazione
   * selettiva richiederebbe un ciclo di probe (TLBP) per ogni possibile
   * entry, risultando più costosa dei TLB miss che essa eviterebbe. */
  TLBCLR();

  /* Invoca NSYS2 (TerminateProcess) tramite il Nucleo per rimuovere
   * definitivamente il processo dal sistema. Il Nucleo si occuperà di
   * aggiornare processCount, la Ready Queue e lo scheduler. */
  SYSCALL(TERMPROCESS, 0, 0, 0);
}
