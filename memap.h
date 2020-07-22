#ifndef _MEM_AP_H_
#define _MEM_AP_H_

#include <stdbool.h>
#include <stdint.h>
#include "util.h"


#define MEMAP_OK							0
#define MEMAP_INIT_CFG_READ_FAIL			1
#define MEMAP_INIT_MEM_IS_BE				2
#define MEMAP_INIT_ACCESS_SIZE_SET_ERR		3
#define MEMAP_INIT_ROM_TAB_ADDR_READ_FAIL	4
#define MEMAP_INIT_ROM_TAB_REG_INVAL		5
#define MEMAP_INIT_AP_SEL_FAIL				6

//init/reinit
uint8_t memapInit(uint8_t apSel, unaligned_uint32_t* romTabP);	//selects the ap to do things. return rom table addr or 0 is fail
bool memapReselect(uint8_t apSel);								//tobe done when changing APs

//lowest level funcs
bool memapSetAddr(uint32_t addr);
bool memapReadMultiple(unaligned_uint32_t *dst, uint16_t num);
bool memapWriteMultiple(const unaligned_uint32_t *vals, uint16_t num);

//middle-layer funcs
bool memapRead(unaligned_uint32_t *valP); //autoincrements. not fast, if need multiple do it another way
bool memapWrite(uint32_t val); //autoincrements. not fast, if need multiple do it another way

//convenience wrappers
bool memapReadAddr(uint32_t addr, unaligned_uint32_t *valP);
bool memapWriteAddr(uint32_t addr, uint32_t val);



#endif
