#include "./headers/interrupts.h"
#include "../../uriscv-latest/src/include/uriscv/cpu.h"
#include "headers/initial.h"
#include <uriscv/const.h>
#include <uriscv/liburiscv.h>
#include <uriscv/types.h>
/**
 * todo:
 - gestire errori
 */

volatile unsigned int *bitmap = (unsigned int *)BITMAP_BASE;

void interruptHandler(void) {
  unsigned int exceptCode = getCAUSE() & CAUSE_EXCCODE_MASK;
  unsigned int intlineNo;
  switch (exceptCode) {
  case IL_CPUTIMER:
    intlineNo = 1;
    PLTInterrupt();
    break;
  case IL_TIMER:
    intlineNo = 2;
    ITInterrupt();
    break;
  case IL_DISK:
    intlineNo = 3;
    deviceInterrupt(intlineNo);
    break;
  case IL_FLASH:
    intlineNo = 4;
    deviceInterrupt(intlineNo);
    break;
  case IL_ETHERNET:
    intlineNo = 5;
    deviceInterrupt(intlineNo);
    break;
  case IL_PRINTER:
    intlineNo = 6;
    deviceInterrupt(intlineNo);
    break;
  case IL_TERMINAL:
    intlineNo = 7;
    deviceInterrupt(intlineNo);
    break;
  default:
    // errore
    break;
  }
}

void deviceInterrupt(unsigned int intlineNo) {
  unsigned int word = intlineNo - 3;
  unsigned int DevNo;
  // bool found = false;
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
    /*errore */
  }
  volatile memaddr devAddrBase =
      START_ADDR + ((intlineNo - 3) * 0x80) + (DevNo * 0x10);
  volatile memaddr *devAddrBase_ptr = (volatile memaddr *)devAddrBase;
  unsigned int status;
  unsigned int semNum;
  if (word != 4) {
    // NON terminal
    status = devAddrBase_ptr[STATUS];
    devAddrBase_ptr[COMMAND] = ACK;
    semNum = (intlineNo - 3) * 8 + DevNo;
  } else {
    // terminal
    unsigned int tran_status = devAddrBase_ptr[TRANSTATUS];
    unsigned int recv_status = devAddrBase_ptr[RECVSTATUS];
    if (tran_status != READY && tran_status != BUSY &&
        tran_status != UNINSTALLED) {
      status = tran_status;
      devAddrBase_ptr[TRANCOMMAND] = ACK;
      semNum = 32 + DevNo;
    } else if (recv_status != READY && recv_status != BUSY &&
               recv_status != UNINSTALLED) {
      status = recv_status;
      devAddrBase_ptr[RECVCOMMAND] = ACK;
      semNum = 32 + DevNo + 8;
    }
  }
  int *semValue = (int *)&subDevice[semNum];
  pcb_t *pcb = removeBlocked(semValue);
  if (pcb) {
    pcb->p_s.reg_a0 = status;
    insertProcQ(&readyQueue, pcb);
    softBlockCount--;
    // da blocked a ready
    pcb->p_semAdd = NULL;
  } else
    (*semValue)++;

  unsigned int cpuNum = getPRID();
  if (currProc)
    LDST(GET_EXCEPTION_STATE_PTR(cpuNum));
  else
    scheduler();
}

// gestione interrupt causati da process local timer
void PLTInterrupt(void) {
  unsigned int cpuNum = getPRID();
  state_t *state = GET_EXCEPTION_STATE_PTR(cpuNum);
  currProc->p_s = *state;
  cpu_t currTime;
  STCK(currTime);
  currProc->p_time = currProc->p_time + currTime - processTimer;
  setTIMER(TIMESLICE);
  insertProcQ(&readyQueue, currProc);
  currProc = NULL;
  scheduler();
}

void ITInterrupt(void) {
  LDIT(PSECOND);
  int *sem = (int *)&subDevice[48];
  pcb_t *pcb;
  while (headBlocked(sem)) {
    pcb = removeBlocked(sem);
    pcb->p_semAdd = NULL;
    softBlockCount--;
    insertProcQ(&readyQueue, pcb);
  }
  subDevice[48] = 0;
  unsigned int cpuNum = getPRID();
  if (currProc)
    LDST(GET_EXCEPTION_STATE_PTR(cpuNum));
  else
    scheduler();
}
