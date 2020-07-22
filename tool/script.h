#ifndef _SCRIPT_H_
#define _SCRIPT_H_

#include <stddef.h>
#include "cpu.h"

struct Script;

#define SCRIPT_OP_FLAG_HAVE_ERASE_ALL		1
#define SCRIPT_OP_FLAG_HAVE_ERASE_BLOCK		2
#define SCRIPT_OP_FLAG_HAVE_WRITE_BLOCK		4


//init and deinit
struct Script* scriptLoad(struct Cpu* cpu, bool verbose);
bool scriptInit(struct Script *scpt, bool verbose);
void scriptFree(struct Script *scpt);

//info
uint32_t scriptGetFlashWriteStageAreaAddr(const struct Script *scpt);	//data is cached internally by scriptInit();
uint32_t scriptGetSupportedOps(const struct Script *scpt);				//->{SCRIPT_OP_FLAG_HAVE_}, data is cached internally by scriptInit();
bool scriptIsValidFlashRange(const struct Script *scpt, uint32_t start, uint32_t len, bool forWrites, uint32_t *suggestedLenP);	//show error to stdout if len is bad and suggestedLenP == NULL, else store suggested len to suggestedLenP. Will store 0 if no possibility exists (len is too big)
uint32_t scriptGetFlashBlockSize(const struct Script *scpt, uint32_t base, bool forWrites);
bool scriptGetNthContiguousFlashAreaInfo(const struct Script *scpt, uint32_t n, uint32_t *areaStartP, uint32_t *areaLenP, const char **nameP);	//just to get contig mem areas
const char *scriptGetAreaName(const struct Script *scpt, uint32_t addr);		//may be NULL 

//ops
bool scriptEraseAll(struct Script *scpt);
bool scriptEraseBlock(struct Script *scpt, uint32_t base);
bool scriptWriteBlock(struct Script *scpt, uint32_t base);


#endif
