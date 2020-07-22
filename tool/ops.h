#ifndef _OPS_H_
#define _OPS_H_

#include "debugger.h"
#include "script.h"
#include "types.h"
#include "cpu.h"

#define TOOL_OP_WANTS_DEBUGGER			0x00000001	//this op wants debuggger but can live without it
#define TOOL_OP_NEEDS_DEBUGGER			0x00000002	//this op needs debuggger
#define TOOL_OP_WANTS_CPU				0x00000004	//this op wants cpu to be attached but can live without it
#define TOOL_OP_NEEDS_CPU				0x00000008	//this op needs cpu to be attached
#define TOOL_OP_WANTS_SCRIPT			0x00000010	//this op wants script but can live without it
#define TOOL_OP_NEEDS_SCRIPT			0x00000020	//this op needs script
#define TOOL_OP_NEEDS_SCPT_WRITE		0x00000040	//this op needs script to be able to write
#define TOOL_OP_NEEDS_SCPT_ERASEBLOCK	0x00000080	//this op needs script to be able to erase a block
#define TOOL_OP_NEEDS_SCPT_ERASEALL		0x00000100	//this op needs script to be able to erase whole chip
#define TOOL_OP_FLAG_SET_INFO_MODE		0x40000000	//if you set this, info mode flag will be set
#define TOOL_OP_FLAG_HELP_ONLY			0x80000000	//if you set this, only the help step will run

#define TOOL_OP_STEP_PRE_DEBUGGER		0			//idempotent please. run before debugger is opened
#define TOOL_OP_STEP_PRE_DEBUGGER_ID	1			//idempotent please. run before debugger is contacted
#define TOOL_OP_STEP_PRE_CPUID			2			//idempotent please. run before CPUID is done
#define TOOL_OP_STEP_PRE_SCRIPT			3			//idempotent please. run before script is loaded
#define TOOL_OP_STEP_POST_SCRIPT		4			//idempotent please. run after script is loaded (but before init is called)
#define TOOL_OP_STEP_POST_SCRIPT_INIT	5			//idempotent please. run after script is inited (do not do work here, just preflight it)
#define TOOL_OP_STEP_MAIN				6			//do actual work here

#define TOOL_OP_FLAG_INFO_MODE			0x00000001	//in "info" mode - be more verbose


struct ToolOpInfo {
	struct ToolOpInfo *next;
	const char *name;
	void* (*toolOpCmdParse)(const char *argv0, int *argcP, char ***argvP, uint32_t *needFlagsP);									//MANDATORY; return data to pass to do/free or NULL on failure. Show own message for error. Usage will be shown as well. Set"needScriptP" to true if device script is needed
	bool (*toolOpDo)(void *toolOpData, uint32_t step, uint32_t flags, struct Debugger *dbg, struct Cpu *cpu, struct Script *scpt);	//MANDATORY; do action
	void (*toolOpDataFree)(void *toolOpData);																						//MANDATORY; free the data
	void (*toolOpHelpFn)(const char *argv0);																						//MANDATORY; show help for this function
};



void opsSubscribeOp(struct ToolOpInfo *opNfo);
const struct ToolOpInfo *opsGetFirst(void);


#ifdef WIN32
	#define SUBSCRIBE_TOOL_OP									\
		 static int __sub(void){								\
		 	opsSubscribeOp(&__op);								\
		 	return 0;											\
		 }														\
   		 __pragma(data_seg(".CRT$XIU"))							\
   		 static int(*__psub) () = __sub;						\
   		  __pragma(data_seg())
#else
	#define SUBSCRIBE_TOOL_OP									\
		__attribute__((constructor)) static void __sub(void) {	\
			opsSubscribeOp(&__op);								\
		}
#endif

#define DEFINE_OP(_name, _parceFn, _doFn, _freeFn, _helpFn)		\
	static struct ToolOpInfo __op = {							\
		/*.next = */NULL,										\
		/*.name = */#_name,										\
		/*.toolOpCmdParse = */_parceFn,							\
		/*.toolOpDo = */_doFn,									\
		/*.toolOpDataFree = */_freeFn,							\
		/*.toolOpHelpFn = */_helpFn,							\
	};															\
	SUBSCRIBE_TOOL_OP



/*

struct ToolOpDataWrite {
	FILE* file;
	bool noack;
	bool preErase;
	uint32_t startAddr;
	uint32_t length;
};

struct ToolOpDataRead {
	FILE* file;
	uint32_t startAddr;
	uint32_t length;
};

struct ToolOpDataUpload {
	FILE* file;
	bool noack;
	uint32_t startAddr;
	uint32_t length;
};

struct ToolOpDataErase {
	uint32_t startAddr;
	uint32_t length;
};

struct ToolOpDataTrace {
	uint32_t addr;
};

struct ToolOpDataDebug {
	bool noreset;
	uint16_t port;
};

struct ToolOpDataPower {
	uint16_t milliVolts;
};

struct ToolOpDataScript {
	FILE *scpt;
};

*/

#endif

