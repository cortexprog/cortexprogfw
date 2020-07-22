#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "utilOp.h"
#include "memio.h"
#include "ops.h"

struct ToolOpDataWrite {
	FILE *file;
	uint32_t addr, len;
	bool noack, noerase, haveAddr, rigidLen;
};

static void* writeOpParse(const char *argv0, int *argcP, char ***argvP, uint32_t *needFlagsP)
{
	bool noack = false, noerase = false, haveAddr = false, haveLen = false;
	struct ToolOpDataWrite *data;
	const char *filename = NULL;
	long long vAddr, vLen;
	FILE *fil;
	
	if (!*argcP) {
		fprintf(stderr, " write: not enough arguments given for \"write\" command\n");
		return NULL;
	}
	
	filename = *(*argvP)++;
	(*argcP)--;
	
	//allow these in either order (just to be nice), but only once
	while (*argcP) {
		if (!noack && !strcmp("noack", *(*argvP))) {
			
			noack = true;
			(*argvP)++;
			(*argcP)--;
		}
		else if (!noerase && !strcmp("noerase", *(*argvP))) {
			
			noerase = true;
			(*argvP)++;
			(*argcP)--;
		}
		else
			break;
	}
		
	if (*argcP && 1 == sscanf((*argvP)[0], "%lli", &vAddr)) {
		
		(*argvP)++;
		(*argcP)--;
		haveAddr = true;
		
		if (*argcP && 1 == sscanf((*argvP)[0], "%lli", &vLen)) {
			

			(*argvP)++;
			(*argcP)--;
			haveLen = true;
		}
	}
	
	if (vAddr < 0 || vLen < 0) {
		fprintf(stderr, " write: address and length for \"write\" command look invalid\n");
		return NULL;
	}

	//now open the file (early in case we need its size)
	fil = fopen(filename, "rb");
	if (!fil) {
		fprintf(stderr, " write: unable to open '%s' for \"write\" command\n", filename);
		return NULL;
	}
	
	if (!haveLen) {
		if (fseek(fil, 0, SEEK_END) < 0 || (vLen = ftell(fil)) < 0 || fseek(fil, 0, SEEK_SET) < 0 || !vLen) {
			
			fprintf(stderr, " write: refusing to \"write\" empty file '%s'\n", filename);
			fclose(fil);
			return NULL;
		}
	}

	*needFlagsP |= TOOL_OP_NEEDS_DEBUGGER | TOOL_OP_NEEDS_CPU | TOOL_OP_NEEDS_SCRIPT | TOOL_OP_NEEDS_SCPT_WRITE;
	if (!noerase)
		*needFlagsP |= TOOL_OP_NEEDS_SCPT_ERASEBLOCK;
	
	data = calloc(1, sizeof(struct ToolOpDataWrite));
	if (!data) {
		fclose(fil);
		return NULL;
	}
	
	data->file = fil;
	data->addr = (uint32_t)vAddr;
	data->len = (uint32_t)vLen;
	data->noack = noack;
	data->noerase = noerase;
	data->haveAddr = haveAddr;
	data->rigidLen = haveLen;
	
	return data;
}

static bool writeOpDo(void *toolOpData, uint32_t step, uint32_t flags, struct Debugger *dbg, struct Cpu *cpu, struct Script *scpt)
{
	struct ToolOpDataWrite *data = (struct ToolOpDataWrite*)toolOpData;
	struct UtilProgressState stateErz = {0,};
	struct UtilProgressState stateWri = {0,};
	
	if (step == TOOL_OP_STEP_POST_SCRIPT_INIT) {
		if (!data->haveAddr) {
			
			if (!utilOpFindLargestFlashArea(scpt, &data->addr, NULL))
				return false;
		}
		
		if (((uint64_t)data->addr + data->len) > (1ULL << 32)) {
			
			fprintf(stderr, " write: address and length for \"write\" command appear invalid\n");
			return false;
		}
	}
	
	if (step == TOOL_OP_STEP_MAIN) {
		
		uint32_t ofst, suggestedLen = 0;
		
		//test & fix range
		if (!scriptIsValidFlashRange(scpt, data->addr, data->len, data->noerase, &suggestedLen)) {
			if (!suggestedLen) {
				fprintf(stderr, " write: requested write boundaries impossible 0x%08X + 0x%08X\n", data->addr, data->len);
				return false;
			}
			else if (data->rigidLen) {
				fprintf(stderr, " write: length 0x%08X is not properly aligned. Suggested: 0x%08X\n", data->len, suggestedLen);
				return false;
			}
			else {
				fprintf(stderr, " write: automatically rounding write up by %u bytes (to 0x%08X)\n", suggestedLen - data->len, suggestedLen);
				data->len = suggestedLen;
			}
		}
		//erase if needed
		if (!data->noerase) {
			
			for (ofst = 0; ofst < data->len;) {
				uint32_t sizeNow = scriptGetFlashBlockSize(scpt, data->addr + ofst, false);
				
				utilOpShowProgress(&stateErz, "ERASING", data->addr, data->len, ofst, true);
				
				if (!scriptEraseBlock(scpt, data->addr + ofst)) {
					fprintf(stderr, " write: failed to erase %u bytes to 0x%08X\n", sizeNow, data->addr + ofst);
					return false;
				}
				
				ofst += sizeNow;
			}
			utilOpShowProgress(&stateErz, "ERASING", data->addr, data->len, ofst, true);
		}
		
		for (ofst = 0; ofst < data->len;) {
			uint32_t sizeNow = scriptGetFlashBlockSize(scpt, data->addr + ofst, true);
			
			utilOpShowProgress(&stateWri, "UPLOADING", data->addr, data->len, ofst, true);
			
			if (!memioWriteFromFile(cpu, scriptGetFlashWriteStageAreaAddr(scpt), sizeNow, data->file, !data->noack, false, false)) {
				fprintf(stderr, " write: failed to upload %u bytes to 0x%08X\n", sizeNow, scriptGetFlashWriteStageAreaAddr(scpt));
				return false;
			}
			
			utilOpShowProgress(&stateWri, "WRITING", data->addr, data->len, ofst, true);
			
			if (!scriptWriteBlock(scpt, data->addr + ofst)) {
				fprintf(stderr, " write: failed to write %u bytes to 0x%08X\n", sizeNow, data->addr + ofst);
				return false;
			}
			
			ofst += sizeNow;
		}
		utilOpShowProgress(&stateWri, "DONE", data->addr, data->len, data->len, true);
	}
	
	return true;
}

static void writeOpFree(void *toolOpData)
{
	struct ToolOpDataWrite *data = (struct ToolOpDataWrite*)toolOpData;
	
	fclose(data->file);
	free(data);
}

static void writeOpHelp(const char *argv0)	//help for "write"
{
	fprintf(stderr,
		"USAGE: %s upload filename [noack] [noerase] [start_addr [length]]\n"
		"\tFlashes data from a file into Flash. Flash is erased before being\n"
		"\twritten. The \"noerase\"option disables this. The \"noack\"\n"
		"\toption allows for faster transfers. If no length is given,\n"
		"\tthe entire file is uploaded. If file is shorter then requested\n"
		"\tlength, zeroes are written up to the requeste dlength. If no\n"
		"\taddress is given, flash is written from the strat of the largest\n"
		"\tflash area forward.\n", argv0);
}

DEFINE_OP(write, writeOpParse, writeOpDo, writeOpFree, writeOpHelp);