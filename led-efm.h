#ifndef _LED_H_
#define _LED_H_

#include <stdbool.h>
#include <stdint.h>
#include "plat.h"

#define LED_ACT		0
#define LED_PWR		1

void ledInit(void);

static inline void ledSet(uint32_t which, bool on)
{
	if (which == LED_ACT) {
	
		uint32_t mask = 1 << 11;
		
		if (on)
			GPIO->P[1].DOUTCLR = mask;
		else
			GPIO->P[1].DOUTSET = mask;
	}
	else if (BL->blGetHwVer() != HW_VER_EFM_V5)		//power control only available in pre-v5 boards
		TIMER1->CC[1].CCV = on ? 0xE0 : 0x100;
}


#endif
