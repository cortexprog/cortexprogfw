#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "ops.h"

#define NONNULL_RET_VAL		((void*)1)

static void* infoOpParse(const char *argv0, int *argcP, char ***argvP, uint32_t *needFlagsP)
{
	*needFlagsP |= TOOL_OP_WANTS_DEBUGGER | TOOL_OP_WANTS_CPU | TOOL_OP_WANTS_SCRIPT | TOOL_OP_FLAG_SET_INFO_MODE;
	
	return NONNULL_RET_VAL;
}

static bool infoOpDo(void *toolOpData, uint32_t step, uint32_t flags, struct Debugger *dbg, struct Cpu *cpu, struct Script *scpt)
{
	//nothing to do really - this op is a no-op as info is always shown
	
	return true;
}

static void infoOpFree(void *toolOpData)
{
	//nothing
}

static void infoOpHelp(const char *argv0)	//help for "info"
{
	fprintf(stderr,
		"USAGE: %s info\n"
		"\tJust shows the information on the attached debugged chip\n", argv0);
}

DEFINE_OP(info, infoOpParse, infoOpDo, infoOpFree, infoOpHelp);