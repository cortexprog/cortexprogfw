#ifndef _WIRE_H_
#define _WIRE_H_

#ifndef WIN32
#include <stdbool.h>
#include <stdint.h>
#endif
#include "util.h"


typedef uint8_t uint1_t;

#define BUS_SWD_ACK			1
#define BUS_SWD_WAIT		2
#define BUS_SWD_FAULT		4
#define BUS_SWD_EMPTY		7			//if bus has nothing on it (or nothing is talking) we'll see this

#define BUS_DATA_PAR_ERR	0x80		//orred with return of wireBusRead();


void wireInit(void);
void wireSwdSendKey(void);

void wireSetResetPinVal(bool high);		//reset is active low

//low level funcs, do not use
uint8_t llWireBusRead(uint1_t ap, uint8_t a23, unaligned_uint32_t *valP);
uint8_t llWireBusWrite(uint1_t ap, uint8_t a23, uint32_t val);

uint32_t wireSetClockSpeed(uint32_t speed);

//even lower - do not use even more (also, not on avr at all due to avr asm code)
void llWireBusWriteBits(uint32_t val, uint32_t nbits);	//8..16 bits only


#endif
