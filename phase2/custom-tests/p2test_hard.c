/* phase2/p2test_hard.c */
#include "../../headers/const.h"
#include "../../headers/types.h"
#include <uriscv/liburiscv.h>

typedef unsigned int devregtr;

#define PRINTCHR 2
#define RECVD 5
#define TERMSTATMASK 0xFF
#define TERM0ADDR 0x10000254

#define BUSERROR 7
#define ADDRERROR 5
#define USYSCALLEXCPT 8
#define MSYSCALLEXCPT 11
#define BADADDR 0xFFFFFFFF

#define QPAGE 1024

int sem_term_mut = 1;

void print(char *msg) {
  char *s = msg;
  devregtr *base = (devregtr *)(TERM0ADDR);
  devregtr *command = base + 3;
  devregtr status;

  SYSCALL(PASSEREN, (int)&sem_term_mut, 0, 0);
  while (*s != '\0') {
    devregtr value = PRINTCHR | (((devregtr)*s) << 8);
    status = SYSCALL(DOIO, (int)command, (int)value, 0);
    if ((status & TERMSTATMASK) != RECVD) {
      PANIC();
    }
    s++;
  }
  SYSCALL(VERHOGEN, (int)&sem_term_mut, 0, 0);
}

void uTLB_RefillHandler() {
  print("TLB Refill Exception\n");
  setENTRYHI(0x80000000);
  setENTRYLO(0x00000000);
  TLBWR();
  LDST((state_t *)BIOSDATAPAGE);
}

unsigned int current_sp_alloc = 0;

int get_new_stack() {
  int sp;
  SYSCALL(PASSEREN, (int)&sem_term_mut, 0, 0);

  if (current_sp_alloc == 0) {
    state_t s;
    STST(&s);
    current_sp_alloc = s.reg_sp - QPAGE;
  }

  sp = current_sp_alloc;
  current_sp_alloc -= QPAGE;

  SYSCALL(VERHOGEN, (int)&sem_term_mut, 0, 0);
  return sp;
}

/* ------------------- Tree Stress ------------------- */
int sem_tree_mut = 1;
int sem_tree_block = 0;
int sem_tree_sync = 0;
int tree_processes_alive = 0;
int tree_processes_blocked = 0;
int tree_root_pid = 0;
int tree_error = 0;

void p_grandchild() {
  SYSCALL(PASSEREN, (int)&sem_tree_mut, 0, 0);
  tree_processes_alive++;
  SYSCALL(VERHOGEN, (int)&sem_tree_mut, 0, 0);

  SYSCALL(PASSEREN, (int)&sem_tree_mut, 0, 0);
  tree_processes_blocked++;
  SYSCALL(VERHOGEN, (int)&sem_tree_mut, 0, 0);
  SYSCALL(PASSEREN, (int)&sem_tree_block, 0, 0);

  tree_error = 1;
  SYSCALL(TERMPROCESS, 0, 0, 0);
}

void p_child() {
  state_t gc1, gc2;
  STST(&gc1);
  gc1.reg_sp = get_new_stack();
  gc1.pc_epc = (memaddr)p_grandchild;
  gc1.status |= MSTATUS_MIE_MASK | MSTATUS_MPP_M;
  gc1.mie = MIE_ALL;

  STST(&gc2);
  gc2.reg_sp = get_new_stack();
  gc2.pc_epc = (memaddr)p_grandchild;
  gc2.status |= MSTATUS_MIE_MASK | MSTATUS_MPP_M;
  gc2.mie = MIE_ALL;

  SYSCALL(CREATEPROCESS, (int)&gc1, PROCESS_PRIO_LOW, 0);
  SYSCALL(CREATEPROCESS, (int)&gc2, PROCESS_PRIO_LOW, 0);

  SYSCALL(PASSEREN, (int)&sem_tree_mut, 0, 0);
  tree_processes_alive++;
  SYSCALL(VERHOGEN, (int)&sem_tree_mut, 0, 0);

  SYSCALL(PASSEREN, (int)&sem_tree_mut, 0, 0);
  tree_processes_blocked++;
  SYSCALL(VERHOGEN, (int)&sem_tree_mut, 0, 0);
  SYSCALL(PASSEREN, (int)&sem_tree_block, 0, 0);

  tree_error = 1;
  SYSCALL(TERMPROCESS, 0, 0, 0);
}

