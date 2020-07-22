#ifdef WIN32
#include <windows.h>
#include "psapi.h"
#pragma comment(lib, "Psapi.lib")
#endif
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "debugger.h"
#include "alloca.h"
#include "script.h"
#include "memio.h"
#include "types.h"
#include "cpu.h"
#include "ops.h"

#define TOOL_VERSION	0x01030000


struct ToolOp {
	struct ToolOp *next;
	const struct ToolOpInfo *funcs;
	void *data;
};

static struct ToolOpInfo *mTools = NULL;



void opsSubscribeOp(struct ToolOpInfo *opNfo)
{
	opNfo->next = mTools;
	mTools = opNfo;
}

const struct ToolOpInfo *opsGetFirst(void)
{
	return mTools;
}

static bool opsRun(const struct ToolOp *ops, uint32_t step, uint32_t flags, struct Debugger *dbg, struct Cpu *cpu, struct Script *scpt)
{
	const struct ToolOp *op;
	
	for (op = ops; op; op = op->next) {
		
		if (!op->funcs->toolOpDo(op->data, step, flags, dbg, cpu, scpt))	//assume error printed by op if needed
			return false;
	}
	
	return true;
}

static void freeOps(struct ToolOp *ops)
{
	struct ToolOp *op;
	
	while (ops) {
		op = ops;
		ops = ops->next;
		op->funcs->toolOpDataFree(op->data);
		free(op);
	}
}

static bool verifyScriptSupportsOpsWeNeed(struct Script *scpt, uint32_t needIt, uint32_t respectiveScriptFlag, const char *opFriendlyName)
{
	if (!needIt || (scriptGetSupportedOps(scpt) & respectiveScriptFlag))
		return true;
	
	fprintf(stderr, "ERROR: This script does not support the needed \"%s\" operation\n", opFriendlyName);
	return false;
}

