#ifndef _SWD_H_
#define _SWD_H_


#include <stdbool.h>
#include <stdint.h>
#include "util.h"
#include "wire.h"



//mega low-level things
bool swdWireBusRead(uint1_t ap, uint8_t a23, unaligned_uint32_t *valP);
bool swdWireBusWrite(uint1_t ap, uint8_t a23, uint32_t val);


//super low level things
bool swdReadIdcode(unaligned_uint32_t *valP);
bool swdReadStat(unaligned_uint32_t *valP);
bool swdReadRdbuf(unaligned_uint32_t *valP);
bool swdWriteAbort(uint32_t val);
bool swdWriteSelect(uint32_t val);
bool swdWriteCtrl(uint32_t val);

//medium level
#define SWD_OK							0
#define SWD_ATTACH_IDCODE_READ_FAIL		1
#define SWD_ATTACH_DAP_V0				2
#define SWD_ATTACH_REG_WRITE_FAIL		3	//some reg write failed
#define SWD_ATTACH_REG_READ_FAIL		4	//some reg read failed
#define SWD_ATTACH_RDBUF_READ_FAIL		5
#define SWD_ATTACH_CTRL_STAT_VAL_ERR	6
#define SWD_ATTACH_AHB_NOT_FOUND		7	//returned at end of enum. not necessarily error (if there were results)


uint8_t swdDapInit(void);	//returns the error code above

//returns above error codes, takes state. start it at zero. returns SWD_ATTACH_AHB_NOT_FOUND when no more results
uint8_t swdAttachAndEnumApsWithAhbs(uint8_t *stateP);

//convert above iterator state into a valid apIdx
static inline uint8_t swdStateToApIdx(uint8_t state)
{
	return state - 1;	
}

#endif

