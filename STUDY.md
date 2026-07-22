# PandOSsh Exam Roadmap

Based on the actual code in this repo (the **uRISCV** port — RISC-V registers, not MIPS).

## Big picture (learn this first, 30 min)

The system is a layer stack; be able to draw it and say what each layer gives the one above:

1. **Hardware/BIOS (uRISCV emulator)** — raises exceptions, saves processor state at `BIOSDATAPAGE`, jumps where the Pass-Up Vector says.
2. **Phase 1 — Queue managers** (`phase1/`): PCBs, process queues, process tree, Active Semaphore List. Pure data structures, no hardware.
3. **Phase 2 — Nucleus** (`phase2/`): scheduler, 10 kernel syscalls (NSYS1–10), interrupt handling, Pass Up or Die.
4. **Phase 3 — Support Level** (`phase3/`): virtual memory (Pager + Swap Pool), user-level syscalls (SYS2/4/5/6), and the shell (the "sh" in PandOSsh) running as a demand-paged U-proc.

Sources: `doc.md` (root), then the "Project Overview" section of each `submit_phaseN/Documentation-phaseN.md` — the fastest recap of each layer.

## Step 0 — Minimum hardware knowledge (half a day)

**Caveat:** `uMPS3princOfOperations.pdf` is the MIPS version; the code is uRISCV, so names map as: Status→`mstatus`, Cause→`mcause`, EPC→`mepc`/`pc_epc`, CP0→CSRs. Concepts are identical.

**Register cheat sheet (memorize — the "which register when" part):**

| Register / field | Used for | Where in the code |
|---|---|---|
| `reg_a0` | syscall number in; return value out | `exceptions.c:214`, `interrupts.c:168` |
| `reg_a1–a3` | syscall arguments (state ptr, sem addr, cmd, support ptr) | every `case` in `syscallHandler` |
| `pc_epc` | PC at exception; `+= WORDLEN` (4) to skip the `ecall` | `exceptions.c:231`, `sysSupport.c:71` |
| `cause` (mcause) | interrupt bit + ExcCode (`CAUSE_IS_INT`, `CAUSE_EXCCODE_MASK`) | `exceptions.c:174-190` |
| `status` (mstatus) | `MPP` = previous privilege (M=kernel/U=user), `MPIE`/`MIE` = interrupt enable | `initial.c:96`, user-mode check `exceptions.c:221` |
| `mie` | per-line interrupt mask; `MIE_MTIE` = PLT timer | `scheduler.c:35` |
| `reg_sp` | stack pointer (`RAMTOP` for kernel proc, `USERSTACKTOP` for U-procs) | `initial.c:100`, `initProc.c:148` |
| `entry_hi` | VPN (bits 31-12) + ASID | TLB handlers, page tables |
| `entry_lo` | PFN + **D**irty + **V**alid + **G**lobal bits | `vmSupport.c`, `initProc.c` |
| `Index` | result of `TLBP` probe; `PRESENTFLAG` = not found | `vmSupport.c:193` |

**PoP sections actually worth reading** (skip the rest): §2.2–2.3 processor state & Status (p. 8), §3 exception handling + BIOS Data Page + Cause (p. 11–19), §4.1 system clocks (TOD, Interval Timer, PLT — p. 20), §5.1–5.2 + 5.7 device registers, bitmaps, terminals (p. 25–43), §6.3 address translation, EntryHi/EntryLo, TLB (p. 49–57), §7.4 `LDST`/`LDCXT` (p. 66). ~40 pages total.

**Memory map to memorize:** `BIOSDATAPAGE` 0x0FFF.F000 (saved exception state), `PASSUPVECTOR` 0x0FFF.F900, `RAMSTART` 0x2000.0000, `KERNELSTACK` 0x2000.1000, device registers from `START_ADDR` (each device 0x10 bytes, each line 0x80), KUSEG from 0x8000.0000, `UPROCSTARTADDR` 0x8000.00B0, `USERSTACKTOP` 0xC000.0000 (stack page VPN 0xBFFFF).

## Step 1 — Phase 1: data structures (half a day)

Spec: `PandOSshPhase1Spec.pdf` (whole thing). Doc: `submit_phase1/Documentation-phase1.md`. Code: `phase1/pcb.c`, `phase1/asl.c`, `headers/listx.h`.

