/* p2test_massive_logged.c
 * Comprehensive Stress Test for PandOSsh Phase 2 with explicit correctness
 * logging.
 */

#include "../../headers/const.h"
#include "../../headers/types.h"
#include <uriscv/liburiscv.h>

typedef unsigned int devregtr;

#define PRINTCHR 2
#define RECVD 5
#define QPAGE 1024
#define TERM0ADDR 0x10000254
#define TERM1ADDR 0x1000026C
#define TERM2ADDR 0x10000284
#define LOOPNUM 50000
#define CLOCKINTERVAL 100000UL

#define MSTATUS_MIE_MASK 0x8
#define MIE_ALL 0xFFFFFFFF
#define MSTATUS_MPP_M 0x1800

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

int sem_term_mut = 1;
int sem_test_A_done = 0;
int sem_test_B_sync = 0;
int sem_test_B_barrier = 0;
int sem_test_C_done = 0;
int test_B_count = 0;

int pid_A1, pid_A2, pid_A3;
state_t state_A1, state_A2, state_A3;
state_t state_B[15];
state_t state_C1, state_C2;

void test_A1();
void test_A2();
void test_A3();
void test_B_worker();
void test_C1_io();
void test_C2_cpu();

void print(char *msg) {
  char *s = msg;
  devregtr *base = (devregtr *)(TERM0ADDR);
  devregtr *command = base + 3;
  devregtr status;

  SYSCALL(PASSEREN, (int)&sem_term_mut, 0, 0);
  while (*s != EOS) {
    devregtr value = PRINTCHR | (((devregtr)*s) << 8);
    status = SYSCALL(DOIO, (int)command, (int)value, 0);
    if ((status & 0xFF) != RECVD) {
      PANIC();
    }
    s++;
  }
  SYSCALL(VERHOGEN, (int)&sem_term_mut, 0, 0);
}

void print_int(int num) {
  char buf[12];
  int i = 10;
  buf[11] = '\0';
  if (num == 0) {
    print("0");
    return;
  }
  int is_neg = 0;
  if (num < 0) {
    is_neg = 1;
    num = -num;
  }
  while (num > 0) {
    buf[i--] = (num % 10) + '0';
    num /= 10;
  }
  if (is_neg) {
    buf[i--] = '-';
  }
  print(&buf[i + 1]);
}

void uTLB_RefillHandler() {
  setENTRYHI(0x80000000);
  setENTRYLO(0x00000000);
  TLBWR();
  LDST((state_t *)BIOSDATAPAGE);
}

void test() {
  print("Main: Massive Test Suite Started.\n");

  /* TEST A */
  print("Main: [TEST A] Initiating Deep Tree Termination Test.\n");

  STST(&state_A1);
  state_A1.reg_sp = state_A1.reg_sp - QPAGE;
  state_A1.pc_epc = (memaddr)test_A1;
  state_A1.status |= MSTATUS_MIE_MASK | MSTATUS_MPP_M;
  state_A1.mie = MIE_ALL;

  pid_A1 = SYSCALL(CREATEPROCESS, (int)&state_A1, 1, (int)NULL);
  if (pid_A1 < 0) {
    print("Main: [ERROR] Failed to create A1.\n");
    PANIC();
  }
  print("Main: [TEST A] Successfully created A1 with PID: ");
  print_int(pid_A1);
  print("\n");

  SYSCALL(PASSEREN, (int)&sem_test_A_done, 0, 0);
  print(
      "Main: [TEST A] Sequence complete. Cascading termination successful.\n");

  /* TEST B */
  print("Main: [TEST B] Initiating ASL Capacity & Mutex Test.\n");

  int i;
  int base_sp = state_A1.reg_sp;
  for (i = 0; i < 15; i++) {
    STST(&state_B[i]);
    state_B[i].reg_sp = base_sp - (QPAGE * (i + 1));
    state_B[i].pc_epc = (memaddr)test_B_worker;
    state_B[i].status |= MSTATUS_MIE_MASK | MSTATUS_MPP_M;
    state_B[i].mie = MIE_ALL;

    int pid = SYSCALL(CREATEPROCESS, (int)&state_B[i], 1, (int)NULL);
    if (pid < 0) {
      print("Main: [ERROR] Failed to create B worker index ");
      print_int(i);
      print("\n");
      PANIC();
    }
  }
  print("Main: [TEST B] Successfully created 15 worker processes.\n");

  SYSCALL(YIELD, 0, 0, 0);

  print("Main: [TEST B] Unblocking all 15 workers.\n");
  for (i = 0; i < 15; i++) {
    SYSCALL(VERHOGEN, (int)&sem_test_B_barrier, 0, 0);
  }

  print("Main: [TEST B] Waiting for all workers to synchronize termination.\n");
  for (i = 0; i < 15; i++) {
    SYSCALL(PASSEREN, (int)&sem_test_B_sync, 0, 0);
  }

  print("Main: [TEST B] Expected mutex count: 15. Actual count: ");
  print_int(test_B_count);
  print("\n");
  if (test_B_count != 15) {
    print("Main: [ERROR] ASL concurrency failure. Count mismatch.\n");
    PANIC();
  }
  print("Main: [TEST B] ASL Stress Test passed.\n");

  /* TEST C */
  print("Main: [TEST C] Initiating Pseudo-Clock and I/O Test.\n");

  STST(&state_C1);
  state_C1.reg_sp = state_B[14].reg_sp - QPAGE;
  state_C1.pc_epc = (memaddr)test_C1_io;
  state_C1.status |= MSTATUS_MIE_MASK | MSTATUS_MPP_M;
  state_C1.mie = MIE_ALL;

  STST(&state_C2);
  state_C2.reg_sp = state_C1.reg_sp - QPAGE;
  state_C2.pc_epc = (memaddr)test_C2_cpu;
  state_C2.status |= MSTATUS_MIE_MASK | MSTATUS_MPP_M;
  state_C2.mie = MIE_ALL;

  SYSCALL(CREATEPROCESS, (int)&state_C1, 1, (int)NULL);
  SYSCALL(CREATEPROCESS, (int)&state_C2, 1, (int)NULL);
  print("Main: [TEST C] C1 and C2 created.\n");

  SYSCALL(PASSEREN, (int)&sem_test_C_done, 0, 0);
  SYSCALL(PASSEREN, (int)&sem_test_C_done, 0, 0);
  print("Main: [TEST C] Timing and I/O complete.\n");

  print("Main: All Tests Passed. System Halted.\n");
  *((memaddr *)0xFFFFFFFF) = 0;
}

