# PandOSsh Phase 3: Level 4 Documentation

## 1. Project Overview

Phase 3 implements the **Support Level** (Level 4) of the PandOSsh operating system. This layer sits above the Nucleus (Phase 2) and provides the infrastructure for running isolated, demand-paged **User-mode processes (U-procs)** with virtual memory.

The Support Level provides:

- **Instantiator Process**: Bootstraps the Swap Pool, support structures, and launches the shell U-proc.
- **Virtual Memory Management (Pager)**: A demand-paging TLB exception handler that manages a shared Swap Pool backed by per-process flash devices.
- **Support-Level System Services**: Higher-level syscalls (`WRITETERMINAL`, `READTERMINAL`, `EXECUTE`, `TERMINATE`) exposed to U-procs via the `GeneralExceptionHandler`.
- **User-Space Shell**: A shell process (`PandOSsh>> `) that reads commands from Terminal 0 and spawns child U-proc programs via `EXECUTE`.

## 2. Global Variables and Core Components

The Support Level manages shared state through the following key variables (defined in `initProc.c`, exported via `initProc.h`):

- **`swapPool[POOLSIZE]`**: Array of `swap_t` descriptors tracking the content of each RAM frame in the Swap Pool. Each entry stores the owning ASID (`sw_asid`), the logical page number (`sw_pageNo`), and a pointer to the corresponding PTE (`sw_pte`). A value of `-1` in `sw_asid` marks the frame as free.
- **`swapSemaphore`**: Binary mutex (initialized to 1) that serializes access to the Swap Pool across all concurrent U-procs.
- **`masterSemaphore`**: Semaphore (initialized to 0) on which the Instantiator blocks after launching the shell; the shell performs a V on it when it terminates, triggering system shutdown.
- **`shellSemaphore`**: Semaphore (initialized to 0) used to synchronize the shell with its child U-proc during `EXECUTE`. The shell blocks after spawning a child and is unblocked when the child terminates.
- **`devSemaphores[NSUPPSEM]`**: Array of 49 binary mutexes (each initialized to 1) for peripheral I/O: 8 disk, 8 flash, 8 network, 8 printer, 16 terminal sub-devices (8 RX + 8 TX).
- **`supStructs[UPROCMAX]`**: Static pool of up to 8 `support_t` structures, managed via a free list (`supStructs_freeList`).
- **`page_mutex_holder`**: Stores the ASID of the U-proc currently holding `swapSemaphore`, used to avoid self-deadlock in `ProgramTrapHandler` when the Pager invokes it after already acquiring the mutex.

---

## 3. Module: Instantiator Process (`initProc.c`)

The `test()` function serves as the Instantiator Process, invoked by the Nucleus as the root process.

### Support Structure Management

- **`allocateSupport()`**: Removes and returns the head of `supStructs_freeList`. Returns `NULL` if no structures are available.
- **`deallocateSupport(s)`**: Prepends a `support_t` back to `supStructs_freeList`.

### Initialization Sequence (`test`)

1. **Swap Pool**: Calls `initSwapStructs()` to mark all frames as free and compute `swapPoolBase` from the kernel's a.out header.
2. **Device Semaphores**: Initializes all 49 `devSemaphores` to 1 (mutual exclusion).
3. **Support Structures**: Initializes `supStructs_freeList` and populates it with all entries of `supStructs`.
4. **Synchronization**: Sets `masterSemaphore = 0`, `shellSemaphore = 0`, and `page_mutex_holder = -1`.
5. **Shell Header Read**: Reads the a.out header of the shell image from Flash Device 0 (ASID 1) into `shellHeaderBuf` using a `DOIO` syscall. Extracts `textSize` to determine read-only vs. writable pages.
6. **Shell Support Structure**: Allocates a `support_t` for the shell (ASID 1), sets exception contexts for the Pager (Context 0, stack at `RAMTOP - PAGESIZE`) and `GeneralExceptionHandler` (Context 1, stack at `RAMTOP - 2*PAGESIZE`), and initializes the private page table with correct DIRTY bits.
7. **Launch**: Calls `CREATEPROCESS` to spawn the shell, then blocks on `masterSemaphore` via `PASSEREN`.
8. **Shutdown**: After the shell terminates and unblocks the Instantiator, calls `TERMPROCESS` to cause the system to `HALT`.

