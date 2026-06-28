#include <uriscv/liburiscv.h>

#include "../headers/print.h"
#include "../headers/tconst.h"

void main() {
  print(WRITETERMINAL, "PandOSsh\n");
  SYSCALL(TERMINATE, 0, 0, 0);
}
