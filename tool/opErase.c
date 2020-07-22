#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "utilOp.h"
#include "memio.h"
#include "ops.h"

struct ToolOpDataErase {
	uint32_t addr, len;
	bool haveAddr;
};

static void* eraseOpParse(const char *argv0, int *argcP, char ***argvP, uint32_t *needFlagsP)
{
	struct ToolOpDataErase *data;
	long long vAddr, vLen;
	bool haveAddr = false;
	
	if (*argcP >= 2 && 1 == sscanf((*argvP)[0], "%lli", &vAddr) && 1 == sscanf((*argvP)[1], "%lli", &vLen)) {
	
		(*argvP) += 2;
		(*argcP) -= 2;
		haveAddr = true;
	}
	
	if (vAddr < 0 || vLen < 0) {
		fprintf(stderr, " erase: address and length for \"erase\" command look invalid\n");
		return NULL;
	}

	*needFlagsP |= TOOL_OP_NEEDS_DEBUGGER | TOOL_OP_NEEDS_CPU | TOOL_OP_NEEDS_SCRIPT | TOOL_OP_NEEDS_SCPT_ERASEBLOCK;
	
	data = calloc(1, sizeof(struct ToolOpDataErase));
	if (!data)
		return NULL;
	
	data->addr = (uint32_t)vAddr;
	data->len = (uint32_t)vLen;
	data->haveAddr = haveAddr;
	
	return data;
}

static bool eraseOpDo(void *toolOpData, uint32_t step, uint32_t flags, struct Debugger *dbg, struct Cpu *cpu, struct Script *scpt)
{
	struct ToolOpDataErase *data = (struct ToolOpDataErase*)toolOpData;
	struct UtilProgressState state = {0,};
	
	if (step == TOOL_OP_STEP_POST_SCRIPT_INIT) {
		if (!data->haveAddr) {
			
			if (!utilOpFindLargestFlashArea(scpt, &data->addr, &data->len))
				return false;
		}
		
		if (((uint64_t)data->addr + data->len) > (1ULL << 32)) {
			
			fprintf(stderr, " erase: address and length for \"erase\" command appear invalid\n");
			return false;
		}
	}
	
	if (step == TOOL_OP_STEP_MAIN) {
		
		uint32_t ofst, suggestedLen = 0;
		
		//test & fix range
		if (!scriptIsValidFlashRange(scpt, data->addr, data->len, false, &suggestedLen)) {
			if (!suggestedLen) {
				fprintf(stderr, " erase: requested write boundaries impossible 0x%08X + 0x%08X\n", data->addr, data->len);
				return false;
			}
			else {
				fprintf(stderr, " erase: length 0x%08X is not properly aligned. Suggested: 0x%08X\n", data->len, suggestedLen);
				return false;
			}
		}
		
		//do the erase
		for (ofst = 0; ofst < data->len;) {
			uint32_t sizeNow = scriptGetFlashBlockSize(scpt, data->addr + ofst, false);
			
			utilOpShowProgress(&state, "ERASING", data->addr, data->len, ofst, true);
			
			if (!scriptEraseBlock(scpt, data->addr + ofst)) {
				fprintf(stderr, " erase: failed to erase %u bytes to 0x%08X\n", sizeNow, data->addr + ofst);
				return false;
			}
			
			ofst += sizeNow;
		}
		utilOpShowProgress(&state, "ERASING", data->addr, data->len, ofst, true);
	}
	
	return true;
}

static void eraseOpFree(void *toolOpData)
{
	struct ToolOpDataErase *data = (struct ToolOpDataErase*)toolOpData;
	
	free(data);
}

static void eraseOpHelp(const char *argv0)	//help for "erase"
{
	fprintf(stderr,
		"USAGE: %s erase [start_addr length]\n"
		"\tErases flash. If no address and length is given, the longest\n"
		"\tcontiguous flash range in the chip is erased (for most chips\n"
		"\tthis means the entire chip is erased). This operation will not\n"
		"\twork if your chip's script does not support the \"erase\" op.\n"
		"\tIn that case try the \"eraseall\" command instead.", argv0);
}

DEFINE_OP(erase, eraseOpParse, eraseOpDo, eraseOpFree, eraseOpHelp);