int main(int argc, char** argv)
{
	const char *self = *argv++, *wantedDebuggerSnum = NULL;
	struct ToolOp *opsH = NULL, *opsT = NULL, *op;
	uint32_t needFlags = 0, opsFlags = 0;
	const struct ToolOpInfo *nfo;
	struct Debugger *dbg = NULL;
	struct Script *scpt = NULL;
	struct Cpu *cpu = NULL;
	int ret = -1;
	
	
	argc--;
	
	//verify we have params
	if (argc <= 0) {
		fprintf(stderr, "No command given. Try:\n\t%s help\n", self);
		goto out;
	}
	
	//see if we have a debugger serial number
	if (argc > 0 && !strcmp(*argv, "-s")) {
		
		argc--;
		argv++;
		
		if (argc) {
			
			wantedDebuggerSnum = *argv++;
			argc--;
		}
		else {
			//will fail later, as required
			wantedDebuggerSnum = DEBUGGER_SNUM_GUARANTEED_ILLEGAL;
		}
		needFlags |= TOOL_OP_WANTS_DEBUGGER;
	}
	
	//see if we have a speed param
	
	
	//first parse all params
	while (argc > 0) {
		
		const char *curCmd = *argv++;
		void *opData = NULL;
		argc--;
		
		for (nfo = opsGetFirst(); nfo && nfo->name; nfo = nfo->next) {
			
			if (strcmp(nfo->name, curCmd))
				continue;
			
			opData = nfo->toolOpCmdParse(self, &argc, &argv, &needFlags);
			if (!opData)	//parsing error occured -> bail out (assume error has been printed by parser)
				goto out;
			break;
		}
		
		if (!opData) {		//command not found
			fprintf(stderr, "Command '%s' unknown. Try:\n\t%s help\n", curCmd, self);
			goto out;
		}
		
		if (needFlags & TOOL_OP_FLAG_HELP_ONLY) {	//we saw help flag - destroy all other ops and parse no more
			
			freeOps(opsH);
			opsH = opsT = NULL;
			argc = 0;
		}
		
		if (needFlags & TOOL_OP_FLAG_SET_INFO_MODE)	//info mode activated
			opsFlags = TOOL_OP_FLAG_INFO_MODE;
		
		op = calloc(1, sizeof(struct ToolOp));
		if (!op) {			//if this fails, you've got issues...
			nfo->toolOpDataFree(opData);
			goto out;
		}
		
		op->funcs = nfo;
		op->data = opData;
		if (opsT)
			opsT->next = op;
		else
			opsH = op;
		opsT = op;
	}

	if (opsFlags & TOOL_OP_FLAG_INFO_MODE)
		fprintf(stderr, "CortexProg tool ver %u.%u.%u.%u (c) 2017 CortexProg.com\n", (TOOL_VERSION >> 24) & 0xff, (TOOL_VERSION >> 16) & 0xff, (TOOL_VERSION >> 8) & 0xff, (TOOL_VERSION >> 0) & 0xff);

	//run pre-debugger steps
	if (!opsRun(opsH, TOOL_OP_STEP_PRE_DEBUGGER, opsFlags, dbg, cpu, scpt))
		goto out;

	//if we want or need a debugger, get it now
	if (needFlags & (TOOL_OP_WANTS_DEBUGGER | TOOL_OP_NEEDS_DEBUGGER)) {
		
		dbg = debuggerOpen(wantedDebuggerSnum, !!(opsFlags & TOOL_OP_FLAG_INFO_MODE));
		if (!dbg && (needFlags & TOOL_OP_NEEDS_DEBUGGER))
			goto out;
	}
	
	//run pre-debugger-id steps
	if (!opsRun(opsH, TOOL_OP_STEP_PRE_DEBUGGER_ID, opsFlags, dbg, cpu, scpt))
		goto out;

	//init the debugger (separate because things like fwupdate cannot reply on debugger replying properly to our init code)
	if (needFlags & (TOOL_OP_WANTS_DEBUGGER | TOOL_OP_NEEDS_DEBUGGER)) {
		
		if (!dbg || (!debuggerInit(dbg, !!(opsFlags & TOOL_OP_FLAG_INFO_MODE)) && (needFlags & TOOL_OP_NEEDS_DEBUGGER)))
			goto out;
	}

	//run pre-CPUID steps
	if (!opsRun(opsH, TOOL_OP_STEP_PRE_CPUID, opsFlags, dbg, cpu, scpt))
		goto out;

	if (dbg && (needFlags & (TOOL_OP_WANTS_CPU | TOOL_OP_NEEDS_CPU)))
		cpu = cpuAttach(dbg, !!(opsFlags & TOOL_OP_FLAG_INFO_MODE));
	if (!cpu && (needFlags & TOOL_OP_NEEDS_CPU))
		goto out;

	//run pre-script steps
	if (!opsRun(opsH, TOOL_OP_STEP_PRE_SCRIPT, opsFlags, dbg, cpu, scpt))
		goto out;

	//if we need to load the script, do so now
	if (cpu && (needFlags & (TOOL_OP_WANTS_SCRIPT | TOOL_OP_NEEDS_SCRIPT)))
		scpt = scriptLoad(cpu, !!(opsFlags & TOOL_OP_FLAG_INFO_MODE));
	
	if (!scpt && (needFlags & TOOL_OP_NEEDS_SCRIPT))
		goto out;

	//run post-script steps
	if (!opsRun(opsH, TOOL_OP_STEP_POST_SCRIPT, opsFlags, dbg, cpu, scpt))
		goto out;
	
	if (scpt && !scriptInit(scpt, !!(opsFlags & TOOL_OP_FLAG_INFO_MODE)))
		goto out;
	
	//verify script is capable of what we need
	if (!verifyScriptSupportsOpsWeNeed(scpt, needFlags & TOOL_OP_NEEDS_SCPT_WRITE, SCRIPT_OP_FLAG_HAVE_WRITE_BLOCK, "write block"))
		goto out;
	if (!verifyScriptSupportsOpsWeNeed(scpt, needFlags & TOOL_OP_NEEDS_SCPT_ERASEBLOCK, SCRIPT_OP_FLAG_HAVE_ERASE_BLOCK, "erase block"))
		goto out;
	if (!verifyScriptSupportsOpsWeNeed(scpt, needFlags & TOOL_OP_NEEDS_SCPT_ERASEALL, SCRIPT_OP_FLAG_HAVE_ERASE_ALL, "erase all"))
		goto out;
	
	//run post-script-init steps
	if (!opsRun(opsH, TOOL_OP_STEP_POST_SCRIPT_INIT, opsFlags, dbg, cpu, scpt))
		goto out;
	
	//run main steps
	if (!opsRun(opsH, TOOL_OP_STEP_MAIN, opsFlags, dbg, cpu, scpt))
		goto out;
	
	//we're done
	ret = 0;

out:

	if (scpt)
		scriptFree(scpt);

	if (cpu)
		cpuFree(cpu);

	if (dbg)
		debuggerClose(dbg);

	freeOps(opsH);
	return ret;
}