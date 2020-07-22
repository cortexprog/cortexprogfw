#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "ops.h"

#ifdef WIN32
#define strcasecmp _stricmp  
#endif

//we use the top bit to make our value not be zero

#define VOLTAGE_ABSOLUTE_MAX					5000	//certainly we cannot supply anything over 5V
#define VOLTAGE_FOR_CMD_ON						3300	//the voltage for old-style debugger (also default for "on" command)

#define VOLTAGE_SLOP							100		//generic slop for various "close-enough" cases

static void* powerOpParse(const char *argv0, int *argcP, char ***argvP, uint32_t *needFlagsP)
{
	long long voltage;
	
	*needFlagsP |= TOOL_OP_NEEDS_DEBUGGER;
	
	
	if (!*argcP) {
		fprintf(stderr, " power: \"power\" command needs a parameter\n");
		return NULL;
	}
	
	if (!strcasecmp((*argvP)[0], "on"))
		voltage = VOLTAGE_FOR_CMD_ON;
	else if (!strcasecmp((*argvP)[0], "off"))
		voltage = 0;
	else if(1 != sscanf((*argvP)[0], "%lli", &voltage) || (voltage < 0) || (voltage > VOLTAGE_ABSOLUTE_MAX)) {
		
		fprintf(stderr, " power: given voltage invalid\n");
		return NULL;
	}
	
	(*argvP)++;
	(*argcP)--;
	
	return (void*)(uintptr_t)(0x80000000UL | voltage);
}

static bool powerOpDo(void *toolOpData, uint32_t step, uint32_t flags, struct Debugger *dbg, struct Cpu *cpu, struct Script *scpt)
{
	uint32_t voltage = ((uint32_t)(uintptr_t)toolOpData) &~ 0x80000000UL;
	
	if ((step == TOOL_OP_STEP_PRE_CPUID && voltage > VOLTAGE_SLOP) ||		//for anything nonzero, do it early so that debugger can proceed
		(step == TOOL_OP_STEP_MAIN && voltage <= VOLTAGE_SLOP)) {			//for zero, shut down after all things are likely done
		
		if (debuggerCanSupportVariablePowerCtl(dbg)) {
			
			//variable voltage control - we will try it
	
			if (!debuggerPowerCtlVariable(dbg, voltage)) {
				fprintf(stderr, " power: setting failed\n");
				return false;
			}
			return true;
		}
		else if (debuggerCanSupportOnOffPowerCtl(dbg)) {
			
			bool on;
			
			if (voltage == VOLTAGE_FOR_CMD_ON) {
				
				fprintf(stderr, " power: setting power to \"on\"\n");
				on = true;
			}
			else if (voltage >= VOLTAGE_FOR_CMD_ON - VOLTAGE_SLOP && voltage <= VOLTAGE_FOR_CMD_ON + VOLTAGE_SLOP) {
			
				fprintf(stderr, " power: this debugger only has on/off 3.3V supply. %umV is close enough to 3.3V. Treating it as \"on\"\n", voltage);
				on = true;
			}
			else if (!voltage) {
				
				fprintf(stderr, " power: setting power to \"off\"\n");
				on = false;
			}
			else if (voltage <= VOLTAGE_SLOP) {
				fprintf(stderr, " power: this debugger only has on/off 3.3V supply. %umV is close enough to 0. Treating it as \"off\"\n", voltage);
				on = false;
			}
			else {
				
				fprintf(stderr, " power: this debugger only has on/off 3.3V supply. %umV cannot be supplied\n", voltage);
				return false;
			}
			
			if (!debuggerPowerCtlSimple(dbg, on)) {
				fprintf(stderr, " power: setting failed\n");
				return false;
			}
			return true;
		}
		else {
			
			fprintf(stderr, " power: this debugger has no software power control.\n");
			return false;
		}
	}
	
	return true;
}

static void powerOpFree(void *toolOpData)
{
	//nothing
}

static void powerOpHelp(const char *argv0)	//help for "power"
{
	fprintf(stderr,
		"USAGE: %s power \"on\"|\"off\"|millivolts\n"
		"\tIf the debugger is capable of supplying power to the\n"
		"\ttarget, this command controls that functionality. The\n"
		"\tparameter is the voltage to supply, in millivolts. \"off\"\n"
		"\tcan be a used as a shortcut for 0mV and \"on\" can be\n"
		"\tused as a shortcut for %dmV\n", argv0, VOLTAGE_FOR_CMD_ON);
}

DEFINE_OP(power, powerOpParse, powerOpDo, powerOpFree, powerOpHelp);