#ifndef NTZERO_INTERRUPTS_H
#define NTZERO_INTERRUPTS_H

#include "types.h"

typedef void (*NTZ_INTERRUPT_HANDLER)(u8 vector, u64 error_code, void *context);

NTSTATUS InterruptInitialize(void);
NTSTATUS InterruptRegister(u8 vector, NTZ_INTERRUPT_HANDLER handler, void *context);
void InterruptUnregister(u8 vector);
NTSTATUS InterruptAllocateVector(u8 *out_vector);
void InterruptFreeVector(u8 vector);
void InterruptEnable(void);
void InterruptDisable(void);

#endif
