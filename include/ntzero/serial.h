#ifndef NTZERO_SERIAL_H
#define NTZERO_SERIAL_H

#include "types.h"

void SerialInitialize(void);
void SerialWrite(const char *text);
void SerialWriteHex(u64 value);
void SerialWriteDec(u64 value);

#endif
