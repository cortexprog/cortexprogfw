#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "utilOp.h"
#include "memio.h"
#include "ops.h"

struct ToolOpDataRead {
	FILE *file;
	uint32_t addr, len;
	bool haveAddrInfo;
};

static void* readOpParse(const char *argv0, int *argcP, char ***argvP, uint32_t *needFlagsP)
{
	struct ToolOpDataRead *data;
	const char *filename = NULL;
	bool haveAddrInfo = false;
	long long vAddr, vLen;
	FILE *fil;
	
	if (*argcP < 1) {
		fprintf(stderr, " read: filename not given for \"read\" command\n");
		return NULL;
	}
	
	filename = *(*argvP)++;
	(*argcP)--;
	
	if (*argcP >= 2) {		//see if we have two addresses
		
		long long vSum;
		
		if (1 == sscanf((*argvP)[0], "%lli", &vAddr)) {
			
			if (1 == sscanf((*argvP)[1], "%lli", &vLen)) {
				
				vSum = vAddr + vLen;
				
				if (vAddr < 0 || vLen <= 0 || vSum >= (1ULL << 32)) {
					
					fprintf(stderr, " read: address and length for \"read\" command look invalid\n");
					return NULL;
				}

				(*argvP) += 2;
				(*argcP) -= 2;
				haveAddrInfo = true;
			}
			else {
				
				fprintf(stderr, " read: address and length are both required for \"read\" command\n");
				return NULL;
			}
		}
	}
	
	//now open the file
	fil = fopen(filename, "wb");
	if (!fil) {
		fprintf(stderr, " read: unable to open '%s' for \"read\" command\n", filename);
		return NULL;
	}
	
	*needFlagsP |= TOOL_OP_NEEDS_DEBUGGER | TOOL_OP_NEEDS_CPU;
	
	if (!haveAddrInfo)		//if no address given, we will need the scrit to knoe what memory area we need
		*needFlagsP |= TOOL_OP_NEEDS_SCRIPT;
	
	data = calloc(1, sizeof(struct ToolOpDataRead));
	if (!data) {
		fclose(fil);
		return NULL;
	}
	
	data->file = fil;
	data->addr = (uint32_t)vAddr;
	data->len = (uint32_t)vLen;
	data->haveAddrInfo = haveAddrInfo;
	
	return data;
}

static bool readOpDo(void *toolOpData, uint32_t step, uint32_t flags, struct Debugger *dbg, struct Cpu *cpu, struct Script *scpt)
{
	struct ToolOpDataRead *data = (struct ToolOpDataRead*)toolOpData;
	
	if (step == TOOL_OP_STEP_POST_SCRIPT_INIT && !data->haveAddrInfo) {
		
		if (!utilOpFindLargestFlashArea(scpt, &data->addr, &data->len))
			return false;
		
		data->haveAddrInfo = true;
	}
	
	
	if (step == TOOL_OP_STEP_MAIN) {
		
		if (!memioReadToFile(cpu, data->addr, data->len, data->file, true, true)) {
			
			fprintf(stderr, " read: failed to read requested memory (0x%08X + 0x%08X)\n", data->addr, data->len);
			return false;
		}
	}
	
	return true;
}

static void readOpFree(void *toolOpData)
{
	struct ToolOpDataRead *data = (struct ToolOpDataRead*)toolOpData;
	
	fclose(data->file);
	free(data);
}

static void readOpHelp(const char *argv0)	//help for "read"
{
	fprintf(stderr,
		"USAGE: %s read filename [start_addr length]\n"
		"\tReads memory into a given filename. If no address & length is given,\n"
		"\tlargest flash area is read\n", argv0);
}

DEFINE_OP(read, readOpParse, readOpDo, readOpFree, readOpHelp);