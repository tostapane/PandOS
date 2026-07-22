/* Stress test for the Support Level paging subsystem (the Pager).
 *
 * bigArray spans 24 pages, more than the 16 frames of the Swap Pool
 * (POOLSIZE = 2 * UPROCMAX), so every full sweep is guaranteed to
 * evict dirty pages to the backing store and fault them back in.
 * Three passes with a different seed each catch stale write-backs,
 * the reverse and strided sweeps defeat a FIFO replacement policy,
 * and the recursion at the end alternates stack-page and data-page
 * accesses. */

#include <uriscv/liburiscv.h>

#include "../headers/print.h"
#include "../headers/tconst.h"
#include "../headers/utils.h"

#define PAGES 24
#define INTS_PER_PAGE 1024 /* 4KB page / sizeof(int) */
#define STRIDE 64
#define PASSES 3
#define DEPTH 16

int bigArray[PAGES * INTS_PER_PAGE];

int expected(int p, int j, int pass) {
  return (p << 16) + (j << 4) + pass;
}

int touchRec(int depth) {
  if (depth == 0)
    return 0;
  return bigArray[(depth % PAGES) * INTS_PER_PAGE] + touchRec(depth - 1);
}

void main() {
  int pass, p, j, errors = 0;
  char numBuf[12];

  print(WRITETERMINAL, "vmStress: paging stress test starts\n");

  for (pass = 1; pass <= PASSES; pass++) {
    /* dirty every page, sequential order */
    for (p = 0; p < PAGES; p++)
      for (j = 0; j < INTS_PER_PAGE; j += STRIDE)
        bigArray[p * INTS_PER_PAGE + j] = expected(p, j, pass);

    /* verify in reverse order: the pages written first have been
     * evicted by now, so this faults them back in from the flash */
    for (p = PAGES - 1; p >= 0; p--)
      for (j = 0; j < INTS_PER_PAGE; j += STRIDE)
        if (bigArray[p * INTS_PER_PAGE + j] != expected(p, j, pass))
          errors++;

    /* strided sweep across pages */
    for (j = 0; j < PAGES; j++) {
      p = (j * 7) % PAGES;
      if (bigArray[p * INTS_PER_PAGE] != expected(p, 0, pass))
        errors++;
    }

    print(WRITETERMINAL, "vmStress: pass ");
    itoa(pass, numBuf);
    print(WRITETERMINAL, numBuf);
    print(WRITETERMINAL, " done\n");
  }

  /* alternate faults on the stack page and on data pages */
  {
    int want = 0;
    for (j = 1; j <= DEPTH; j++)
      want += expected(j, 0, PASSES);
    if (touchRec(DEPTH) != want)
      errors++;
  }

  if (errors == 0)
    print(WRITETERMINAL, "vmStress: SUCCESS, memory consistent after thrashing\n");
  else {
    print(WRITETERMINAL, "vmStress: ERROR, corrupted locations: ");
    itoa(errors, numBuf);
    print(WRITETERMINAL, numBuf);
    print(WRITETERMINAL, "\n");
  }

  /* Terminate normally */
  SYSCALL(TERMINATE, 0, 0, 0);
}