Know cold:
- `list_head` sentinel-based doubly linked lists and the `container_of` trick (borrowed from Linux) — a classic oral question.
- `pcb_t` fields and *which list each field threads*: `p_list` (queue/free list), `p_parent`/`p_child`/`p_sib` (tree), `p_semAdd` (which semaphore it's blocked on).
- Allocation pattern: static array `pcbTable[MAXPROC]` + `pcbFree` list; `allocPcb` resets fields. Same pattern reused twice later (semd free list, phase 3 `supStructs_freeList`).
- ASL: `semd_t` keyed by semaphore *address* (`s_key`), one descriptor only while processes are blocked; `insertBlocked`/`removeBlocked`/`outBlocked` semantics.

## Step 2 — Phase 2: the Nucleus (1.5–2 days, the core of the exam)

Spec: `PandOSshPhase2Spec.pdf`. Doc: `submit_phase2/Documentation-phase2.md`. Read code in this order:

**1. `phase2/initial.c` — boot.** Pass-Up Vector setup (TLB-refill handler + general `exceptionHandler`, both on `KERNELSTACK`), global state (`processCount`, `softBlockCount`, `readyQueue`, `currProc`, `subDevice[49]`), `LDIT(PSECOND)` = 100ms pseudo-clock, first process (kernel mode `MSTATUS_MPP_M`, all interrupts, SP=`RAMTOP`, PC=`test`).

**2. `phase2/scheduler.c` — 49 lines, know it line by line.** Round-robin: dequeue, `setTIMER(5ms)` (note the `TIMESCALEADDR` multiplication — time is in ticks), `STCK(processTimer)` for CPU accounting, `LDST`. The three empty-queue cases: `HALT` (no processes), `WAIT` (soft-blocked exist — with PLT masked out of `mie`, otherwise the dead PLT would fire), `PANIC` (deadlock).

**3. `phase2/exceptions.c` — the heart.**
- `exceptionHandler` dispatch on `cause`: interrupt bit → `interruptHandler`; ExcCode 8/11 (ecall from U/M mode) → syscalls; 24–28 → TLB; else program trap.
- All 10 syscalls (NSYS1–10). For each know: arguments in which register, return value, blocking or not. Special attention:
  - `PASSEREN`/`VERHOGEN` on **binary** semaphores — the V "passes the baton" (moves a waiter to ready instead of incrementing) at `exceptions.c:333`.
  - `DOIO` — device index computed from the command address (`(addr - START_ADDR)/0x10`, terminals split TX=32–39 / RX=40–47), process blocks on `subDevice[index]`, `softBlockCount++`, and *then* the command is written (`exceptions.c:403`).
  - The blocking-return path (`exceptions.c:500-511`): non-blocking → `LDST(exception_state)`; blocking → charge CPU time to `p_time`, `currProc = NULL`, `scheduler()`.
  - Negative syscall in user mode → cause rewritten to `PRIVINSTR` and rerouted to program trap (`exceptions.c:220`).
- **Pass Up or Die** (`programTrapHandler`/`tlbHandler`): if `p_supportStruct` exists, copy saved state into `sup_exceptState[GENERALEXCEPT|PGFAULTEXCEPT]` and `LDCXT` into the support handler's context; else `recursive_terminate` + scheduler. Know the difference between `LDST` (loads a full `state_t`) and `LDCXT` (loads sp/status/pc — enters a *new* context).
- Extras: `find_pcb` (walk up to root, DFS down — needed because TERMPROCESS takes a PID), `recursive_terminate` (kills subtree, fixes `softBlockCount` only for device semaphores — processes blocked on *user* semaphores aren't "soft blocked").
- `uTLB_RefillHandler` (`exceptions.c:23`): VPN → page-table index (0xBFFFF → 31, else VPN−0x80000), write the PTE with `TLBWR`, `LDST`. Runs on *every* TLB miss; the Pager only runs when the page is invalid.

**4. `phase2/interrupts.c`.** Priority order (lowest line first), device discovery via the **interrupt bitmap** (`BITMAP_BASE`, word per line, bit per device), ACK by writing `ACK` to the command register, terminal special case (two sub-devices; status byte `& 0xFF == 5` = char transmitted/received; upper byte carries the character). Waking: `removeBlocked`, status delivered in the woken process's `reg_a0`, `softBlockCount--`; if nobody was waiting, V the semaphore. `PLTInterrupt` = end of time slice (state → PCB, back of ready queue, charge time). `ITInterrupt` = pseudo-clock: `LDIT` reload, wake *everyone* on `subDevice[48]`, reset it to 0. Return convention: if `currProc` still exists, `LDST` back into it (interrupts don't cost the running process its slice), else `scheduler()`.

## Step 3 — Phase 3: Support Level + shell (1.5–2 days)

Spec: `PandOSshPhase3Spec.pdf`. Doc: `submit_phase3/Documentation-phase3.md`. Reading order:

**1. `phase3/initProc.c` — the Instantiator (`test`).** Initialization of: `swapSemaphore=1` (mutex) vs `masterSemaphore=0`/`shellSemaphore=0` (synchronization) — *be ready to explain why 1 vs 0*; 49 `devSemaphores` at 1; support-struct free list (phase-1 pattern reuse). Shell setup: ASID 1, user mode `MSTATUS_MPP_U`, page table of 32 entries (31 text/data VPN 0x80000+, stack VPN 0xBFFFF, all V=0 → pure demand paging), D bit = 0 on `.text` pages (read-only), exception contexts pointing at `Pager` and `GeneralExceptionHandler` in **kernel** mode with stacks carved under `RAMTOP`. Then `CREATEPROCESS`, P on `masterSemaphore`, and on wake `TERMPROCESS` → `processCount==0` → `HALT`.

**2. `phase3/vmSupport.c` — the Pager.** The full page-fault algorithm in order: TLB-Mod (write to read-only) → trap; P on `swapSemaphore` (+ record `page_mutex_holder`); missing VPN from saved `entry_hi`; pick frame (free-frame scan, else FIFO `fifo_ptr`); if occupied: **atomically** (interrupts off via `MSTATUS_MIE`) invalidate victim's PTE + selective TLB update (`TLBP`, if found `TLBWI`), then flash-write victim's page to its owner's backing store; flash-read the missing page; update Swap Pool + PTE (`frameAddr | VALIDON`, preserving D); selective TLB update again; V and `LDST`. Know *why* the atomicity is needed (TLB/page-table consistency across a context switch) and *why* read happens before the PTE update (spec-prescribed ordering).

**3. `phase3/sysSupport.c` — support syscalls.** Dispatch via `sup_exceptState[GENERALEXCEPT].cause`. SYS2 TERMINATE = ProgramTrapHandler; SYS4 WRITETERMINAL / SYS5 READTERMINAL — parameter validation against KUSEG/USERSTACKTOP (kill on violation), mutex on `devSemaphores[40]` (term0 TX) / `[32]` (term0 RX), char-by-char DOIO, status 5 = OK, char in bits 8–15 of the status. SYS6 EXECUTE (shell-only, this project's distinctive syscall): validate ASID 2–8, allocate support struct, read the program's a.out header from flash `asid-1`, build page table (read-only `.text` via `AOUT_HE_TEXT_MEMSZ`), `CREATEPROCESS`, then shell blocks on `shellSemaphore` until the child dies. `ProgramTrapHandler` cleanup sequence: free the dying ASID's Swap Pool frames, the `page_mutex_holder` self-deadlock guard, the `devSemaphores[32]` orphaned-mutex edge case, V on `masterSemaphore` (shell dying) vs `shellSemaphore` (child dying), `deallocateSupport`, `TLBCLR`, NSYS2.

**4. Shell & testers** (skim): `phase3/headers/shell.h`, `calc.h`, `print.h`, `tconst.h`, `phase3/testers/` — the shell is a REPL U-proc on terminal 0 that spawns programs by ASID via EXECUTE.

## Cross-cutting cheat sheets (make these on paper — likely exam questions)

- **Semaphore map**: kernel `subDevice[0..48]` (sync, init 0, index 48 = pseudo-clock) vs support `devSemaphores[0..48]` (mutex, init 1) vs `swapSemaphore`/`masterSemaphore`/`shellSemaphore`. Two *different* arrays with the same layout — explain the roles.
- **Life of a page fault**: TLB miss → refill handler → PTE invalid → second miss → nucleus `tlbHandler` → pass up (PGFAULTEXCEPT) → Pager → LDST retry. Narrating this end-to-end is the single highest-value thing to rehearse.
- **Life of a terminal write**: SYS4 → pass up (GENERALEXCEPT) → SyscallExceptionHandler → DOIO per char → process blocks → device interrupt → ACK → wake with status in `a0`.
- **§10 optimizations implemented**: 10.1 selective TLB update (`TLBP`+`TLBWI` instead of `TLBCLR`), 10.2 freeing a dead process's frames, 10.3 free-frame search before FIFO eviction, 10.4 read-only `.text` from the a.out header, 10.5 `swapPoolBase` computed from the kernel's a.out header instead of a fixed 32-frame overestimate, 10.6 handler stacks under `RAMTOP`, 10.7 support-struct free list. Each is commented in place in `vmSupport.c`/`initProc.c`/`sysSupport.c`.

## Suggested schedule (5 days)

- Day 1: big picture + step 0.
- Day 2: phase 1 + start phase 2 (initial, scheduler).
- Day 3: exceptions.c + interrupts.c.
- Day 4: phase 3.
- Day 5: cheat sheets, re-read the three Documentation-*.md end to end (they're essentially oral answers already), and rehearse the two "life of a..." narratives out loud.
