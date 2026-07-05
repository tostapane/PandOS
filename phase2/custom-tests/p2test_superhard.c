/* phase2/p2test_superhard.c */
#include "../../headers/const.h"
#include "../../headers/types.h"
#include <uriscv/liburiscv.h>

#ifndef CREATEPROCESS
#define CREATEPROCESS -1
#define TERMPROCESS -2
#define PASSEREN -3
#define VERHOGEN -4
#define DOIO -5
#define GETTIME -6
#define CLOCKWAIT -7
#define GETSUPPORTPTR -8
#define GETPROCESSID -9
#define YIELD -10
#endif

typedef unsigned int devregtr;

#define PRINTCHR 2
#define RECVD 5
#define TERMSTATMASK 0xFF
#define TERM0ADDR 0x10000254

#define BUSERROR 7
#define ADDRERROR 5
#define USYSCALLEXCPT 8
#define MSYSCALLEXCPT 11

#define MSTATUS_MIE_MASK 0x8
#define MIE_MTIE_MASK 0x40
#define MIP_MTIP_MASK 0x40
#define MIE_ALL 0xFFFFFFFF

#define MSTATUS_MPIE_BIT 7
#define MSTATUS_MIE_BIT 3
#define MSTATUS_MPRV_BIT 17
#define MSTATUS_MPP_BIT 11
#define MSTATUS_MPP_M 0x1800
#define MSTATUS_MPP_U 0x0000
#define MSTATUS_MPP_MASK 0x1800

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
    current_sp_alloc = s.reg_sp - 2 * QPAGE;
  }
  sp = current_sp_alloc;
  current_sp_alloc -= QPAGE;
  SYSCALL(VERHOGEN, (int)&sem_term_mut, 0, 0);
  return sp;
}

// ------------------- Test 1: Suicide Tree -------------------
int pid_B, pid_C, pid_D;
int sem_suicide_start = 0;

void process_D() {
  SYSCALL(VERHOGEN, (int)&sem_suicide_start, 0, 0);
  SYSCALL(TERMPROCESS, pid_B, 0, 0);
  print("ERROR: D did not die when terminating its ancestor B!\n");
  PANIC();
}

void process_C() {
  state_t d_state;
  STST(&d_state);
  d_state.reg_sp = get_new_stack();
  d_state.pc_epc = (memaddr)process_D;
  d_state.status |= MSTATUS_MIE_MASK | MSTATUS_MPP_M;
  d_state.mie = MIE_ALL;
  pid_D = SYSCALL(CREATEPROCESS, (int)&d_state, 1, 0);
  while (1) {
    SYSCALL(YIELD, 0, 0, 0);
  }
}

void process_B() {
  state_t c_state;
  STST(&c_state);
  c_state.reg_sp = get_new_stack();
  c_state.pc_epc = (memaddr)process_C;
  c_state.status |= MSTATUS_MIE_MASK | MSTATUS_MPP_M;
  c_state.mie = MIE_ALL;
  pid_C = SYSCALL(CREATEPROCESS, (int)&c_state, 1, 0);
  while (1) {
    SYSCALL(YIELD, 0, 0, 0);
  }
}

// ------------------- Test 2: User Mode Trap -------------------
int user_survived = 0;
void user_process() {
  SYSCALL(-1, 0, 0, 0);
  user_survived = 1;
  while (1) {
  }
}

// ------------------- Test 3: Support_t Trap -------------------
support_t f_support;
void f_trap_handler() {
  print("F trap handler caught exception correctly!\n");
  SYSCALL(TERMPROCESS, 0, 0, 0);
}
void f_process() {
  SYSCALL(1, 0, 0, 0);
  print("ERROR: F survived SYSCALL(1) without trap!\n");
  PANIC();
}

// ------------------- Test 4: Semaphore Queue Ordering -------------------
int sem_queue = 0;
int queue_order[3];
int queue_idx = 0;

