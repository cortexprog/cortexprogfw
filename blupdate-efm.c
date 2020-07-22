#include "blupdate-efm.h"
#include "efm32hg308f64.h"

#define DESIRED_BL_VER			1								//all our BLS are 2+
#define FLASH_PAGE_SIZE			1024




static uint32_t getUpdateData(const uint8_t **dataP);			//returns size and length. must be 4-byte aligned and size must be a multiple of FLASH_PAGE_SIZE

static void flashReadyWait(void)
{
	while (MSC->STATUS & MSC_STATUS_BUSY);
}

static void flashErase(uint16_t dstAddr)						//erase a page
{
	if (dstAddr & (FLASH_PAGE_SIZE - 1))						//can only erase on block boundary
		return;
	
	MSC->WRITECTRL |= MSC_WRITECTRL_WREN;
	MSC->ADDRB = dstAddr;
	flashReadyWait();
	MSC->WRITECMD = MSC_WRITECMD_LADDRIM;						//latch addr
	
	MSC->WRITECMD = MSC_WRITECMD_ERASEPAGE;						//page erase
	flashReadyWait();
	MSC->WRITECTRL &=~ MSC_WRITECTRL_WREN;
}
													//write a page
static void flashWrite(uint16_t dstAddr, const uint8_t *srcData /* must be 4 byte aligned*/)
{
	uint32_t i, *src = (uint32_t*)srcData;						//WILL BE ALIGNED
	
	if (dstAddr & (FLASH_PAGE_SIZE - 1))						//can only write on block boundary
		return;
	
	MSC->WRITECTRL |= MSC_WRITECTRL_WREN;
	MSC->ADDRB = dstAddr;
	flashReadyWait();
	MSC->WRITECMD = MSC_WRITECMD_LADDRIM;	//latch addr
	
	for (i = 0; i < FLASH_PAGE_SIZE; i += sizeof(uint32_t)) {
		
		while (!(MSC->STATUS & MSC_STATUS_WDATAREADY));			//wait for flash to be ready for data
		MSC->WDATA = *src++;
		MSC->WRITECMD = MSC_WRITECMD_WRITEONCE;
		flashReadyWait();
	}
	
	MSC->WRITECTRL &=~ MSC_WRITECTRL_WREN;
}

void blUpdateIfNeeded(uint8_t curVer)
{
	uint32_t i, updateSz;
	const uint8_t *updataData;
	
	if (curVer >= DESIRED_BL_VER)
		return;
	if (curVer < 2)			//before v2 BLs were locked in the EFM HW and we cannot update those
		return;
	
	//we are commited - disable all interrupts
	asm volatile("cpsid i");
	asm volatile("cpsid f");
	
	//get ready...
	updateSz = getUpdateData(&updataData);
	
	//go
	for (i = 0; i < updateSz; i += FLASH_PAGE_SIZE) {
		
		flashErase(i);
		flashWrite(i, updataData + i);
	}
	
	//reset
	NVIC_SystemReset();
}

static uint32_t getUpdateData(const uint8_t **dataP)
{
	static const uint8_t __attribute__((aligned(4))) update[] = {
		//insert exactly 16K of binary data here
	};
	
	
	*dataP = update;
	return sizeof(update);
}
