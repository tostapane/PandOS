# PandOSsh - Phase 3 (The Support Level)

This repository contains the implementation of **Phase 3** for the PandOSsh operating system on uRISCV. This phase implements the **Support Level** (Level 4), introducing virtual memory management, a user-space shell, and higher-level system services for User-mode processes (U-procs).

> **Note:** The Phase 3 submission is **cumulative** and includes the source code of all three phases (Phase 1, Phase 2, and Phase 3).

## Project Structure

The project follows the strict directory structure required for the assignment:

* `root/`
  * `CMakeLists.txt`: Build configuration.
  * `klog.c`: Kernel logging utility.
  * `headers/`: Global headers (`const.h`, `types.h`, `listx.h`).
  * `phase1/`:
    * `pcb.c`: Implementation of PCB queues and process trees.
    * `asl.c`: Implementation of the Active Semaphore List.
    * `headers/`: Phase-specific headers (`pcb.h`, `asl.h`).
  * `phase2/`:
    * `initial.c`: System initialization and first process bootstrapping.
    * `scheduler.c`: Preemptive round-robin scheduler.
    * `interrupts.c`: Interrupt handler for devices and timers.
    * `exceptions.c`: Exception dispatcher, SYSCALL handler, tree-hierarchy tools.
    * `headers/`: Phase-specific headers (`initial.h`, `scheduler.h`, `interrupts.h`, `exceptions.h`).
  * `phase3/`:
    * `initProc.c`: Instantiator process - bootstraps the shell U-proc and manages support structures.
    * `vmSupport.c`: Virtual memory support - implements the Pager (TLB exception handler) and Swap Pool management.
    * `sysSupport.c`: General exception handler - implements U-proc system calls and program trap handling.
    * `headers/`: Phase-specific headers (`initProc.h`, `vmSupport.h`, `sysSupport.h`, `shell.h`, `utils.h`, `calc.h`, `print.h`, `tconst.h`).
    * `testers/`: User-space programs that run on top of the OS shell (`shell.c`, `calc.c`, `echo.c`, `fibEight.c`, `fibEleven.c`, `date.c`, `uname.c`, `sl.c`, `print.c`, `utils.c`).

## Prerequisites

To build and run this project, ensure you have:

1. **uRISCV emulator** installed.
2. **GCC Toolchain for RISC-V** (`riscv64-unknown-elf-gcc`).
3. **CMake** (Version 3.25+).

## Building

1. Create a build directory:

    ```bash
    mkdir build && cd build
    ```

2. Configure the project with CMake:

    ```bash
    cmake ..
    ```

3. Compile:

    ```bash
    cmake --build .
    ```

This will generate `MultiPandOS.core.uriscv`, `MultiPandOS.stab.uriscv`, and the flash images for the shell and U-proc programs.

## Running the Test

1. Load the generated `config_machine.json` file into the uRISCV emulator.
2. Start the simulation.
3. Observe the output in **Terminal 0**.
4. Success condition: The system should display a `PandOSsh>> ` prompt. Commands available: `calc`, `sl`, `date`, `echo`, `fibEight`, `fibEleven`, `uname`. Type `exit` to terminate the shell and halt the system.

## Authors

Please refer to the `AUTHOR.md` file for student details.