### Page Table Initialization

For each U-proc (ASID `k`):
- Pages `[0, numTextPages)` are initialized **read-only** (`pte_entryLO = 0`): these cover the `.text` segment.
- Pages `[numTextPages, MAXPAGES-2]` are **writable** (`DIRTYON`): these cover `.data` and `.bss`.
- The **stack page** (virtual address `0xBFFFF000`) is always writable (`DIRTYON`).

---

## 4. Module: Virtual Memory - The Pager (`vmSupport.c`)

### Swap Pool Initialization (`initSwapStructs`)

Initializes `swapPool` entries to free (`sw_asid = -1`), sets `fifo_ptr = 0`, and computes `swapPoolBase` dynamically from the kernel a.out header (fields `AOUT_HE_DATA_VADDR` and `AOUT_HE_DATA_MEMSZ`/`AOUT_HE_DATA_FILESZ`), placing the Swap Pool immediately after the kernel image - avoiding hard-coding a fixed offset.

### TLB Exception Handler (`Pager`)

Entry point for TLB exceptions forwarded from the Nucleus to the Support Level (Context 0 of a U-proc's support structure).

1. **TLB-Modification Trap**: If `excCode == EXC_TLBMOD` (24), a write to a read-only page has occurred. Delegates immediately to `ProgramTrapHandler` - this is a fatal error for the U-proc.
2. **Acquire Mutex**: Performs a `PASSEREN` on `swapSemaphore`. Records ASID in `page_mutex_holder` to prevent re-locking in `ProgramTrapHandler`.
3. **Page Identification**: Extracts the missing VPN from `entry_hi` of the saved state. Maps it to a `pageIndex` in `[0, MAXPAGES-1]`. Invalid addresses outside the U-proc's logical space trigger `ProgramTrapHandler`.
4. **Frame Selection**: First scans for a free frame (`sw_asid == -1`). If none exists, applies **FIFO replacement** via `fifo_ptr`.
5. **Victim Eviction** (if frame occupied):
   - Disables interrupts to atomically update the victim's PTE: clears the `VALID` bit.
   - Probes the TLB (`TLBP`): if the entry is present, writes the invalidated PTE back (`TLBWI`).
   - Re-enables interrupts.
   - Writes the victim page to its owner's flash device using `DOIO` with `FLASHWRITE`, serialized by the appropriate `devSemaphores` entry. On I/O failure, calls `ProgramTrapHandler`.
6. **Page-In**: Reads the requested page from the current U-proc's flash device into the chosen frame using `DOIO` with `FLASHREAD`. On failure, calls `ProgramTrapHandler`.
7. **PTE Update**: Updates `swapPool` metadata (`sw_asid`, `sw_pageNo`, `sw_pte`). Disables interrupts to atomically set the PTE's `VALID` bit and physical frame address (preserving `DIRTY`). Updates TLB with `TLBP`/`TLBWI`.
8. **Release Mutex**: Clears `page_mutex_holder`, calls `VERHOGEN` on `swapSemaphore`.
9. **Resume**: Returns control to the faulting U-proc via `LDST`.

---

## 5. Module: Support-Level Exception Handler (`sysSupport.c`)

### General Exception Dispatcher (`GeneralExceptionHandler`)

Entry point for non-TLB exceptions forwarded to the Support Level (Context 1). Reads `cause` from `sup_exceptState[1]`, extracts `excCode`, and dispatches to:

- **`SyscallExceptionHandler`** if `excCode == SYSEXCEPTION`.
- **`ProgramTrapHandler`** for all other traps.

### SYSCALL Handler (`SyscallExceptionHandler`)
****
Reads the syscall number from `reg_a0` of the saved state. Increments the saved PC by 4 before branching to avoid re-executing the `ecall` instruction upon return. Implements:

- **`TERMINATE` (SYS2)**: Treated identically to a program trap - triggers `ProgramTrapHandler` for orderly cleanup. This is how a U-proc requests voluntary termination.
- **`WRITETERMINAL` (SYS4)**: Writes a string to Terminal 0 (transmit sub-device, semaphore index 40). Validates that `len ∈ [0, 128]` and that the address range lies within user space `[KUSEG, USERSTACKTOP)`. Performs character-by-character `DOIO` calls, returning the count of successfully transmitted characters in `reg_a0`.
- **`READTERMINAL` (SYS5)**: Reads from Terminal 0 (receive sub-device, semaphore index 32) character-by-character until `\n`. Validates that the destination buffer address is within user space. Supports an optional `maxLen` parameter: characters beyond the limit are consumed from the device but discarded, preventing leftover input for subsequent reads. Returns the number of characters received (including `\n`) in `reg_a0`.
- **`EXECUTE` (SYS6)**: **Shell-only** syscall (enforced by checking `sup_asid == 1`). Validates that the requested ASID `∈ [2, UPROCMAX]`. Allocates a `support_t`, reads the target program's a.out header from its flash device, initializes the page table (read-only `.text`, writable rest), and spawns the new U-proc via `CREATEPROCESS`. The shell then blocks on `shellSemaphore` until the child terminates.
- **Default**: Any unrecognized syscall number triggers `ProgramTrapHandler`.

### Program Trap Handler (`ProgramTrapHandler`)

Handles fatal exceptions for a U-proc (program traps forwarded from the Nucleus, or internally triggered errors). Performs orderly teardown:

1. **Swap Pool Cleanup**: Acquires `swapSemaphore` (only if not already held, checked via `page_mutex_holder`). Marks all frames owned by the dying ASID as free (`sw_asid = -1`), avoiding unnecessary flash write-backs. Releases the semaphore.
2. **Terminal Read Release**: If `devSemaphores[32] == 0`, performs a `VERHOGEN` on it - handles the edge case where the U-proc dies while blocked mid-`READTERMINAL`, preventing the shell from deadlocking on the next read.
3. **Synchronization Signal**: If the dying process is the shell (ASID 1), performs a `VERHOGEN` on `masterSemaphore` to unblock the Instantiator. Otherwise, performs a `VERHOGEN` on `shellSemaphore` to unblock the shell, which is waiting for its child to complete.
4. **Support Structure Reclaim**: Returns the `support_t` to the free list via `deallocateSupport`.
5. **TLB Flush**: Calls `TLBCLR()` to invalidate all TLB entries for the dying ASID.
6. **Terminate**: Calls `TERMPROCESS` (SYS2 via the Nucleus) to remove the process from the system.

---

## 6. User-Space Shell and Programs (`testers/`)

The user-space side runs as a set of U-proc images loaded on individual flash devices.

### Shell (`shell.c`)

The shell (ASID 1) runs an interactive REPL on Terminal 0:

1. Prints the prompt `PandOSsh>> `.
2. Reads a line of input via `READTERMINAL`.
3. Matches the input against a table of known programs (`calc`, `sl`, `date`, `echo`, `fibEight`, `fibEleven`, `uname`), each with its assigned ASID.
4. On a match, issues `EXECUTE` with the target ASID, then implicitly blocks until the child terminates (via `shellSemaphore`).
5. On `exit`, issues `TERMINATE` (SYS2) to shut down.
6. On unknown input (non-empty), prints `Command not found.`.

### Available Programs

| Command     | ASID | Description                                       |
| ----------- | ---- | ------------------------------------------------- |
| `calc`      | 2    | Simple integer arithmetic calculator (RPN-style). |
| `sl`        | 3    | ASCII art steam locomotive animation.             |
| `date`      | 4    | Prints the current system uptime/date info.       |
| `echo`      | 5    | Echoes user input back to Terminal 0.             |
| `fibEight`  | 6    | Computes and prints Fibonacci(8).                 |
| `fibEleven` | 7    | Computes and prints Fibonacci(11).                |
| `uname`     | 8    | Prints OS identification string.                  |
