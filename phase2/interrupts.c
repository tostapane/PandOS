#include "./headers/interrupts.h"
#include "../../uriscv-latest/src/include/uriscv/cpu.h"
#include "headers/initial.h"
#include <uriscv/const.h>
#include <uriscv/types.h>
/**
 * todo:
 * - rimuovere syscall da non timer
 *  -gestire errori
 */
void handleInterrupt(void) {
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
    // perform a VERHOGEN
    (*semValue)++;
    pcb->p_s.reg_a0 = status;
    insertProcQ(&readyQueue, pcb);
    softBlockCount--;
    // da blocked a ready
    pcb->p_semAdd = NULL;
  }

  unsigned int cpuNum = getPRID();
  if (currProc)
    LDST(GET_EXCEPTION_STATE_PTR(cpuNum));
  else
    scheduler();
}

void PLTInterrupt(void) {}
void ITInterrupt(void) {}
