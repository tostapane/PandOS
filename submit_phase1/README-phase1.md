# PandOSsh - Phase 1 (The Queues Manager)

This repository contains the implementation of **Phase 1** for the PandOSsh operating system on uRISCV. This phase implements the Process Control Block (PCB) queues and the Active Semaphore List (ASL).

## Project Structure

The project follows the strict directory structure required for the assignment:

* `root/`
  * `CMakeLists.txt`: Build configuration.
  * `klog.c`: Kernel logging utility.
  * `headers/`: Global headers (`const.h`, `types.h`, `listx.h`).
  * `phase1/`:
    * `pcb.c`: Implementation of PCB queues and process trees.
    * `asl.c`: Implementation of the Active Semaphore List.
    * `p1test.c`: Test suite (Level 2).
    * `headers/`: Phase-specific headers (`pcb.h`, `asl.h`).

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

This will generate `MultiPandOS.core.uriscv` and `MultiPandOS.stab.uriscv`.

## Running the Test

1. Load the generated `.core.uriscv` file into the uRISCV emulator.
2. Start the simulation.
3. Observe the output in **Terminal 0**.
4. Success condition: The system should print `So Long and Thanks for All the Fish` and then halt.

## Authors

Please refer to the `AUTHOR.md` file for student details.
