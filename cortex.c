#include "cortex.h"
#include "memap.h"


//DWT is much fancier on V7M than on v6M, but we do not use that
#define ADDR_DWT_CTRL	0xE0001000UL
#define ADDR_DWT_COMP0	0xE0001020UL
#define ADDR_DWT_MASK0	0xE0001024UL
#define ADDR_DWT_FUNC0	0xE0001028UL
#define OFST_DWT_NEXT   0x00000010UL	//from reg_x to reg_x+1

//v7M (C-M3/C-M4) has an FBP which is more advanced and has options like literal comparators & instr remap
// (M7 is similar but has extra registers)
#define ADDR_FBP_CTRL	0xE0002000UL
#define ADDR_FBP_REMAP	0xE0002004UL
#define ADDR_FBP_COMP0	0xE0002008UL
#define OFST_FBP_NEXT   0x00000004UL	//from reg_x to reg_x+1


#define ADDR_CPUID	0xE000ED00UL
#define ADDR_AIRCR	0xE000ED0CUL
#define ADDR_DFSR	0xE000ED30UL
#define ADDR_CPACR	0xE000ED88UL
#define ADDR_DHCSR	0xE000EDF0UL
#define ADDR_DCRSR	0xE000EDF4UL
#define ADDR_DCRDR	0xE000EDF8UL
#define ADDR_DEMCR	0xE000EDFCUL

#define DHCSR_KEY_MASK	0xFFFF0000UL
#define DHCSR_KEY_VAL	0xA05F0000UL
#define DHCSR_BIT_STEP	0x00000004UL
#define DHCSR_BIT_HALT	0x00000002UL
#define DHCSR_BIT_DBGEN	0x00000001UL

#define CPACR_MASK_FPU	0x00F00000UL

#define CPUID_PART_MASK 0xFFF0

union Info {
	struct {
		uint8_t isV7			: 1;
		uint8_t haveFpu			: 1;
	};
	uint8_t rawVal;
};


#ifdef AVR
#include <avr/io.h>
#define mInfo		(*(union Info*)&EEDR)
#else
static union Info mInfo;
#endif



bool cortexFastCpuSwitch(uint8_t fastCpuSwitchingState)
{
	mInfo.rawVal = fastCpuSwitchingState;
	return true;
}

uint8_t cortexInit(unaligned_uint16_t *cortexTypeP, uint8_t *fastCpuSwitchingStateP)	//zero on error
{
	uint16_t cpuType = 0;
	uint8_t idx, num;
	uint32_t val;
	
	mInfo.rawVal = 0;
	
	//read cpuid & decide what part we have
	if (memapReadAddr(ADDR_CPUID, WRAP_UNALIGNED_POINTER_32(&val)))
		cpuType = val & CPUID_PART_MASK;
	
	if (!cpuType)
		cpuType = CPUID_PART_UNREADABLE;
	
	switch (cpuType) {
		case CPUID_PART_M0:
		case CPUID_PART_M1:
		case CPUID_PART_M0p:
			//vecs: 0x00000401UL
			break;
		case CPUID_PART_M3:
			mInfo.isV7 = 1;
			//vecs: 0x000007F1UL
			break;
		case CPUID_PART_M4:
			mInfo.isV7 = 1;
			//vecs: 0x000007F1UL
			break;
		case CPUID_PART_M7:
			mInfo.isV7 = 1;
			//vecs: 0x000007F1UL
			break;
		default:
			//vecs: 0x000007F1UL
			break;
	}
	
	//enable halt
	if (!memapWriteAddr(ADDR_DEMCR, 0x01000000UL))
		return CORTEX_INIT_DEMCR_WRITE_FAIL;

	//disable all watchpoints
	if (!memapReadAddr(ADDR_DWT_CTRL, WRAP_UNALIGNED_POINTER_32(&val)))
		return CORTEX_INIT_WPT_SETUP_FAIL;
	num = val >> 28;
	
	for (val = ADDR_DWT_COMP0, idx = 0; idx < num; idx++, val += OFST_DWT_NEXT) {
		
		if (!memapWriteAddr(val + 8, 0))	//disable it
			return CORTEX_INIT_WPT_SETUP_FAIL;
	}

	//disable all breakpoints
	//no matter what kind of FBP/BP unit we have, writing zero disables any kind of breakpoint
	if (!memapReadAddr(ADDR_FBP_CTRL, WRAP_UNALIGNED_POINTER_32(&val)))
		return CORTEX_INIT_BPT_SETUP_FAIL;

	num = (uint8_t)(val >> 4) & 0x0F;
	if (mInfo.isV7)
		num += ((uint8_t)(((uint16_t)(val)) >> 8)) & 0x70;
	
	for (val = ADDR_FBP_COMP0, idx = 0; idx < num; idx++, val += OFST_FBP_NEXT) {
		
		if (!memapWriteAddr(val, 0))	//disable it
			return CORTEX_INIT_BPT_SETUP_FAIL;
	}

	//check for FPU
	if (mInfo.isV7) {
		uint32_t old;
		
		if (!memapReadAddr(ADDR_CPACR, WRAP_UNALIGNED_POINTER_32(&old)))
			return CORTEX_INIT_FPU_CHECK_FAIL;
		if (!memapWriteAddr(ADDR_CPACR, old | CPACR_MASK_FPU))
			return CORTEX_INIT_FPU_CHECK_FAIL;
		if (!memapReadAddr(ADDR_CPACR, WRAP_UNALIGNED_POINTER_32(&val)))
			return CORTEX_INIT_FPU_CHECK_FAIL;
		if (!memapWriteAddr(ADDR_CPACR, old))
			return CORTEX_INIT_FPU_CHECK_FAIL;
		
		if ((val & CPACR_MASK_FPU) == CPACR_MASK_FPU)
			mInfo.haveFpu = true;
	}

	UNALIGNED(cortexTypeP) = cpuType;
	if (fastCpuSwitchingStateP)
		*fastCpuSwitchingStateP = mInfo.rawVal;
	 
	return CORTEX_INIT_OK;
}

