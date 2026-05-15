#include <uriscv/liburiscv.h>

#include "../headers/print.h"
#include "../headers/tconst.h"

void main() {
  print(WRITETERMINAL, "Sat  5 Nov 06:15:00 PST 1955\n");
  SYSCALL(TERMINATE, 0, 0, 0);
}
