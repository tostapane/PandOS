#include "./headers/scheduler.h"
#include <uriscv/const.h>
#include <uriscv/liburiscv.h>

void scheduler(void) {

  if (!emptyProcQ(&readyQueue)) {

    // 3.1
    currProc = removeProcQ(&readyQueue);

    // 3.2 Load 5 mills on the PLT [section 7.2].
    setTIMER(TIMESLICE);
    // save the start time of the process
    STCK(processTimer);
    // 3.3
    LDST(&currProc->p_s);

  } else {
    if (processCount == 0) {
      HALT();
    }
    if (processCount > 0 && softBlockCount > 0) {

      setMIE(MIE_ALL & ~MIE_MTIE_MASK);
      unsigned int status = getSTATUS();
      status |= MSTATUS_MIE_MASK;
      setSTATUS(status);

      // enter a wait state
      WAIT();
    }
    if (processCount > 0 && softBlockCount == 0) {
      PANIC();
    }
  }
}