static bool cortexRegAccessWait(void)
{
	uint8_t retryCtr = 64;
	uint32_t tmp;
	
	do {
		if (!memapReadAddr(ADDR_DHCSR, WRAP_UNALIGNED_POINTER_32(&tmp)))	//use valP as temp storage
			return false;
	} while(!(tmp & 0x00010000UL) && retryCtr--);	//wait for S_REGRDY
	
	return !!retryCtr;
}

bool cortexRegRead(uint8_t regNo, unaligned_uint32_t *valP)
{
	return memapWriteAddr(ADDR_DCRSR, regNo) && cortexRegAccessWait() && memapReadAddr(ADDR_DCRDR, valP);
}

bool cortexRegWrite(uint8_t regNo, uint32_t val)
{
	return memapWriteAddr(ADDR_DCRDR, val) && memapWriteAddr(ADDR_DCRSR, 0x10000UL | regNo) && cortexRegAccessWait();
}

bool cortexStep(void)	//only while stopped
{
	uint32_t val;
	
	return memapReadAddr(ADDR_DHCSR, WRAP_UNALIGNED_POINTER_32(&val)) && memapWriteAddr(ADDR_DHCSR, (val & ~(DHCSR_KEY_MASK | DHCSR_BIT_HALT)) | DHCSR_KEY_VAL | DHCSR_BIT_STEP  | DHCSR_BIT_DBGEN);	//clear halt, set step
}

bool cortexStop(void)
{
	uint32_t val;
	
	return memapReadAddr(ADDR_DHCSR, WRAP_UNALIGNED_POINTER_32(&val)) && memapWriteAddr(ADDR_DHCSR, (val & ~DHCSR_KEY_MASK) | DHCSR_KEY_VAL | DHCSR_BIT_HALT | DHCSR_BIT_DBGEN);
}

bool cortexGo(void)
{
	uint32_t val;
	
	return memapReadAddr(ADDR_DHCSR, WRAP_UNALIGNED_POINTER_32(&val)) && memapWriteAddr(ADDR_DHCSR, (val & ~(DHCSR_KEY_MASK | DHCSR_BIT_HALT | DHCSR_BIT_STEP)) | DHCSR_KEY_VAL | DHCSR_BIT_DBGEN);	//leave debugen, clear halt & step
}

bool cortexReset(void)
{
	return memapWriteAddr(ADDR_AIRCR, 0x05FA0004);	//issue a reset
}

uint8_t cortexGetStopReason(void)
{
	uint32_t val;
	
	//see if we're halted at all
	if (!memapReadAddr(ADDR_DHCSR, WRAP_UNALIGNED_POINTER_32(&val)))
		return CORTEX_W_FAIL;
	
	if (!(val & 0x00020000UL))	//not halted -> no halt reason
		return CORTEX_W_FAIL;
	
	if (!memapReadAddr(ADDR_DFSR, WRAP_UNALIGNED_POINTER_32(&val)))
		return CORTEX_W_FAIL;
	
	if (!memapWriteAddr(ADDR_DFSR, val))	//clear only what we read
		return CORTEX_W_FAIL;
	
	return val;
}

bool cortexHaveFpu(void)
{
	return mInfo.haveFpu;
}
