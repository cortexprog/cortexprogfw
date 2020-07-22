#ifndef _CORTEX_H_
#define _CORTEX_H_

#ifndef WIN32
#include <stdbool.h>
#include <stdint.h>
#endif
#include "util.h"


#define CPUID_PART_M0   		0xC200
#define CPUID_PART_M1   		0xC210
#define CPUID_PART_M3   		0xC230
#define CPUID_PART_M4   		0xC240
#define CPUID_PART_M0p  		0xC600
#define CPUID_PART_M7			0xC270
#define CPUID_PART_UNREADABLE	0xBEEF

#define CORTEX_W_HALT_OR_STEP	0x01	/* C_HALT or C_STEP set */
#define CORTEX_W_BKPT		0x02	/* BKPT executed */
#define CORTEX_W_DWPT		0x04	/* data watchpoint */
#define CORTEX_W_VCATCH		0x08	/* vector caught */
#define CORTEX_W_EXTERNAL	0x10	/* external debug request */
#define CORTEX_W_FAIL		0xFF


#define CORTEX_WP_OFF		0
#define CORTEX_WP_PC		4
#define CORTEX_WP_READ		5
#define CORTEX_WP_WRITE		6
#define CORTEX_WP_RW		7
#define CORTEX_WP_MASK		0x0F

#define CORTEX_INIT_OK					0
#define CORTEX_INIT_CPUID_READ_FAIL		1
#define CORTEX_INIT_CPU_TYPE_UNKNOWN	2
#define CORTEX_INIT_DEMCR_WRITE_FAIL	3
#define CORTEX_INIT_WPT_SETUP_FAIL		4
#define CORTEX_INIT_BPT_SETUP_FAIL		5
#define CORTEX_INIT_FPU_CHECK_FAIL		6

//stateP is used for fast cpu switching later on
uint8_t cortexInit(unaligned_uint16_t *cortexTypeP, uint8_t *fastCpuSwitchingStateP);	//zero on error

//to be done after memap has been switched already
bool cortexFastCpuSwitch(uint8_t fastCpuSwitchingState);

bool cortexRegRead(uint8_t regNo, unaligned_uint32_t *valP);
bool cortexRegWrite(uint8_t regNo, uint32_t val);

bool cortexStep(void);
bool cortexStop(void);
bool cortexGo(void);
bool cortexReset(void);
uint8_t cortexGetStopReason(void);		//return & CLEAR(!!!) stop reason (aka: "what happened??") -> CORTEX_W_*

bool cortexHaveFpu(void);

#define CORTEX_REG_Rx(x)	(x)	//R0..R15
#define CORTEX_REG_XPSR		16
#define CORTEX_REG_MSP		17
#define CORTEX_REG_PSP		18
#define CORTEX_REG_CFBP		20 /* control, faultmask, basepri, primask */
#define CORTEX_REG_FPCSR	33
#define CORTEX_REG_Sx(x)	(64 + (x))	//S0..S31


#endif

