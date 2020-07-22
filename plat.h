#ifndef _PLAT_H_
#define _PLAT_H_


#include "../ModulaR/bl.h"
#include <stdbool.h>
#include <stdint.h>


void platInit(bool enableCdcAcmUart, uint8_t blVer);

#ifdef EFM

//	#define USE_TX_AS_RST	//hack for now, overwrites everything (incl real reset pin)

	#include "efm32hg308f64.h"

	#define usbWork			BL->usbWork
	#define packetCanSend	BL->packetCanSend
	#define packetSend		BL->packetSend
	#define packetRx		BL->packetRx
	#define packetRxRelease	BL->packetRxRelease
	#define bootloader		BL->bootloader
	#define getUsbCaps		BL->getUsbCaps
	#define usbReenumerate	BL->usbReenumerate
	#define blGetBlVersion	BL->blGetBlVersion

	void platDeinit(void);
	void platWork(void);
	
	uint32_t platGetFlags(void);		//this should be fast. cache as needed
	bool platPowerOnOffSet(bool on);
	bool platPowerVaribleSet(uint32_t mV);
	
	int32_t platGetCurSupplyVoltage(void);		//only tells us what we're trying to supply

	#define HW_TYPE		SWD_COMMS_HW_TYP_EFM
	uint8_t platGetHwVerForComms();			//get platform version in format that comms understands: SWD_COMMS_HW_*_VER_*
	
	void platGetSupplyAbilities(uint16_t *millivoltsMinP, uint16_t *millivoltsMaxP, uint16_t *milliampsMaxP);
	uint32_t platGetSwdMaxClkSpeed(void);	//in hz
	uint32_t platSetSwdClockSpeed(uint32_t speed);
	
	uint64_t platGetTicks(void);
	uint32_t platGetTicksPerSecond(void);
	
#endif

#ifdef AVR

	#include <util/delay.h>
	#include <avr/pgmspace.h>
	#include <avr/interrupt.h>
	#include <avr/wdt.h>
	#include <avr/io.h>
	#define platDeinit()
	#define platWork()
	#define platPowerOnOffSet(on)		(0 * on /* just to make sure it is used */)
	#define platPowerVaribleSet(mv)		(0 * mv /* just to make sure it is used */)
	#define platGetFlags()				(SWD_FLAG_SLOW_DEBUGGER)
	#define platGetHwVerForComms()		(0)
	#define platGetSupplyAbilities(...)	LINK_ERROR_THIS_SHOULD_NEVER_BE_CALLED()
	#define platGetSwdMaxClkSpeed(...)	LINK_ERROR_THIS_SHOULD_NEVER_BE_CALLED()
	#define platSetSwdClockSpeed(...)	LINK_ERROR_THIS_SHOULD_NEVER_BE_CALLED()
	#define platGetCurSupplyVoltage()	(-1)

	#define HW_TYPE		SWD_COMMS_HW_TYP_AVR_PROTO

	extern int LINK_ERROR_THIS_SHOULD_NEVER_BE_CALLED(void);

	#define platGetTicks()				LINK_ERROR_THIS_SHOULD_NEVER_BE_CALLED()
	#define platGetTicksPerSecond()		LINK_ERROR_THIS_SHOULD_NEVER_BE_CALLED()

#endif






#endif