/* --- Test A Functions --- */
void test_A1() {
  STST(&state_A2);
  state_A2.reg_sp = state_A1.reg_sp - QPAGE;
  state_A2.pc_epc = (memaddr)test_A2;
  state_A2.status |= MSTATUS_MIE_MASK | MSTATUS_MPP_M;
  state_A2.mie = MIE_ALL;

  pid_A2 = SYSCALL(CREATEPROCESS, (int)&state_A2, 1, (int)NULL);

  SYSCALL(YIELD, 0, 0, 0);

  SYSCALL(TERMPROCESS, pid_A2, 0, 0);

  print("A1: A2 and its progeny correctly terminated. Signaling Main.\n");
  SYSCALL(VERHOGEN, (int)&sem_test_A_done, 0, 0);
  SYSCALL(TERMPROCESS, 0, 0, 0);
}

void test_A2() {
  STST(&state_A3);
  state_A3.reg_sp = state_A2.reg_sp - QPAGE;
  state_A3.pc_epc = (memaddr)test_A3;
  state_A3.status |= MSTATUS_MIE_MASK | MSTATUS_MPP_M;
  state_A3.mie = MIE_ALL;

  pid_A3 = SYSCALL(CREATEPROCESS, (int)&state_A3, 1, (int)NULL);

  SYSCALL(YIELD, 0, 0, 0);

  print("A2: [ERROR] I should have been terminated by A1!\n");
  PANIC();
}

void test_A3() {
  SYSCALL(YIELD, 0, 0, 0);
  print(
      "A3: [ERROR] I should have been terminated cascadingly when A2 died!\n");
  PANIC();
}

/* --- Test B Functions --- */
int b_mutex = 1;
void test_B_worker() {
  int my_pid = SYSCALL(GETPROCESSID, 0, 0, 0);
  SYSCALL(PASSEREN, (int)&sem_test_B_barrier, 0, 0);

  SYSCALL(PASSEREN, (int)&b_mutex, 0, 0);
  test_B_count++;
  print("B_Worker (PID: ");
  print_int(my_pid);
  print("): Mutex acquired. Counter is now: ");
  print_int(test_B_count);
  print("\n");
  SYSCALL(VERHOGEN, (int)&b_mutex, 0, 0);

  SYSCALL(VERHOGEN, (int)&sem_test_B_sync, 0, 0);
  SYSCALL(TERMPROCESS, 0, 0, 0);
}

/* --- Test C Functions --- */
void test_C1_io() {
  print("C1: Initiating DOIO on Terminal 2.\n");
  devregtr *base = (devregtr *)(TERM2ADDR);
  devregtr *command = base + 3;
  devregtr value = PRINTCHR | (((devregtr)'Y') << 8);

  devregtr status = SYSCALL(DOIO, (int)command, (int)value, 0);
  if ((status & 0xFF) != RECVD) {
    print("C1: [ERROR] Terminal 2 DOIO failed. Status: ");
    print_int(status);
    print("\n");
    PANIC();
  }
  print("C1: DOIO finished correctly with correct RECVD status.\n");
  SYSCALL(VERHOGEN, (int)&sem_test_C_done, 0, 0);
  SYSCALL(TERMPROCESS, 0, 0, 0);
}

void test_C2_cpu() {
  print("C2: Testing Pseudo-clock.\n");
  cpu_t time1, time2;
  STCK(time1);

  SYSCALL(CLOCKWAIT, 0, 0, 0);

  STCK(time2);
  cpu_t delta = time2 - time1;
  print("C2: CLOCKWAIT delta (real time): ");
  print_int(delta);
  print("\n");

  if (delta < (CLOCKINTERVAL / 2)) {
    print("C2: [ERROR] CLOCKWAIT returned too early.\n");
    PANIC();
  }
  print("C2: CLOCKWAIT duration is correct.\n");

  print("C2: Testing CPU Time Accounting.\n");
  cpu_t cpu_start = SYSCALL(GETTIME, 0, 0, 0);
  int i;
  for (i = 0; i < LOOPNUM; i++) {
  }
  cpu_t cpu_end = SYSCALL(GETTIME, 0, 0, 0);

  print("C2: CPU Time delta: ");
  print_int(cpu_end - cpu_start);
  print("\n");
  if (cpu_end <= cpu_start) {
    print("C2: [ERROR] CPU time did not increase.\n");
    PANIC();
  }

  print("C2: Clock and CPU time OK. Signaling Main.\n");
  SYSCALL(VERHOGEN, (int)&sem_test_C_done, 0, 0);
  SYSCALL(TERMPROCESS, 0, 0, 0);
}