void p_root() {
  state_t c1, c2;
  STST(&c1);
  c1.reg_sp = get_new_stack();
  c1.pc_epc = (memaddr)p_child;
  c1.status |= MSTATUS_MIE_MASK | MSTATUS_MPP_M;
  c1.mie = MIE_ALL;

  STST(&c2);
  c2.reg_sp = get_new_stack();
  c2.pc_epc = (memaddr)p_child;
  c2.status |= MSTATUS_MIE_MASK | MSTATUS_MPP_M;
  c2.mie = MIE_ALL;

  SYSCALL(CREATEPROCESS, (int)&c1, PROCESS_PRIO_LOW, 0);
  SYSCALL(CREATEPROCESS, (int)&c2, PROCESS_PRIO_LOW, 0);

  SYSCALL(PASSEREN, (int)&sem_tree_mut, 0, 0);
  tree_processes_alive++;
  SYSCALL(VERHOGEN, (int)&sem_tree_mut, 0, 0);

  SYSCALL(VERHOGEN, (int)&sem_tree_sync, 0, 0);

  SYSCALL(PASSEREN, (int)&sem_tree_mut, 0, 0);
  tree_processes_blocked++;
  SYSCALL(VERHOGEN, (int)&sem_tree_mut, 0, 0);
  SYSCALL(PASSEREN, (int)&sem_tree_block, 0, 0);

  tree_error = 1;
  SYSCALL(TERMPROCESS, 0, 0, 0);
}

void test_tree() {
  print("==== Starting tree termination stress test ====\n");
  state_t r;
  STST(&r);
  r.reg_sp = get_new_stack();
  r.pc_epc = (memaddr)p_root;
  r.status |= MSTATUS_MIE_MASK | MSTATUS_MPP_M;
  r.mie = MIE_ALL;

  tree_root_pid = SYSCALL(CREATEPROCESS, (int)&r, PROCESS_PRIO_LOW, 0);
  print("fatto createprocess \n");
  SYSCALL(PASSEREN, (int)&sem_tree_sync, 0, 0);

  while (tree_processes_blocked < 7) {
    SYSCALL(YIELD, 0, 0, 0);
  }

  print("fatto yield \n");
  SYSCALL(TERMPROCESS, tree_root_pid, 0, 0);
  print("fatto termprocess \n");

  for (int i = 0; i < 7; i++) {
    SYSCALL(VERHOGEN, (int)&sem_tree_block, 0, 0);
  }

  for (int i = 0; i < 10; i++) {
    SYSCALL(YIELD, 0, 0, 0);
  }

  if (tree_error) {
    print("Tree termination test FAILED.\n");
    PANIC();
  } else {
    print("Tree termination test PASSED.\n");
  }
}

/* ------------------- ASL Stress Test ------------------- */
int sem_asl_mutex = 1;
int sem_asl_sync = 0;
int asl_counter = 0;

void asl_worker() {
  for (int i = 0; i < 10; i++) {
    SYSCALL(PASSEREN, (int)&sem_asl_mutex, 0, 0);
    int temp = asl_counter;
    SYSCALL(YIELD, 0, 0, 0);
    asl_counter = temp + 1;
    SYSCALL(VERHOGEN, (int)&sem_asl_mutex, 0, 0);
  }
  SYSCALL(VERHOGEN, (int)&sem_asl_sync, 0, 0);
  SYSCALL(TERMPROCESS, 0, 0, 0);
}

void test_asl() {
  print("==== Starting ASL/Mutex stress test ====\n");
  for (int i = 0; i < 10; i++) {
    state_t w;
    STST(&w);
    w.reg_sp = get_new_stack();
    w.pc_epc = (memaddr)asl_worker;
    w.status |= MSTATUS_MIE_MASK | MSTATUS_MPP_M;
    w.mie = MIE_ALL;
    SYSCALL(CREATEPROCESS, (int)&w, PROCESS_PRIO_LOW, 0);
  }

  for (int i = 0; i < 10; i++) {
    SYSCALL(PASSEREN, (int)&sem_asl_sync, 0, 0);
  }

  if (asl_counter != 100) {
    print("ASL/Mutex test FAILED.\n");
    PANIC();
  } else {
    print("ASL/Mutex test PASSED.\n");
  }
}

/* ------------------- Clock/Time Stress Test ------------------- */
int sem_clock_mut = 1;
int sem_clock_sync = 0;
int clock_processes_done = 0;

void clock_worker() {
  cpu_t last_time = SYSCALL(GETTIME, 0, 0, 0);
  for (int i = 0; i < 5; i++) {
    SYSCALL(CLOCKWAIT, 0, 0, 0);
    cpu_t new_time = SYSCALL(GETTIME, 0, 0, 0);
    if (new_time < last_time) {
      print("Error: Time went backwards!\n");
      PANIC();
    }
    last_time = new_time;
  }

  SYSCALL(PASSEREN, (int)&sem_clock_mut, 0, 0);
  clock_processes_done++;
  SYSCALL(VERHOGEN, (int)&sem_clock_mut, 0, 0);

  SYSCALL(VERHOGEN, (int)&sem_clock_sync, 0, 0);
  SYSCALL(TERMPROCESS, 0, 0, 0);
}

