#include "efm32hg308f64.h"
#include "led-efm.h"


//v0,v1,v2,v3,v4:
//	b13 is vcc for leds (must be high)
//	b8 (pwr), b11 (act) are active low
//	b8 is tim1_cc1 which we'll use to reduce its brightness to 1/8 brightness via PWM
//v5:
//	b11 is act, power led i snot controllable

void ledInit(void)
{
	if (BL->blGetHwVer() == HW_VER_EFM_V5) {	//simpler v5 code
		
		//clock up gpio
		CMU->HFPERCLKEN0 |= CMU_HFPERCLKEN0_GPIO;
		
		//unlock gpio
		GPIO->LOCK = _GPIO_LOCK_LOCKKEY_UNLOCK;
		
		//enable high-current drive
		GPIO->P[1].CTRL = GPIO_P_CTRL_DRIVEMODE_HIGH;
		
		//enable proper pins as gpios in high current mode
		GPIO->P[1].MODEH = (GPIO->P[1].MODEH &~ _GPIO_P_MODEH_MODE11_MASK) | GPIO_P_MODEH_MODE11_PUSHPULLDRIVE;
		
		//set ACT pin high (led off)
		GPIO->P[1].DOUTSET = 1 << 11;
	}
	else {
		//clock up gpio and timer1
		CMU->HFPERCLKEN0 |= CMU_HFPERCLKEN0_GPIO | CMU_HFPERCLKEN0_TIMER1;
		
		//unlock gpio
		GPIO->LOCK = _GPIO_LOCK_LOCKKEY_UNLOCK;
		
		//enable high-current drive
		GPIO->P[1].CTRL = GPIO_P_CTRL_DRIVEMODE_HIGH;
		
		//enable proper pins as gpios in high current mode
		GPIO->P[1].MODEH = (GPIO->P[1].MODEH &~ (_GPIO_P_MODEH_MODE8_MASK | _GPIO_P_MODEH_MODE11_MASK | _GPIO_P_MODEH_MODE13_MASK)) | GPIO_P_MODEH_MODE8_PUSHPULLDRIVE | GPIO_P_MODEH_MODE11_PUSHPULLDRIVE | GPIO_P_MODEH_MODE13_PUSHPULLDRIVE;
		
		//set all pins	(macrofab populated LEDS backwards)
		GPIO->P[1].DOUTSET = (1 << 8) | (1 << 11) | (1 << 13);
		
		//config timer 1
		TIMER1->CMD = TIMER_CMD_STOP;
		TIMER1->CTRL = TIMER_CTRL_PRESC_DIV1 | TIMER_CTRL_CLKSEL_PRESCHFPERCLK | TIMER_CTRL_MODE_UP;
		TIMER1->TOP = 0xFF;
		TIMER1->TOPB = 0xFF;
		TIMER1->CNT = 0;
		TIMER1->ROUTE = TIMER_ROUTE_LOCATION_LOC3 | TIMER_ROUTE_CC1PEN;
		TIMER1->CC[1].CTRL = TIMER_CC_CTRL_MODE_PWM;
		TIMER1->CC[1].CCV = 0x100;
		TIMER1->CMD = TIMER_CMD_START;
	}
}



