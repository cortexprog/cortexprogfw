#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "utilOp.h"
#include "memio.h"
#include "ops.h"


#define NONNULL_RET_VAL		((void*)1)

static void* eraseAllOpParse(const char *argv0, int *argcP, char ***argvP, uint32_t *needFlagsP)
{
	*needFlagsP |= TOOL_OP_NEEDS_DEBUGGER | TOOL_OP_NEEDS_CPU | TOOL_OP_NEEDS_SCRIPT | TOOL_OP_NEEDS_SCPT_ERASEALL;
	
	return NONNULL_RET_VAL;
}

static bool eraseAllOpDo(void *toolOpData, uint32_t step, uint32_t flags, struct Debugger *dbg, struct Cpu *cpu, struct Script *scpt)
{
	if (step == TOOL_OP_STEP_MAIN) {
		
		//do the erase
		fprintf(stderr, "Performing \"erase all\"...\n");
		if (!scriptEraseAll(scpt)) {
			fprintf(stderr, " eraseall: failed\n");
			return false;
		}
		
		fprintf(stderr, " done\n");
	}
	
	return true;
}

static void eraseAllOpFree(void *toolOpData)
{
	//nothing to do here
}

static void eraseAllOpHelp(const char *argv0)	//help for "eraseall"
{
	fprintf(stderr,
		"USAGE: %s eraseall\n"
		"\tPerforms an \"erase all\" operation on the chip. What\n"
		"\tspecifically is erased depends on the chip. In most cases,\n"
		"\tall flash is erased. This operation will not work if your\n"
		"\tchip's script does not support the \"erase all\" op. In\n"
		"\tthat case try the \"erase\" command instead.", argv0);
}

DEFINE_OP(eraseall, eraseAllOpParse, eraseAllOpDo, eraseAllOpFree, eraseAllOpHelp);