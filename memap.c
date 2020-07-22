#include <string.h>
#include "memap.h"
#include "swd.h"

static uint8_t mApSel;
static uint8_t mSelected = 0;
static uint32_t mCurAddr;

#define CSW_AUTOINCR_VALID_RANGE	0x3FFUL

#define REG_CSW		0x00
#define REG_TAR		0x04
#define REG_DRW		0x0C
#define REG_BD0		0x10
#define REG_BD1		0x14
#define REG_BD2		0x18
#define REG_BD3		0x1C
#define REG_CFG		0xF4
#define REG_BASE	0xF8
#define REG_IDR		0xFC

#define MEMAP_SIZE_BYTE		0
#define MEMAP_SIZE_HALFWORD	1
#define MEMAP_SIZE_WORD		2


#define REG_TO_WIRE_ADDR(r)	((r >> 2) & 3)

//perform select such that we can access a given reg
static bool memapSelectRegRange(uint8_t addr)
{
	mSelected = (addr & 0xF0);
	return swdWriteSelect((((uint32_t)mApSel) << 24) | mSelected);
}

bool memapReselect(uint8_t apSel)
{
	mApSel = apSel;
	memapSelectRegRange(0);
	
	return true;
}

static bool memapSimpleRead(uint8_t addr, unaligned_uint32_t *valP)
{
	if ((addr & 0xF0) != mSelected && !memapSelectRegRange(addr))
		return false;
	
	if (!swdWireBusRead(1, REG_TO_WIRE_ADDR(addr), valP))
		return false;
	
	return swdReadRdbuf(valP);

}

static bool memapSimpleWrite(uint8_t addr, uint32_t val)
{
	unaligned_uint32_t dummy;
	
	if ((addr & 0xF0) != mSelected && !memapSelectRegRange(addr))
		return false;
	
	if (!swdWireBusWrite(1, REG_TO_WIRE_ADDR(addr), val))
		return false;
	
	return swdReadRdbuf(&dummy);	//lets it cycle through
}

static bool memapSetAccessSize(uint8_t size)
{
	uint32_t val;

	if (!memapSimpleRead(REG_CSW, WRAP_UNALIGNED_POINTER_32(&val)))
		return false;

	val = (val & 0xFFFFFFC8UL) | 0x10 | size;	//autoincremengint access of given size

	if (!memapSimpleWrite(REG_CSW, val))
		return false;

	if (!memapSimpleRead(REG_CSW, WRAP_UNALIGNED_POINTER_32(&val)))
		return false;

	return (((uint8_t)val) & 0x37) == (0x10 | size);
}

uint8_t memapInit(uint8_t apSel, unaligned_uint32_t* romTabP)
{
	uint32_t val;
	
	if (!memapReselect(apSel))
		return MEMAP_INIT_AP_SEL_FAIL;
	
	//get cfg
	if (!memapSimpleRead(REG_CFG, WRAP_UNALIGNED_POINTER_32(&val)))
		return MEMAP_INIT_CFG_READ_FAIL;
	
	//we only support LE memory
	if (val & 1)
		return MEMAP_INIT_MEM_IS_BE;
	
	if (!memapSetAccessSize(MEMAP_SIZE_WORD))
		return MEMAP_INIT_ACCESS_SIZE_SET_ERR;
	
	if (romTabP && !memapSimpleRead(REG_BASE, romTabP))
		return MEMAP_INIT_ROM_TAB_ADDR_READ_FAIL;
	
	return MEMAP_OK;
}

bool memapSetAddr(uint32_t addr)
{
	mCurAddr = addr;
	
	if (memapSimpleWrite(REG_TAR, addr))
		return true;
	
	swdWriteAbort(0x1F);
	return false;
}

static bool memapHandleIncrement(bool *autoincrRanOutP, unaligned_uint32_t *prevRdbufP)		//return success
{
	uint32_t newAddr = mCurAddr + 4;
	
	*autoincrRanOutP = false;
	if ((newAddr ^ mCurAddr) &~ CSW_AUTOINCR_VALID_RANGE) {		//we've run out of autoincrementing ability
		
		if (prevRdbufP)
			if (!swdReadRdbuf(prevRdbufP))
				return false;
		
		if (!memapSetAddr(newAddr))
			return false;
		
		*autoincrRanOutP = true;
	}
	
	mCurAddr = newAddr;
	
	return true;
}

bool memapReadMultiple(unaligned_uint32_t *dst, uint16_t num)
{
	unaligned_uint32_t dummy;
	bool autoincrRanOut = false;
	
	if (!swdWireBusRead(1, REG_TO_WIRE_ADDR(REG_DRW), &dummy))
		goto fail;
	
	while (num-- > 1) {
		if (!memapHandleIncrement(&autoincrRanOut, dst))
			goto fail;
		
		if (autoincrRanOut) {
			if (!swdWireBusRead(1, REG_TO_WIRE_ADDR(REG_DRW), &dummy))
				goto fail;
		}
		else {
			if (!swdWireBusRead(1, REG_TO_WIRE_ADDR(REG_DRW), dst))
				goto fail;
		}
		dst++;
	}
	
	if (!swdReadRdbuf(dst))
		goto fail;
		
	if (!memapHandleIncrement(&autoincrRanOut, NULL))
			goto fail;
	
	return true;
	
fail:
	swdWriteAbort(0x1F);	//clear error
	return false;
}

bool memapWriteMultiple(const unaligned_uint32_t *vals, uint16_t num)
{
	unaligned_uint32_t dummy;
	bool autoincrRanOut = false;
	
	while (num--) {
		
		if (!swdWireBusWrite(1, REG_TO_WIRE_ADDR(REG_DRW), UNALIGNED(vals++)))
			goto fail;
		
		if (!memapHandleIncrement(&autoincrRanOut, NULL))
			goto fail;
	}
	
	if (!swdReadRdbuf(&dummy))
		goto fail;
	
	return true;
	
fail:
	swdWriteAbort(0x1F);	//clear error
	return false;
}

bool memapRead(unaligned_uint32_t *valP)
{
	return memapReadMultiple(valP, 1);
}

bool memapWrite(uint32_t val)
{
	return memapWriteMultiple(WRAP_UNALIGNED_POINTER_32(&val), 1);
}

bool memapReadAddr(uint32_t addr, unaligned_uint32_t *valP)
{
	if (memapSetAddr(addr) && memapRead(valP))
		return true;
	
	swdWriteAbort(0x1F);	//clear error
	return false;
}

bool memapWriteAddr(uint32_t addr, uint32_t val)
{
	if (memapSetAddr(addr) && memapWrite(val))
		return true;
	
	swdWriteAbort(0x1F);	//clear error
	return false;
}