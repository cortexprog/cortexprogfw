#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "ops.h"


struct ToolOpDataHelp {
	const char *argv0;		//alive always and not freed/alloced by us
	char *cmd;
};

static void* helpOpParse(const char *argv0, int *argcP, char ***argvP, uint32_t *needFlagsP)
{
	struct ToolOpDataHelp *ret = calloc(1, sizeof(struct ToolOpDataHelp));
	
	if (!ret)
		return NULL;
	
	if (*argcP) {
		ret->cmd = strdup(**argvP);
		*argcP = 0;	//no commands allowed after help as it may not be clear if next command was param to help or an actual command
	}
	ret->argv0 = argv0;
	*needFlagsP = TOOL_OP_FLAG_HELP_ONLY;
	
	return ret;
}

static bool helpOpDo(void *toolOpData, uint32_t step, uint32_t flags, struct Debugger *dbg, struct Cpu *cpu, struct Script *scpt)
{
	struct ToolOpDataHelp *opData = (struct ToolOpDataHelp*)toolOpData;
	const struct ToolOpInfo *nfo;

	if (step == TOOL_OP_STEP_PRE_DEBUGGER) {
		
		const char *prev = NULL;
		
		if (opData->cmd) {
			for (nfo = opsGetFirst(); nfo; nfo = nfo->next) {
				if (strcmp(nfo->name, opData->cmd))
					continue;
			
				nfo->toolOpHelpFn(opData->argv0);
				return true;
			}
			//if no command is found, default to normal help
		}
		
		fprintf(stderr,
			"USAGE: %s [command [param1 [param2 [...]]]] [command2 [...]]\n"
			"Commands are executed in the order they are given.\n"
			"KNOWN COMMANDS:\n", opData->argv0);
		
		//show them in a sorted order
		
		for (nfo = opsGetFirst(); nfo; nfo = nfo->next) {
			
			const struct ToolOpInfo *cur;
			const char *best = NULL;
			
			for (cur = opsGetFirst(); cur && cur->name; cur = cur->next) {
					
					if (prev && strcmp(prev, cur->name) >= 0)	//should be greater than prev
						continue;
					if (best && strcmp(best, cur->name) <= 0)	//should be less than best
						continue;
					best = cur->name;
			}
			fprintf(stderr, "  %s\n", best);
			prev = best;
		}
		fprintf(stderr, "You may learn more about any command by trying:\n  %s help <COMMAND>\n", opData->argv0);
	}
	
	return true;
}

static void helpOpFree(void *toolOpData)
{
	struct ToolOpDataHelp *opData = (struct ToolOpDataHelp*)toolOpData;
	
	free(opData->cmd);
	free(opData);
}

static void helpOpHelp(const char *argv0)	//help for "help"
{
	fprintf(stderr,
		"USAGE: %s help [command]\n"
		"\tShows the generic help info, or one for a particular command.\n", argv0);
}



DEFINE_OP(help, helpOpParse, helpOpDo, helpOpFree, helpOpHelp);