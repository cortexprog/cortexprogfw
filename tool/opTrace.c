#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "utilOp.h"
#include "memio.h"
#include "ops.h"

struct ToolOpDataTrace {
	uint32_t addr;
	bool noreset;
};


static void* traceOpParse(const char *argv0, int *argcP, char ***argvP, uint32_t *needFlagsP)
{
	struct ToolOpDataTrace *ret;
	bool noreset = false;
	long long vAddr;
	
	if (*argcP && !strcmp("noreset", *(*argvP))) {
		
		noreset = true;
		(*argvP)++;
		(*argcP)--;
	}
	
	if (!*argcP || 1 != sscanf((*argvP)[0], "%lli", &vAddr) || (vAddr & 3) || (vAddr >> 32)) {
		
		fprintf(stderr, " trace: trace mailbox address invalid or not given\n");
		return NULL;
	}
	
	(*argvP)++;
	(*argcP)--;
	
	*needFlagsP |= TOOL_OP_NEEDS_DEBUGGER | TOOL_OP_NEEDS_CPU;
	
	ret = malloc(sizeof(*ret));
	if (!ret)
		return NULL;
	
	ret->noreset = noreset;
	ret->addr = vAddr;
	
	return ret;
}

static bool traceOpDo(void *toolOpData, uint32_t step, uint32_t flags, struct Debugger *dbg, struct Cpu *cpu, struct Script *scpt)
{
	struct ToolOpDataTrace *data = (struct ToolOpDataTrace*)toolOpData;
	uint32_t traceAddr = data->addr;
	
	if (step == TOOL_OP_STEP_MAIN) {
		
		int32_t i, nBytes;
		char *buf;
		
		fprintf(stderr, "TRACING using mailbox at [0x%08X]. Press [ENTER] to terminate\n", traceAddr);
		
		if (!data->noreset)
			(void)cpuReset(cpu);
		
		while(!utilGetKey() && (nBytes = cpuTraceLogRead(cpu, traceAddr, (void**)&buf)) >= 0) {
			
			for (i = 0; i < nBytes; i++)
				putchar(buf[i]);
			
			free(buf);
		}
	}
	
	return true;
}

static void traceOpFree(void *toolOpData)
{
	struct ToolOpDataTrace *data = (struct ToolOpDataTrace*)toolOpData;
	
	free(data);
}

static void traceOpHelp(const char *argv0)	//help for "trace"
{
	fprintf(stderr,
		"USAGE: %s trace  [\"noreset\"] trace_addr\n"
		"\tZeroWireTrace the running application. Use the given\n"
		"\taddress as the mailbox. Any writeable word in RAM or\n"
		"\tMMIO will do. Anytime during tracing, pressing [ENTER]\n"
		"\twill stop the tracing. Note that this may block the\n"
		"\tdebugged program. The \"noreset\" param tells CortexProg\n"
		"\tTo not reest the chip before starting.\n", argv0);
}

DEFINE_OP(trace, traceOpParse, traceOpDo, traceOpFree, traceOpHelp);