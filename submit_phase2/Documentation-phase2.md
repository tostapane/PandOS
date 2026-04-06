# PandOSsh Phase 2: Level 2 Documentation

## 1. Project Overview

Phase 2 implements the **Nucleus layer** (Level 3) of the PandOSsh operating system. This layer is responsible for process scheduling, exception and interrupt handling, and providing core system services. It establishes a multi-programmed environment where sequential processes share the processor.

The Nucleus serves as the intermediary between the hardware/BIOS and the higher-level Support Level, providing:

- **System Initialization**: Bootstrapping the Nucleus and starting the first process.
- **Scheduling**: A preemptive round-robin scheduler to manage CPU time.
- **Exception Dispatching**: A centralized handler for interrupts, SYSCALLs, and program traps.
- **System Services**: Low-level synchronization primitives and I/O facilities.
- **Pass Up Policy**: A facility for forwarding unhandled exceptions to the Support Level.

## 2. Global Variables and Core Components

The Nucleus manages the system state using several key variables:

- **`processCount`**: Integer tracking all started processes that haven't terminated.
- **`softBlockCount`**: Integer counting processes blocked waiting for I/O or timer events.
- **`readyQueue`**: A queue of PCBs in the "ready" state, managed by priority.
- **`currProc`**: Pointer to the PCB currently executing on the processor.
- **`subDevice`**: An array of 49 semaphores: 8 disks, 8 flash, 8 network, 8 printers, 16 terminal sub-devices (8 RX, 8 TX), and 1 Pseudo-clock semaphore.

## 3. Module: System Initialization (`initial.c`)

Initialization occurs at boot time:

1. **Pass-Up Vector**: Populates the vector at `0x0FFFF900`. It registers `uTLB_RefillHandler` (which acts as a stub provided externally by the test suite `p2test` to prevent multiple definition conflicts) and the local `exceptionHandler`. Both handlers use `KERNELSTACK` (`0x20001000`).
2. **Data Structures**: Initializes PCB and ASL pools via `initPcbs()` and `initASL()`.
3. **Global State**: Resets counters, clears the `readyQueue`, and sets `currProc` to `NULL`. Device semaphores are initialized to zero.
4. **Interval Timer**: Loads the system-wide timer with `PSECOND` (100ms) to drive the Pseudo-clock.
5. **Root Process**: Allocates the first PCB, setting its state to kernel-mode, interrupts enabled, SP to `RAMTOP`, and PC to the `test` function.
6. **Start**: Adds the root process to the `readyQueue`, increments `processCount`, and calls the `scheduler()`.

## 4. Module: Scheduler (`scheduler.c`)

Implements a preemptive round-robin scheduling algorithm with a 5ms `TIMESLICE`.

- **Dispatch**: Removes the head PCB from `readyQueue`, sets the Processor Local Timer (PLT) to 5ms, and performs `LDST` on the process state.
- **Idle/Halt States**:
  - **Halt**: If `processCount == 0`, the system calls `HALT()`.
  - **Wait**: If `processCount > 0` and `softBlockCount > 0`, the CPU enables interrupts (disabling PLT) and enters a `WAIT()` state.
  - **Panic**: If `processCount > 0` and `softBlockCount == 0`, a deadlock is detected and `PANIC()` is called.

## 5. Module: Exception Handling (`exceptions.c`)

### Exception Dispatcher (`exceptionHandler`)

The entry point for all exceptions (except TLB-Refill). It determines the cause from the state saved at `BIOSDATAPAGE`:

- **Interrupts**: Delegates to `interruptHandler()`.
- **SYSCALLs (ExcCode 8, 11)**: Delegates to `syscallHandler()`.
- **TLB Exceptions (ExcCode 24-28)**: Delegates to `tlbHandler()`.
- **Program Traps (ExcCode 0-7, 9, 10, 12-23)**: Delegates to `programTrapHandler()`.

### Architectural Oddities & Design Decisions

- **Handling TLB-Refill**: While the specification formally necessitates a stub for `uTLB_RefillHandler` located in `exceptions.c`, its body has been purposefully omitted from the final codebase to bypass fatal `Multiple Definition` linker errors, given that the provided `p2test.c` validation suite already injects its own duplicate prototype during compilation.
- **Timer CPU Accounting Anomaly**: A known semantic anomaly is retained inside `PLTInterrupt()`. The pointer `currProc` is nullified proactively right before the elapsed CPU time metric (`p_time`) is calculated and attempted to be summed. As a result, the time delta for the preempted process gets entirely discarded instead of accumulated. This is documented internally but untouched to preserve strict code invariance rules.

### SYSCALL Processing (`syscallHandler`)

- **Security**: Privileged SYSCALLs (-1 to -10) called in User Mode trigger a simulated Program Trap with the cause set to `PRIVINSTR`.
- **PC Management**: To avoid infinite loops, the saved PC is incremented by 4 for all SYSCALLs that do not cause immediate process termination.
- **Implemented Services**:
  - `CREATEPROCESS (-1)`: Creates a child process and returns its PID.
  - `TERMPROCESS (-2)`: Terminates a process (the caller if PID=0) and all its progeny.
  - `PASSEREN (-3)` / `VERHOGEN (-4)`: P and V operations on physical semaphores.
  - `DOIO (-5)`: Performs a P on a device semaphore and initiates I/O. This call is always blocking.
  - `GETCPUTIME (-6)`: Returns the total CPU time used by the process.
  - `CLOCKWAIT (-7)`: Blocks the process on the Pseudo-clock semaphore until the next 100ms tick.
  - `GETSUPPORTPTR (-8)`: Returns the process's support structure pointer.
  - `GETPROCESSID (-9)`: Returns the PID of the caller or its parent.
  - `YIELD (-10)`: Voluntarily relinquishes the CPU, moving the process to the end of the `readyQueue`.

### Pass Up or Die Policy

For Program Traps and TLB exceptions:

- **Pass Up**: If a `p_supportStruct` exists, the exception state is saved and control is passed to the Support Level via `LDCXT`.
- **Die**: If no support structure exists, the process and its progeny are terminated.

## 6. Module: Interrupt Handling (`interrupts.c`)

Processes pending interrupts in priority order (lower line/device number first).

- **PLT (Line 1, ExcCode 7)**: Timeslice expired. The current process is returned to the `readyQueue`.
- **Interval Timer (Line 2, ExcCode 3)**: Pseudo-clock tick. Unblocks all processes waiting on the clock semaphore.
- **Devices (Lines 3-7, ExcCodes 17-21)**:
  1. Identifies the interrupting device via the Bit Map.
  2. Acknowledges the interrupt by writing `ACK` to the device register.
  3. Performs a V operation on the device semaphore, unblocking the process and returning the completion status in `a0`.
