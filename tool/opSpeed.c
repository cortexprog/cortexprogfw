#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "ops.h"

//we use the top bit of the pointer for marker for speed

static void* speedOpParse(const char *argv0, int *argcP, char ***argvP, uint32_t *needFlagsP)
{
	long long clock;
	
	*needFlagsP |= TOOL_OP_NEEDS_DEBUGGER;
	
	if (!*argcP) {
		fprintf(stderr, " speed: \"speed\" command needs a parameter\n");
		return NULL;
	}
	
	if(1 != sscanf((*argvP)[0], "%lli", &clock) || (clock < 0) || (clock > 1000000000UL /* 1GHz is too fast anyways */)) {
		
		fprintf(stderr, " speed: given speed is invalid\n");
		return NULL;
	}
	
	(*argvP)++;
	(*argcP)--;
	
	return (void*)(uintptr_t)(0x80000000UL | clock);
}

static bool speedOpDo(void *toolOpData, uint32_t step, uint32_t flags, struct Debugger *dbg, struct Cpu *cpu, struct Script *scpt)
{
	uint32_t resultantClock, clock = ((uint32_t)(uintptr_t)toolOpData) &~ 0x80000000UL;
	
	if (step > TOOL_OP_STEP_PRE_DEBUGGER_ID) {				//for complex scenarios this makes sense - each step after debugger exists we do this
		
		if (debuggerCanSupportVariableClockSpeed(dbg)) {
			
			if (!debuggerClockCtlSet(dbg, clock, &resultantClock)) {
				fprintf(stderr, " speed: error setting clock speed\n");
				return false;
			}
			if ((flags & TOOL_OP_FLAG_INFO_MODE) && step == TOOL_OP_STEP_MAIN)	//log only in main step
				fprintf(stderr, " speed: set clock to %u.%06uMHz (requested was %u.%06uMHz)\n", resultantClock / 1000000, resultantClock % 1000000, clock / 1000000, clock % 1000000);
					
			return true;
		}
		else {
			
			fprintf(stderr, " speed: this debugger has no software clock control.\n");
			return false;
		}
	}
	
	return true;
}

static void speedOpFree(void *toolOpData)
{
	//nothing
}

static void speedOpHelp(const char *argv0)	//help for "speed"
{
	fprintf(stderr,
		"USAGE: %s speed CLOCKSPEED_IN_HZ\n"
		"\tIf the debugger is capable of clock speed control, this\n"
		"\tcommand controls that functionality. The parameter is the\n"
		"\tdesired SWD clock speed, in Hz. The earest possible speed\n"
		"\twill be chosen.\n", argv0);
}

DEFINE_OP(speed, speedOpParse, speedOpDo, speedOpFree, speedOpHelp);