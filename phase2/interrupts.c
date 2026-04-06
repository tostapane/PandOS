#include "headers/interrupts.h"
#include "headers/initial.h"
#include <uriscv/const.h>
#include <uriscv/cpu.h>
#include <uriscv/liburiscv.h>
#include <uriscv/types.h>

// Indirizzo base della mappa di bit per gli Interrupt pendenti dei dispositivi
volatile unsigned int *bitmap = (unsigned int *)BITMAP_BASE;

/**
 * @brief Identifica e gestisce la tipologia di Interrupt generato
 * sull'architettura.
 *
 * Questa funzione direziona l'esecuzione verso l'handler dedicato (Timer
 * LOCALE, Interval Timer o Dispositivi esterni) leggendo il registro CAUSE del
 * processore. Utilizza delle costanti strutturate su "Linee di Interrupt"
 * codificate.
 */
void interruptHandler(void) {

  // Estrae rigorosamente la porzione di codice d'eccezione mascherando i flag
  // di stato
  unsigned int exceptCode = getCAUSE() & CAUSE_EXCCODE_MASK;
  unsigned int intlineNo;

  // Smista l'anomalia verso il sottosistema del Nucleo di dovere
  switch (exceptCode) {
  case IL_CPUTIMER:
    // Linea 1: PLT (Processor Local Timer) comunemente deputato al time-slicing
    // (Scheduler)
    intlineNo = 1;
    PLTInterrupt();
    break;
  case IL_TIMER:
    // Linea 2: Interval Timer per calcoli generici o di orologio di sistema
    // (Pseudo-clock)
    intlineNo = 2;
    ITInterrupt();
    break;
  case IL_DISK:
    // Linea 3: Dischi di archiviazione magnetici/non-volatili
    intlineNo = 3;
    deviceInterrupt(intlineNo);
    break;
  case IL_FLASH:
    // Linea 4: Memorie Flash ad accesso fulmineo ma disaccoppiato
    intlineNo = 4;
    deviceInterrupt(intlineNo);
    break;
  case IL_ETHERNET:
    // Linea 5: Interfacce di connettività di rete
    intlineNo = 5;
    deviceInterrupt(intlineNo);
    break;
  case IL_PRINTER:
    // Linea 6: Dispositivi di stampa
    intlineNo = 6;
    deviceInterrupt(intlineNo);
    break;
  case IL_TERMINAL:
    // Linea 7: Terminali interattivi (ciascun device possiede sub-moduli T/R)
    intlineNo = 7;
    deviceInterrupt(intlineNo);
    break;
  default:
    // Gravissima condizione hardware: una linea interrotta senza driver
    // previsti dal OS
    PANIC();
    break;
  }
}

/**
 * @brief Risolve l'Interrupt pendente per uno specifico livello periferico (da
 * linea 3 a 7).
 *
 * Utilizza una mappa hardware per sondare quale tra gli 8 dispositivi associati
 * a quella linea è scattato, decifra in memoria il suo registro dati e
 * ripristina in coda gli eventuali processi sospesi all'attesa del segnale in
 * questione.
 *
 * @param intlineNo L'indice in memoria del canale d'interrupt rilevato.
 */
