#include "wire.h"
#include "swd.h"

//addressed for APnDP = 0
#define ADDR_IDCODE_R	0
#define ADDR_ABORT_W	0
#define ADDR_STAT_R	1
#define ADDR_CTRL_W	1
#define ADDR_TARGETID	1
#define ADDR_SELECT_W	2
#define ADDR_RDBUFF_R	3


#define AP_CLASS_MEMORY		0x08
#define AP_CONN_TYPE_AHB	0x01
#define AP_CONN_TYPE_APB	0x02
#define AP_CONN_TYPE_AXI	0x04



static inline bool swdWireBusReadInt(uint1_t ap, uint8_t a23, unaligned_uint32_t *valP)
{
	uint8_t ret;
	uint8_t tryNo = 0;
	
	do {
		ret = llWireBusRead(ap, a23, valP);
	} while ((ret &~ (uint8_t)BUS_DATA_PAR_ERR) == BUS_SWD_WAIT && ++tryNo);
	
	return BUS_SWD_ACK == ret;
}

static inline bool swdWireBusWriteInt(uint1_t ap, uint8_t a23, uint32_t val)
{
	uint8_t ret;
	uint8_t tryNo = 0;
	
	do {
		ret = llWireBusWrite(ap, a23, val);
	} while ((ret &~ (uint8_t)BUS_DATA_PAR_ERR) == BUS_SWD_WAIT && ++tryNo);
	
	return BUS_SWD_ACK == ret;
}

bool swdWireBusRead(uint1_t ap, uint8_t a23, unaligned_uint32_t *valP)
{
	return swdWireBusReadInt(ap, a23, valP);
}

bool swdWireBusWrite(uint1_t ap, uint8_t a23, uint32_t val)
{
	return swdWireBusWriteInt(ap, a23, val);
}

bool swdReadIdcode(unaligned_uint32_t *valP)
{
	return swdWireBusReadInt(0, ADDR_IDCODE_R, valP);
}

bool swdReadStat(unaligned_uint32_t *valP)
{
	return swdWireBusReadInt(0, ADDR_STAT_R, valP);
}

bool swdReadRdbuf(unaligned_uint32_t *valP)
{
	return swdWireBusReadInt(0, ADDR_RDBUFF_R, valP);
}

bool swdWriteAbort(uint32_t val)
{
	return swdWireBusWriteInt(0, ADDR_ABORT_W, val);
}

bool swdWriteSelect(uint32_t val)
{
	return swdWireBusWriteInt(0, ADDR_SELECT_W, val);
}

bool swdWriteCtrl(uint32_t val)
{
	return swdWireBusWriteInt(0, ADDR_CTRL_W, val);
}

uint8_t swdDapInit(void)
{
	uint8_t dapVer, i;
	uint32_t val;

	if (!swdReadIdcode(WRAP_UNALIGNED_POINTER_32(&val)))
		return SWD_ATTACH_IDCODE_READ_FAIL;

	dapVer = ((uint8_t)(((uint16_t)val) >> 12)) & 0x0F;
	if (!dapVer)
		return SWD_ATTACH_DAP_V0;

	if (!swdWriteAbort(0x1F))
		return SWD_ATTACH_REG_WRITE_FAIL;
	
	if (!swdWriteSelect(0))
		return SWD_ATTACH_REG_WRITE_FAIL;
	
	//power up cpu and debug infrastructure
	if (!swdWriteCtrl(0x50000000UL))
		return SWD_ATTACH_REG_WRITE_FAIL;
	
	//wait for powerup
	for (i = 0; i < 16; i++) {
	
		if (!swdReadStat(WRAP_UNALIGNED_POINTER_32(&val)))
			return SWD_ATTACH_REG_READ_FAIL;
	
		if ((uint8_t)(val >> 28) == 0xF)
			return SWD_OK;
	}

	return SWD_ATTACH_CTRL_STAT_VAL_ERR;
}

uint8_t swdAttachAndEnumApsWithAhbs(uint8_t *stateP)
{
	uint8_t apIdx = 0;
	uint32_t val;
	
	do {

		if (!swdWriteSelect((((uint32_t)apIdx) << 24) | 0xF0))
			return SWD_ATTACH_REG_WRITE_FAIL;

		if (!swdWireBusReadInt(1, 3, WRAP_UNALIGNED_POINTER_32(&val)))	//read IDR
			return SWD_ATTACH_REG_READ_FAIL;

		if (!swdReadRdbuf(WRAP_UNALIGNED_POINTER_32(&val)))
			return SWD_ATTACH_RDBUF_READ_FAIL;

		if (!val)
			break;
		
		//skip all APs that are not by "ARM", are not of class "memory" and not of type "AHB"
		if ((val & 0x0FFFFF0F) == 0x04770001) {
			//record it
			if (*stateP < apIdx + 1) {
				*stateP = apIdx + 1;
				return SWD_OK;
			}
		}
		
		apIdx++;
	} while(apIdx);
	
	return SWD_ATTACH_AHB_NOT_FOUND;
}