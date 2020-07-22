#include <util/delay.h>
#include <avr/io.h>
#include "wire-avr.h"
#include "wire.h"

#define SWD_PORT_REG	(*(volatile uint8_t*)(PORT_SWD + 0x20))
#define SWD_DDR_REG		(*(volatile uint8_t*)(DDR_SWD + 0x20))
#define SWD_PIN_REG		(*(volatile uint8_t*)(PIN_SWD + 0x20))
#define SWD_MASK_IO		((uint8_t)(1 << BIT_NO_SWDIO))
#define SWD_MASK_CK		((uint8_t)(1 << BIT_NO_SWDCK))
#define SWD_MASK_RST	((uint8_t)(1 << BIT_NO_SWRST))





static void wireSendBit(uint1_t bit)
{
	if (bit)
		SWD_PORT_REG |= SWD_MASK_IO;
	else
		SWD_PORT_REG &=~ SWD_MASK_IO;

	SWD_PIN_REG = SWD_MASK_CK;
	SWD_PIN_REG = SWD_MASK_CK;
}


void wireInit(void)
{
	SWD_PORT_REG |= SWD_MASK_IO | SWD_MASK_CK; //data,clock & rst idle high
	SWD_DDR_REG |= SWD_MASK_IO | SWD_MASK_CK; //data, rst, and clock are outputs
}


void wireSwdSendKey(void)
{
	uint16_t key = 0xE79E;
	uint8_t i;

	
	for(i = 0; i < 64; i++)
		wireSendBit(1);
	
	for(i = 0; i < 16; i++, key >>= 1)
		wireSendBit((uint8_t)key & 1);
	
	for(i = 0; i < 64; i++)
		wireSendBit(1);
	
	for(i = 0; i < 8; i++)
		wireSendBit(0);
}