void process_QA() {
  SYSCALL(PASSEREN, (int)&sem_queue, 0, 0);
  SYSCALL(PASSEREN, (int)&sem_term_mut, 0, 0);
  queue_order[queue_idx++] = 1;
  SYSCALL(VERHOGEN, (int)&sem_term_mut, 0, 0);
  SYSCALL(TERMPROCESS, 0, 0, 0);
}
void process_QB() {
  SYSCALL(PASSEREN, (int)&sem_queue, 0, 0);
  SYSCALL(PASSEREN, (int)&sem_term_mut, 0, 0);
  queue_order[queue_idx++] = 2;
  SYSCALL(VERHOGEN, (int)&sem_term_mut, 0, 0);
  SYSCALL(TERMPROCESS, 0, 0, 0);
}
void process_QC() {
  SYSCALL(PASSEREN, (int)&sem_queue, 0, 0);
  SYSCALL(PASSEREN, (int)&sem_term_mut, 0, 0);
  queue_order[queue_idx++] = 3;
  SYSCALL(VERHOGEN, (int)&sem_term_mut, 0, 0);
  SYSCALL(TERMPROCESS, 0, 0, 0);
}

// ------------------- Test 5: IO Termination -------------------
void child_terminator() {
  SYSCALL(YIELD, 0, 0, 0); // Wait for parent to initiate IO
  SYSCALL(TERMPROCESS, pid_B, 0, 0);
  SYSCALL(TERMPROCESS, 0, 0, 0);
}

void process_B_io() {
  state_t c_state;
  STST(&c_state);
  c_state.reg_sp = get_new_stack();
  c_state.pc_epc = (memaddr)child_terminator;
  c_state.status |= MSTATUS_MIE_MASK | MSTATUS_MPP_M;
  c_state.mie = MIE_ALL;
  SYSCALL(CREATEPROCESS, (int)&c_state, 1, 0);

  // We will do IO on Terminal 1 to not interfere with print() mutex which uses
  // Terminal 0
  devregtr *base = (devregtr *)(0x10000254 + 0x10);
  devregtr *command = base + 3;
  devregtr value = PRINTCHR | ('!' << 8);
  SYSCALL(DOIO, (int)command, (int)value, 0);

  print("ERROR: B survived IO termination!\n");
  PANIC();
}

