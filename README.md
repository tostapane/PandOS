# PandOS (µRISCV)

A small multi-layer operating system kernel written in C for **µRISCV**,
an emulated RV32IMFD machine with a TLB-based MMU and memory-mapped devices
(disk, flash, printer, terminal). There is no libc and no host OS. Everything from
the process queues up to demand paging and the user shell is in this repository.

This was a 4-person group project for the Operating Systems course at the University of
Bologna (2025–26). The specification comes from the course. The implementation is ours.

## What it does

The system boots, sets up the kernel and the support level, and loads an interactive
shell from flash device 0 as a virtual-memory user process. From the shell
(`PandOSsh>>`) you can start the other user programs (`calc`, `sl`, `date`, `echo`,
`fibEight`, `fibEleven`, `uname`). Each one is loaded on demand from its own flash device.
`exit` shuts the machine down cleanly (`HALT`).

## Architecture

- **Phase 1: data structures** (`phase1/pcb.c`, `phase1/asl.c`):
  Static pool of 20 PCBs. Priority-ordered process queues and process trees, built on
  intrusive Linux-style lists (`headers/listx.h`). Active Semaphore List, sorted by
  semaphore address.
- **Phase 2: nucleus** (`phase2/initial.c`, `scheduler.c`, `exceptions.c`, `interrupts.c`):
  Boot and pass-up vector setup. Preemptive scheduler with two priority levels and a 5 ms
  time slice on the per-CPU timer (PLT). Exception dispatch. 10 kernel syscalls. Interrupt
  handling for the PLT, the 100 ms interval timer (pseudo-clock) and the device lines.
  Deadlock detection, idle `WAIT`, and "pass up or die" delegation to the support level.
- **Phase 3: support level** (`phase3/initProc.c`, `vmSupport.c`, `sysSupport.c`):
  Per-process virtual memory (32 pages: 31 text/data pages at `0x80000000` and 1 stack
  page at `0xBFFFF000`). TLB refill handler. Pager with a 16-frame swap pool, FIFO
  replacement and flash as backing store. User syscalls. The process instantiator and the
  shell launcher.
- **User programs** (`phase3/testers/*.c`):
  The shell and the test programs, linked as separate a.out images and written onto
  flash devices.

### Kernel syscalls (negative numbers, kernel mode only)

`CREATEPROCESS -1`, `TERMPROCESS -2` (also kills the whole subtree), `PASSEREN -3`,
`VERHOGEN -4`, `DOIO -5` (synchronous device I/O), `GETTIME -6`, `CLOCKWAIT -7`,
`GETSUPPORTPTR -8`, `GETPROCESSID -9`, `YIELD -10`.
If a user-mode process calls one of these, it gets a privileged-instruction trap.

### Support-level syscalls (positive numbers, called from user processes)

`TERMINATE 2`, `WRITETERMINAL 4`, `READTERMINAL 5`, `EXECUTE 6`. Only the shell
(ASID 1) may call `EXECUTE`. It loads the program for an ASID in 2–8 from flash `ASID-1`.

## Implementation details

- **Device sync:** each device has a semaphore (49 total: 40 devices, plus 8 for the
  second sub-device of each terminal, plus the pseudo-clock). The interrupt handler
  acknowledges the device, does a V on its semaphore, and writes the device status into the
  waiting process's `a0`. `softBlockCount` tracks processes blocked on I/O, so the
  scheduler can tell a real deadlock (`PANIC`) from an idle wait (`WAIT`, with the PLT masked).
- **Address-space setup:** each new user process's a.out header is read from flash. Its
  `.text` pages get D=0, which makes them read-only, and its data/stack pages get D=1.
- **Paging:** the swap pool sits right after the kernel's `.data`, page-aligned. A mutex
  protects it. Evictions update the victim's PTE and refresh its TLB entry with
  `TLBP`/`TLBWI` while interrupts are disabled, so the whole TLB is never flushed.
- **Handler stacks:** each process's support-level handler stacks are carved from RAM
  below `RAMTOP`, two pages per ASID, instead of living inside the support struct.
- **CPU-time accounting:** time is charged per process with the TOD clock, and the time
  spent in the kernel on a blocking syscall counts toward the caller.

## Build

Requirements:

- the `riscv64-unknown-elf-` GCC/binutils toolchain
- [µRISCV](https://github.com/virtualsquare/uriscv) installed under `/usr/local`

```sh
./build.sh          # cmake -B build && cmake --build build && make -C phase3/testers
```

This produces `build/MultiPandOS.core.uriscv` and `build/MultiPandOS.stab.uriscv`, the
kernel image and its symbol table, plus one `phase3/testers/*.uriscv` flash image per
program.

The µRISCV header path depends on your distro. Set `URISCV_INC` in `CMakeLists.txt`
(Arch is the default) and `URISCV_INCLUDE_DIR`/`URISCV_LIB_DIR` in
`phase3/testers/Makefile` (Debian is the default) to match your system.

## Run

Start `uriscv` from the repository root, open `config_machine.json`, power on the machine,
and open terminal 0. The config maps the kernel image, flash devices 0–7 and the terminal
with relative paths. It defines 1 CPU, 256 RAM frames and a 16-entry TLB.

## Tests

The default build runs phase 3. The test drivers for the lower layers are kept in the
repository but are not built:

- `phase1/p1test.c`: the course test for the PCB and ASL modules.
- `phase2/p2test.c`: the course test for the nucleus. `phase2/custom-tests/` has our
  extra stress tests (deep process trees, termination while blocked, panic paths).

To run a nucleus test, replace the three `phase3/*.c` sources in `CMakeLists.txt` with
the test file. The test file provides `test()`.

## Known limitations

- The kernel runs on one CPU. Some multi-CPU constants exist but SMP is not implemented.
- `EXECUTE` blocks the shell until the child exits, so only one program runs at a time
  on top of the shell.
- The kernel has no nested exceptions: the exception handler and the TLB refill handler
  share a single kernel stack.
- Built with `-O0 -ggdb` to make debugging in the simulator easier.
