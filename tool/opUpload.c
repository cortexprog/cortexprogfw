#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "utilOp.h"
#include "memio.h"
#include "ops.h"

struct ToolOpDataUpload {
	FILE *file;
	uint32_t addr, len;
	bool noack;
};

static void* uploadOpParse(const char *argv0, int *argcP, char ***argvP, uint32_t *needFlagsP)
{
	bool noack = false, haveLen = false;
	struct ToolOpDataUpload *data;
	const char *filename = NULL;
	long long vSum, vAddr, vLen;
	FILE *fil;
	
	if (*argcP < 2) {
		fprintf(stderr, " upload: not enough arguments given for \"upload\" command\n");
		return NULL;
	}
	
	filename = *(*argvP)++;
	(*argcP)--;
	
	if (!strcmp("noack", *(*argvP))) {
		
		noack = true;
		(*argvP)++;
		(*argcP)--;
	}
	
	if (*argcP < 1) {
		fprintf(stderr, " upload: the \"upload\" command needs a destination address\n");
		return NULL;
	}
	
	if (1 == sscanf((*argvP)[0], "%lli", &vAddr)) {
		
		(*argvP)++;
		(*argcP)--;
		
		if (*argcP && 1 == sscanf((*argvP)[0], "%lli", &vLen)) {
			

			(*argvP)++;
			(*argcP)--;
			haveLen = true;
		}
	}
	
	//now open the file (early in case we need its size)
	fil = fopen(filename, "rb");
	if (!fil) {
		fprintf(stderr, " upload: unable to open '%s' for \"upload\" command\n", filename);
		return NULL;
	}
	
	if (!haveLen) {
		if (fseek(fil, 0, SEEK_END) < 0 || (vLen = ftell(fil)) < 0 || fseek(fil, 0, SEEK_SET) < 0 || !vLen) {
			
			fprintf(stderr, " upload: refusing to \"upload\" empty file '%s'\n", filename);
			fclose(fil);
			return NULL;
		}
	}

	vSum = vAddr + vLen;
	if (vAddr < 0 || vLen <= 0 || vSum >= (1ULL << 32)) {
		
		fprintf(stderr, " upload: address and length for \"upload\" command look invalid\n");
		fclose(fil);
		return NULL;
	}

	*needFlagsP |= TOOL_OP_NEEDS_DEBUGGER | TOOL_OP_NEEDS_CPU;
	
	data = calloc(1, sizeof(struct ToolOpDataUpload));
	if (!data) {
		fclose(fil);
		return NULL;
	}
	
	data->file = fil;
	data->addr = (uint32_t)vAddr;
	data->len = (uint32_t)vLen;
	data->noack = noack;
	
	return data;
}

static bool uploadOpDo(void *toolOpData, uint32_t step, uint32_t flags, struct Debugger *dbg, struct Cpu *cpu, struct Script *scpt)
{
	struct ToolOpDataUpload *data = (struct ToolOpDataUpload*)toolOpData;
	
	if (step == TOOL_OP_STEP_MAIN) {
		
		if (!memioWriteFromFile(cpu, data->addr, data->len, data->file, !data->noack, true, true)) {
			
			fprintf(stderr, " upload: failed to upload requested memory (0x%08X + 0x%08X)\n", data->addr, data->len);
			return false;
		}
	}
	
	return true;
}

static void uploadOpFree(void *toolOpData)
{
	struct ToolOpDataUpload *data = (struct ToolOpDataUpload*)toolOpData;
	
	fclose(data->file);
	free(data);
}

static void uploadOpHelp(const char *argv0)	//help for "upload"
{
	fprintf(stderr,
		"USAGE: %s upload filename [noack] start_addr [length]\n"
		"\tWrites data from a file into memory (RAM). The \"noack\"\n"
		"\toption allows for faster transfers. If no length is given,\n"
		"\tthe entire file is uploaded", argv0);
}

DEFINE_OP(upload, uploadOpParse, uploadOpDo, uploadOpFree, uploadOpHelp);