void test() {
  print("Superhard Test starting...\n");

  // 1. Suicide Test
  print("Starting suicide test...\n");
  state_t b_state;
  STST(&b_state);
  b_state.reg_sp = get_new_stack();
  b_state.pc_epc = (memaddr)process_B;
  b_state.status |= MSTATUS_MIE_MASK | MSTATUS_MPP_M;
  b_state.mie = MIE_ALL;
  pid_B = SYSCALL(CREATEPROCESS, (int)&b_state, 1, 0);

  SYSCALL(PASSEREN, (int)&sem_suicide_start, 0, 0);
  SYSCALL(CLOCKWAIT, 0, 0, 0);
  print("Suicide test completed.\n");

  // 2. User Mode Privilege Trap Test
  print("Starting user mode trap test...\n");
  state_t e_state;
  STST(&e_state);
  e_state.reg_sp = get_new_stack();
  e_state.pc_epc = (memaddr)user_process;
  e_state.status =
      (e_state.status & ~MSTATUS_MPP_MASK) | MSTATUS_MPP_U | MSTATUS_MIE_MASK;
  e_state.mie = MIE_ALL;
  SYSCALL(CREATEPROCESS, (int)&e_state, 1, 0);

  SYSCALL(CLOCKWAIT, 0, 0, 0);
  if (user_survived) {
    print("ERROR: User process survived privileged syscall!\n");
    PANIC();
  }
  print("User mode trap test passed.\n");

  // 3. Support_t Trap Handling Test
  print("Starting support_t trap handling test...\n");
  f_support.sup_exceptContext[GENERALEXCEPT].stackPtr = get_new_stack();
  f_support.sup_exceptContext[GENERALEXCEPT].status |=
      MSTATUS_MIE_MASK | MSTATUS_MPP_M;
  f_support.sup_exceptContext[GENERALEXCEPT].pc = (memaddr)f_trap_handler;

  state_t f_state;
  STST(&f_state);
  f_state.reg_sp = get_new_stack();
  f_state.pc_epc = (memaddr)f_process;
  f_state.status |= MSTATUS_MIE_MASK | MSTATUS_MPP_M;
  f_state.mie = MIE_ALL;
  SYSCALL(CREATEPROCESS, (int)&f_state, 1, (int)&f_support);

  SYSCALL(CLOCKWAIT, 0, 0, 0);
  print("Support_t trap test completed.\n");

  // 4. Semaphore Queue Ordering Test
  print("Starting semaphore queue ordering test...\n");
  state_t qa, qb, qc;
  STST(&qa);
  qa.reg_sp = get_new_stack();
  qa.pc_epc = (memaddr)process_QA;
  qa.status |= MSTATUS_MIE_MASK | MSTATUS_MPP_M;
  qa.mie = MIE_ALL;
  STST(&qb);
  qb.reg_sp = get_new_stack();
  qb.pc_epc = (memaddr)process_QB;
  qb.status |= MSTATUS_MIE_MASK | MSTATUS_MPP_M;
  qb.mie = MIE_ALL;
  STST(&qc);
  qc.reg_sp = get_new_stack();
  qc.pc_epc = (memaddr)process_QC;
  qc.status |= MSTATUS_MIE_MASK | MSTATUS_MPP_M;
  qc.mie = MIE_ALL;

  SYSCALL(CREATEPROCESS, (int)&qa, 1, 0);
  SYSCALL(YIELD, 0, 0, 0);
  SYSCALL(CREATEPROCESS, (int)&qb, 1, 0);
  SYSCALL(YIELD, 0, 0, 0);
  SYSCALL(CREATEPROCESS, (int)&qc, 1, 0);
  SYSCALL(YIELD, 0, 0, 0);

  SYSCALL(VERHOGEN, (int)&sem_queue, 0, 0);
  SYSCALL(YIELD, 0, 0, 0);
  SYSCALL(VERHOGEN, (int)&sem_queue, 0, 0);
  SYSCALL(YIELD, 0, 0, 0);
  SYSCALL(VERHOGEN, (int)&sem_queue, 0, 0);
  SYSCALL(YIELD, 0, 0, 0);

  SYSCALL(CLOCKWAIT, 0, 0, 0);

  if (queue_order[0] != 1 || queue_order[1] != 2 || queue_order[2] != 3) {
    print("ERROR: Semaphore queue ordering is incorrect!\n");
    PANIC();
  }
  print("Semaphore queue ordering test passed.\n");

  // 5. IO Termination Test
  print("Starting IO termination test...\n");
  state_t b_io_state;
  STST(&b_io_state);
  b_io_state.reg_sp = get_new_stack();
  b_io_state.pc_epc = (memaddr)process_B_io;
  b_io_state.status |= MSTATUS_MIE_MASK | MSTATUS_MPP_M;
  b_io_state.mie = MIE_ALL;
  pid_B = SYSCALL(CREATEPROCESS, (int)&b_io_state, 1, 0);

  SYSCALL(CLOCKWAIT, 0, 0, 0);
  SYSCALL(CLOCKWAIT, 0, 0, 0);
  print("IO termination test passed (no crash upon returning to no waiting "
        "process).\n");

  // 6. CPUTIME stress test
  print("Starting GetCPUTime test...\n");
  cpu_t t1 = SYSCALL(GETTIME, 0, 0, 0);
  for (int i = 0; i < 1000; i++) {
    SYSCALL(YIELD, 0, 0, 0);
  }
  cpu_t t2 = SYSCALL(GETTIME, 0, 0, 0);
  if (t2 <= t1) {
    print("ERROR: GetCPUTime did not advance appropriately!\n");
    PANIC();
  }
  print("GetCPUTime test passed.\n");

  print("All Superhard Tests Passed Successfully!\n");
  SYSCALL(TERMPROCESS, 0, 0, 0);
}
