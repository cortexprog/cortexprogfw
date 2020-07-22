#ifndef _MEMIO_H_
#define _MEMIO_H_

#include <stdio.h>
#include "types.h"
#include "cpu.h"


//FILE* api		(large, progress indicators)
bool memioWriteFromFile(struct Cpu* cpu, uint32_t dstAddr, uint32_t dstLen, FILE *f, bool withAck, bool showProgress, bool showSpeedInProgress);	//will round file read up to nearest 4 bytes. Write is padded by zeroes to asked sz
bool memioReadToFile(struct Cpu *cpu, uint32_t srcAddr, uint32_t srcLen, FILE *f, bool showProgress, bool showSpeedInProgress);

//void* api		(RAM, no progress indicator), return num bytes done
uint32_t memioReadToBuffer(struct Cpu *cpu, uint32_t srcAddr, uint32_t srcLen, void* dst);


#endif
