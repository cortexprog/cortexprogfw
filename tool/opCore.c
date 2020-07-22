#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "ops.h"

#define NONZERO_OR_MASK	0x80000000UL			//we or this with our core id to make sire we get a nonnull value

static void* coreOpParse(const char *argv0, int *argcP, char ***argvP, uint32_t *needFlagsP)
{
	long long coreid;
	
	if (*argcP < 1 || 1 != sscanf((*argvP)[0], "%lli", &coreid) || coreid < 0 || coreid > 0xffff) {
		fprintf(stderr, " core: no valid CORE ID provided\n");
		return NULL;
	}
	
	(*argvP)++;
	(*argcP)--;
	*needFlagsP |= TOOL_OP_NEEDS_DEBUGGER | TOOL_OP_NEEDS_CPU;
	
	return (void*)(uintptr_t)(NONZERO_OR_MASK | coreid);
}

static bool coreOpDo(void *toolOpData, uint32_t step, uint32_t flags, struct Debugger *dbg, struct Cpu *cpu, struct Script *scpt)
{
	uint16_t coreId = (uint16_t)(uintptr_t)toolOpData;
	
	if (step >= TOOL_OP_STEP_PRE_SCRIPT && !cpuSelectCore(cpu, coreId)) {
		
		fprintf(stderr, " core: unable to set core to 0x%04x for step %u\n", coreId, step);
		return false;
	}
	
	return true;
}

static void coreOpFree(void *toolOpData)
{
	//nothing
}

static void coreOpHelp(const char *argv0)	//help for "core"
{
	fprintf(stderr,
		"USAGE: %s core <CORE ID>\n"
		"\tUse a given CPU CORE to perform further operations.\n"
		"\tOnly useful on a multi-core chip. You can use\n"
		"\t'%s info' command to get a list of CORE IDs\n",
		argv0, argv0);
}

DEFINE_OP(core, coreOpParse, coreOpDo, coreOpFree, coreOpHelp);