void test_clock() {
  print("==== Starting Clock/Time stress test ====\n");
  for (int i = 0; i < 15; i++) {
    state_t w;
    STST(&w);
    w.reg_sp = get_new_stack();
    w.pc_epc = (memaddr)clock_worker;
    w.status |= MSTATUS_MIE_MASK | MSTATUS_MPP_M;
    w.mie = MIE_ALL;
    SYSCALL(CREATEPROCESS, (int)&w, PROCESS_PRIO_LOW, 0);
  }

  for (int i = 0; i < 15; i++) {
    SYSCALL(PASSEREN, (int)&sem_clock_sync, 0, 0);
  }

  if (clock_processes_done != 15) {
    print("Clock test FAILED.\n");
    PANIC();
  } else {
    print("Clock test PASSED.\n");
  }
}

/* ------------------- PassUp or Die Stress Test ------------------- */
int sem_passup_sync = 0;
int passup_traps = 0;
support_t passup_support;

void passup_worker2() {
  SYSCALL(99, 0, 0, 0);
  SYSCALL(VERHOGEN, (int)&sem_passup_sync, 0, 0);
  SYSCALL(TERMPROCESS, 0, 0, 0);
}

void passup_general_handler() {
  unsigned int cause = passup_support.sup_exceptState[GENERALEXCEPT].cause;
  // Extract exception code without shifting (RISC-V architecture constraint)
  unsigned int excCode = cause & 0x0000007C;
  // Convert back MIPS-like 0x7C mask logic to natural values if exceptions.c
  // does not shift For RISC-V, the M-mode syscall is actually 11. The previous
  // test logic expected either BUSERROR (7), ADDRERROR (5), etc. If
  // exceptions.c intercepts them naturally, the actual hardware value is in the
  // lower bits.

  if (cause == ADDRERROR || cause == BUSERROR) {
    passup_traps++;
    passup_support.sup_exceptState[GENERALEXCEPT].pc_epc =
        (memaddr)passup_worker2;
    LDST(&(passup_support.sup_exceptState[GENERALEXCEPT]));
  } else if (cause == MSYSCALLEXCPT || cause == USYSCALLEXCPT) {
    passup_traps++;
    passup_support.sup_exceptState[GENERALEXCEPT].pc_epc += 4;
    LDST(&(passup_support.sup_exceptState[GENERALEXCEPT]));
  } else {
    print("Unexpected exception in passup handler!\n");
    PANIC();
  }
}

void passup_pgfault_handler() {
  print("Caught TLB exception!\n");
  PANIC();
}

void passup_worker() {
  *((memaddr *)0x00000000) = 0;
  print("Should not reach here\n");
  PANIC();
}

void test_passup() {
  print("==== Starting PassUp or Die stress test ====\n");

  passup_support.sup_exceptContext[GENERALEXCEPT].stackPtr = get_new_stack();
  passup_support.sup_exceptContext[GENERALEXCEPT].status |=
      MSTATUS_MIE_MASK | MSTATUS_MPP_M;
  passup_support.sup_exceptContext[GENERALEXCEPT].pc =
      (memaddr)passup_general_handler;

  passup_support.sup_exceptContext[PGFAULTEXCEPT].stackPtr = get_new_stack();
  passup_support.sup_exceptContext[PGFAULTEXCEPT].status |=
      MSTATUS_MIE_MASK | MSTATUS_MPP_M;
  passup_support.sup_exceptContext[PGFAULTEXCEPT].pc =
      (memaddr)passup_pgfault_handler;

  state_t w;
  STST(&w);
  w.reg_sp = get_new_stack();
  w.pc_epc = (memaddr)passup_worker;
  w.status |= MSTATUS_MIE_MASK | MSTATUS_MPP_M;
  w.mie = MIE_ALL;

  SYSCALL(CREATEPROCESS, (int)&w, PROCESS_PRIO_LOW, (int)&passup_support);

  SYSCALL(PASSEREN, (int)&sem_passup_sync, 0, 0);

  if (passup_traps != 2) {
    print("PassUp test FAILED.\n");
    PANIC();
  } else {
    print("PassUp test PASSED.\n");
  }
}

/* ------------------- Main test ------------------- */
void test() {
  print("Beginning Hard Phase 2 Test...\n");

  test_asl();
  test_tree();
  test_clock();
  test_passup();

  print("Hard Phase 2 Test Complete! System halted\n");
  HALT();
}
