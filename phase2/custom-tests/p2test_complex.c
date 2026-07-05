#include "../../headers/const.h"
#include "../../headers/types.h"
#include <uriscv/liburiscv.h>

#define QPAGE 1024
#define TERM0ADDR 0x10000254
#define PRINTCHR 2
#define RECVD 5

#define MSTATUS_MIE_MASK 0x8
#define MIE_ALL 0xFFFFFFFF
#define MSTATUS_MPP_M 0x1800

int sem_term_mut = 1;
int sem_prod_cons_mutex = 1;
int sem_items = 0;
int sem_spaces = 10;
int buffer[10];
int in = 0, out = 0;
int produced_count = 0;
int consumed_count = 0;

int test_finished = 0;

void print(char *msg) {
  char *s = msg;
  unsigned int *base = (unsigned int *)(TERM0ADDR);
  unsigned int *command = base + 3;
  unsigned int status;

  SYSCALL(PASSEREN, (int)&sem_term_mut, 0, 0);
  while (*s != '\0') {
    unsigned int value = PRINTCHR | (((unsigned int)*s) << 8);
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

void producer() {
    for (int i = 0; i < 20; i++) {
        SYSCALL(PASSEREN, (int)&sem_spaces, 0, 0);
        SYSCALL(PASSEREN, (int)&sem_prod_cons_mutex, 0, 0);
        
        buffer[in] = i + 1;
        in = (in + 1) % 10;
        produced_count++;
        
        SYSCALL(VERHOGEN, (int)&sem_prod_cons_mutex, 0, 0);
        SYSCALL(VERHOGEN, (int)&sem_items, 0, 0);
    }
    print("[SUCCESS] Producer finished.\n");
    SYSCALL(TERMPROCESS, 0, 0, 0);
}

void consumer() {
    for (int i = 0; i < 20; i++) {
        SYSCALL(PASSEREN, (int)&sem_items, 0, 0);
        SYSCALL(PASSEREN, (int)&sem_prod_cons_mutex, 0, 0);
        
        int item = buffer[out];
        out = (out + 1) % 10;
        consumed_count++;
        
        SYSCALL(VERHOGEN, (int)&sem_prod_cons_mutex, 0, 0);
        SYSCALL(VERHOGEN, (int)&sem_spaces, 0, 0);
    }
    print("[SUCCESS] Consumer finished.\n");
    SYSCALL(VERHOGEN, (int)&test_finished, 0, 0);
    SYSCALL(TERMPROCESS, 0, 0, 0);
}

void test() {
    print("Starting complex test: Producer/Consumer\n");
    
    state_t p_state, c_state;
    
    STST(&p_state);
    p_state.reg_sp = p_state.reg_sp - QPAGE;
    p_state.pc_epc = (memaddr)producer;
    p_state.status |= MSTATUS_MIE_MASK | MSTATUS_MPP_M;
    p_state.mie = MIE_ALL;
    
    STST(&c_state);
    c_state.reg_sp = p_state.reg_sp - QPAGE;
    c_state.pc_epc = (memaddr)consumer;
    c_state.status |= MSTATUS_MIE_MASK | MSTATUS_MPP_M;
    c_state.mie = MIE_ALL;
    
    int pid_p = SYSCALL(CREATEPROCESS, (int)&p_state, 1, 0);
    int pid_c = SYSCALL(CREATEPROCESS, (int)&c_state, 1, 0);
    
    if (pid_p < 0 || pid_c < 0) {
        print("[ERROR] Failed to create processes.\n");
        PANIC();
    }
    
    SYSCALL(PASSEREN, (int)&test_finished, 0, 0);
    
    if (produced_count == 20 && consumed_count == 20) {
        print("[SUCCESS] All items produced and consumed correctly.\n");
    } else {
        print("[ERROR] Mismatch in production/consumption counts.\n");
        print("Produced: "); print_int(produced_count); print("\n");
        print("Consumed: "); print_int(consumed_count); print("\n");
        PANIC();
    }
    
    print("Complex test completed successfully.\n");
    SYSCALL(TERMPROCESS, 0, 0, 0);
}