void deviceInterrupt(unsigned int intlineNo) {
  // L'indice della bitmap parte dai dispositivi mappati dalla riga n.3 (Offset
  // logico 0)
  unsigned int word = intlineNo - 3;
  unsigned int DevNo;

  // Scansione di priorità in ordine hardware: i canali più bassi godono di
  // attenzione preferenziale Confronta bit a bit per ritrovare quale degli 8
  // dispositivi fisici (0-7) ha richiesto interrupt
  if (bitmap[word] & DEV0ON) {
    DevNo = 0;
  } else if (bitmap[word] & DEV1ON) {
    DevNo = 1;
  } else if (bitmap[word] & DEV2ON) {
    DevNo = 2;
  } else if (bitmap[word] & DEV3ON) {
    DevNo = 3;
  } else if (bitmap[word] & DEV4ON) {
    DevNo = 4;
  } else if (bitmap[word] & DEV5ON) {
    DevNo = 5;
  } else if (bitmap[word] & DEV6ON) {
    DevNo = 6;
  } else if (bitmap[word] & DEV7ON) {
    DevNo = 7;
  } else {
    // Segnale fantasma senza corrispondente mappatura bit
    PANIC();
  }

  // Calcolo algebrico dell'indirizzo base della periferica:
  // - START_ADDR è la base in memoria del Memory-Mapped I/O.
  // - Ogni linea di Interrupt si posiziona a step esadecimali di 0x80 bytes di
  // distanza.
  // - Ogni sub-dispositivo interno si diparte scartando 0x10 bytes.
  volatile memaddr devAddrBase =
      START_ADDR + ((intlineNo - 3) * 0x80) + (DevNo * 0x10);

  // Casting in una unione strutturata di convenienza che riflette fedelmente i
  // registri device
  volatile devreg_t *device_register = (volatile devreg_t *)devAddrBase;
  unsigned int status;
  int semNum = -1;

  if (word != 4) {
    // Se non è un terminale, leggiamo lo stato del dispositivo e diamo l'ACK
    status = device_register->dtp.status;
    device_register->dtp.command = ACK;
    // Calcoliamo l'indice del semaforo corrispondente: 8 dispositivi per linea
    semNum = (intlineNo - 3) * 8 + DevNo;
  } else {
    // I terminali hanno due sottomoduli: trasmissione e ricezione.
    // Dobbiamo capire quale dei due abbia generato l'interrupt cercando
    // il codice di operazione completata con successo (ovvero il valore 5).
    unsigned int tran_status = device_register->term.transm_status;
    unsigned int recv_status = device_register->term.recv_status;

    // Per confrontare il valore con 5, usiamo la maschera '& 0xFF' per isolare
    // esclusivamente i primi 8 bit (il byte meno significativo) del registro.
    // È vitale usare questa maschera perché, nei terminali, i bit superiori di
    // questo registro non sono zeri ma contengono il singolo carattere ASCII
    // appena trasmesso o ricevuto. Mascherando ignoriamo il carattere e
    // leggiamo lo status puro.
    if ((tran_status & 0xFF) == 5) {
      status = tran_status;
      device_register->term.transm_command = ACK;
      semNum = 32 + DevNo; // Transmission
    } else if ((recv_status & 0xFF) == 5) {
      status = recv_status;
      device_register->term.recv_command = ACK;
      semNum = 32 + DevNo + 8; // Receipt
    }
  }

  if (semNum == -1) // Errore hardware fatale: non sono stati trovati
                    // dispositivi eleggibili
    PANIC();

  // Otteniamo la locazione del semaforo per il corretto device dall'array
  // dedicato
  int *semValue = (int *)&subDevice[semNum];

  // Sveglia e rimuovi un apposito processo bloccato ad aspettare questo
  // input/output
  pcb_t *pcb = removeBlocked(semValue);

  if (pcb) {
    // Passa lo status del dispositivo nel registro a0 del PCB risvegliato per
    // ritornarglielo come valore di ritorno
    pcb->p_s.reg_a0 = status;

    // Ripristina l'eleggibilità del processo per la CPU spostandolo in Ready
    // Queue
    insertProcQ(&readyQueue, pcb);
    softBlockCount--;

    // Il processo entra nello stato 'ready' (non è più in Wait I/O), perciò
    // azzeriamo la dipendenza col semaforo
    pcb->p_semAdd = NULL;
  } else {
    // Se c'è l'interrupt ma NESSUN processo ha attualmente bloccato quel
    // dispositivo, "verhogen" sul semaforo salvando la capacità per il prossimo
    // che lo richiederà.
    (*semValue)++;
  }

  // Routine di ripristino post-interrupt
  unsigned int cpuNum = getPRID();
  if (currProc) {
    // C'era già un processo esecutivo interrotto; non invochiamo scheduler ma
    // lo riprendiamo immediatamente
    STCK(processTimer);
    LDST(GET_EXCEPTION_STATE_PTR(cpuNum));
  } else
    scheduler();
}

// Gestione interrupt causati dal Process Local Timer
// Il PLT scatta quando si esaurisce il quanto di tempo (Time Slice)
// Preassegnato al processo in esecuzione
void PLTInterrupt(void) {
  // Identifica l'ID CPU ed estrae lo stato salvato (il momento esatto del trap
  // da timer)
  unsigned int cpuNum = getPRID();
  state_t *state = GET_EXCEPTION_STATE_PTR(cpuNum);

  // Salva questo stato attuale nel descrittore del processo in modo da
  // riprenderlo dopo
  currProc->p_s = *state;

  // Ricarica il timer a 5 millisecondi (Time Slice)
  setTIMER(TIMESLICE);

  // Rimette il processo corrente alla coda di ready
  insertProcQ(&readyQueue, currProc);

  cpu_t curr_time;
  STCK(curr_time);

  if (currProc != NULL) {
    // Salva il tempo trascorso
    currProc->p_time += (curr_time - processTimer);
    processTimer = curr_time;
  }
  currProc = NULL;

  // Affida il comando generale allo scheduler perché avvii il processo
  // successivo o il prossimo in Ready Queue
  scheduler();
}

void ITInterrupt(void) {
  // Riavvia/Ripristina l'Interval Timer a 100 millisecondi
  LDIT(PSECOND);
  int *sem = (int *)&subDevice[NRSEMAPHORES - 1];
  pcb_t *pcb;

  // Svuota e risveglia indiscriminatamente tutti i processi che attendevano e
  // si erano fermati (CLOCKWAIT)
  while (headBlocked(sem)) {
    pcb = removeBlocked(sem);
    pcb->p_semAdd = NULL;
    softBlockCount--;
    insertProcQ(&readyQueue, pcb);
  }

  // Resetta forzosamente il semaforo a 0 essendo stati svuotati i blocchi
  subDevice[NRSEMAPHORES - 1] = 0;

  // Come per le procedure post-interrupt se avevamo interrotto un programma lo
  // riavviamo. Altrimenti Scheduler.
  unsigned int cpuNum = getPRID();
  if (currProc) {
    STCK(processTimer);
    LDST(GET_EXCEPTION_STATE_PTR(cpuNum));
  } else {
    scheduler();
  }
}
