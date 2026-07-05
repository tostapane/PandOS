# PandOSsh Phase 1: Level 2 Documentation

## 1. Project Overview

Phase 1 implements the **Queue Managers** for the PandOSsh operating system. This layer manages the active entities of the OS, defined as processes, using data structures called Process Control Blocks (PCBs). It encompasses two primary modules: the Process Queue Manager and the Active Semaphore List (ASL).

## 2. Core Data Structures

### Process Control Block (`pcb_t`)

Represents a process, allocated from a generic pool.

- **Queue Fields:** `p_list` is used for process queues and the `pcbFree` list.
- **Tree Fields:** `p_parent`, `p_child`, and `p_sib` manage process hierarchies.
- **Status Info:** Contains processor state (`p_s`), accumulated time (`p_time`), priority (`p_prio`), and process ID (`p_pid`).
- **Synchronization:** `p_semAdd` points to the semaphore on which a process is blocked.

#### Semaphore Descriptor (`semd_t`)

Represents a semaphore that currently has blocked processes.

- **Key:** `s_key` stores the physical address of the semaphore.
- **Queue:** `s_procq` is the list of PCBs blocked on this semaphore.
- **Link:** `s_link` links the descriptor within the ASL or free list.

### List Implementation

The project utilizes generic, type-oblivious doubly linked lists (`struct list_head`) adapted from the Linux kernel.

- Lists use a sentinel element to link the first and last nodes.
- Data is accessed using the `container_of` macro.

---

### 3. Module: Process Queue Manager (`pcb.c`)

This module manages the allocation, organization, and hierarchy of PCBs.

#### Allocation and Deallocation

- **Storage:** A static array `pcbTable` of size `MAXPROC` (20) provides the memory pool.
- **Initialization (`initPcbs`):** Populates the `pcbFree` list with all elements from `pcbTable`.
- **Allocation (`allocPcb`):** Removes a PCB from the free list and resets all fields to `NULL` or `0`, except `p_pid` which is incremented. Returns `NULL` if the list is empty.
- **Deallocation (`freePcb`):** Returns a specific PCB to the `pcbFree` list.

#### Queue Maintenance

Generic operations for manipulating doubly linked lists of PCBs.

- **Initialization (`mkEmptyProcQ`):** Initializes a variable to be a head pointer to a process queue.
- **Insertion (`insertProcQ`):** Inserts a PCB into a queue ordered by priority (descending). If priorities are equal, it uses FIFO ordering (inserted after the last PCB with that priority).
- **Removal (`removeProcQ`, `outProcQ`):** `removeProcQ` removes the head (highest priority). `outProcQ` removes a specific PCB regardless of position.
- **Access (`headProcQ`):** Returns the highest priority PCB without removal.

#### Tree Maintenance

Manages parent-child relationships where a parent has a list of children (`p_child`), and children are linked via siblings (`p_sib`).

- **Status (`emptyChild`):** Returns `TRUE` if the PCB has no children, `FALSE` otherwise.
- **Insertion (`insertChild`):** Adds a PCB as a child of a specified parent.
- **Removal (`removeChild`, `outChild`):** `removeChild` detaches the first child. `outChild` detaches a specific PCB from its parent.

---

### 4. Module: Active Semaphore List (`asl.c`)

This module manages semaphores that have at least one blocked process.

#### Logic and Storage

- **Active Definition:** A semaphore is "active" only if its process queue is not empty. Inactive semaphores do not have descriptors in the ASL.
- **Storage:** Uses a static array `semd_table` and a free list `semdFree`.
- **Sorted List:** The ASL (`semd_h`) is maintained in sorted order to support efficient searching and insertion "at the appropriate position".

#### Functions

- **`initASL()`:** Initializes the `semdFree` list to contain all elements of the `semd_table` array. This is called once during data structure initialization.
- **`getSemd(key)` (Private Helper):** Searches the ASL for a descriptor with the matching `key`. It utilizes the sorted nature of the list to return `NULL` early if the search key exceeds the current node's key.
- **`insertBlocked(semAdd, p)`:** Inserts a PCB into the queue of the semaphore at `semAdd`.
  - If the semaphore is not in the ASL, it allocates a new `semd_t` from the free list and inserts it into the ASL.
  - Returns `TRUE` if allocation fails (no free descriptors), `FALSE` otherwise.
- **`removeBlocked(semAdd)`:** Removes the head PCB from the specified semaphore's queue.
  - If the queue becomes empty, the `semd_t` is removed from the ASL and returned to the free list.
- **`outBlocked(p)`:** Removes a specific PCB from its semaphore's queue. Handles descriptor deallocation if the queue empties.
- **`headBlocked(semAdd)`:** Returns the head PCB of the blocked queue without removal.
