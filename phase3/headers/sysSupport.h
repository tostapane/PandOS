#ifndef SYSSUPPORT_H
#define SYSSUPPORT_H

#include "../../headers/types.h"
#include "../../headers/const.h"

/* The main entry point for non-TLB exceptions passed up by the Nucleus. */
void GeneralExceptionHandler();

/* Handles SYSCALLs >= 1 */
void SyscallExceptionHandler(support_t *supStruct, unsigned int excCode);

/* Handles U-proc fatal errors */
void ProgramTrapHandler(support_t *supStruct);

#endif