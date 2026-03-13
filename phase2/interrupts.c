#include "./headers/interrupts.h"
#include "../../uriscv-latest/src/include/uriscv/cpu.h"
#include <uriscv/const.h>
#include <uriscv/types.h>

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
  unsigned int status = devAddrBase_ptr[STATUS];
  devAddrBase_ptr[COMMAND] = ACK;
}
void PLTInterrupt(void) {}
void ITInterrupt(void) {}
