#ifdef WIN32
#include <windows.h>
#include "shlwapi.h"
#pragma comment(lib, "Shlwapi.lib")
#endif
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "debugger.h"
#include "lua-5.3.4/src/lauxlib.h"
#include "lua-5.3.4/src/lualib.h"
#include "utilOp.h"
#include "memio.h"
#include "cpu.h"
#include "ops.h"


//predeclare
static void specialExportOurFuncs(lua_State *lua);

#define LUA_REGISTRY_ADDR_DBG	1
#define LUA_REGISTRY_ADDR_CPU	2
#define LUA_REGISTRY_ADDR_SCPT	3


#ifdef WIN32
	#include <windows.h>
	#define DIRSEP				'\\'
	#define PATHCMP				_stricmp
	#define PATHNCMP			_strnicmp 
	#define FILEEXISTS(path)	PathFileExistsA(path)
#else
	#include <unistd.h>
	#define DIRSEP				'/'
	#define PATHCMP				strcmp
	#define PATHNCMP			strncmp
	#define FILEEXISTS(path)	(!access(path, R_OK))
#endif

struct SpecialScriptFindInfo {
	const char *name;
	char *found;
};

static bool specialOpFindSpecialFileCallback(void *userData, const char *path, const char *justName)
{
	struct SpecialScriptFindInfo *fi = (struct SpecialScriptFindInfo*)userData;
	
	//maybe a direct match with name
	if (!PATHCMP(fi->name, justName))
		goto found;
	
	//else might be a match with extension (we are caseless about that)
	//since our search already verified extension, so let's check for length and name match
	if (strlen(justName) > strlen(fi->name) && strlen(justName) - strlen(fi->name) == 4 && !PATHNCMP(fi->name, justName, strlen(fi->name)))
		goto found;

	return true;

found:
	fi->found = strdup(path);
	return false;
}

static void* specialOpParse(const char *argv0, int *argcP, char ***argvP, uint32_t *needFlagsP)
{
	char *filename = NULL, *params = NULL;
	bool freeName = false;
	lua_State *lua;
	
	if (!*argcP) {
		fprintf(stderr, " special: not enough arguments given for \"special\" command\n");
		return NULL;
	}
	
	filename = *(*argvP)++;
	(*argcP)--;
	
	if (*argcP) {
		params = *(*argvP);
		
		if (params[0] == '{' && params[strlen(params) - 1] == '}') {
			
			(*argvP)++;
			(*argcP)--;
		}
		else
			params = NULL;
	}
	
	//find the scriptfile if param is not a path (contains a slash or a file with that name exists i nthis directory)
	if (!strchr(filename, DIRSEP) && !FILEEXISTS(filename)){
		
		struct SpecialScriptFindInfo fi = {0,};
		char *specialpaths[8] = {0,};
		int idx = 0;
		
		fi.name = filename;
		specialpaths[idx++] = "SPECIAL";
		if (getenv("CORTEXPROG_SPECIAL_DIR"))
			specialpaths[idx++] = getenv("CORTEXPROG_SPECIAL_DIR");
		specialpaths[idx++] = "/usr/share/cortexprog/special";
		specialpaths[idx++] = ".";
		
		//we check for $GIVEN_NAME and $GIVEN_NAME.lua hence the need to check in "."
		
		utilFindFilesInPath((const char* const*)specialpaths, ".lua", &specialOpFindSpecialFileCallback, &fi);
		
		if (fi.found) {
			
			fprintf(stderr, " special: assuming you meant '%s' by '%s'\n", fi.found, filename);
			
			filename = fi.found;
			freeName = true;
		}
	}
	
	//init lua
	lua = luaL_newstate();
	if (!lua) {
		fprintf(stderr, " special: unable to init lua\n");
		return NULL;
	}
	luaL_openlibs(lua);
	specialExportOurFuncs(lua);
	
	//verify file exists (lua will check again, this is just to show user a nice message)
	if (!FILEEXISTS(filename)) {
		fprintf(stderr, " special: unable to open '%s'\n", filename);
		goto out_lua_destroy;
	}
	
	//load scriptfile
	if (LUA_OK != luaL_dofile(lua, filename)) {
		fprintf(stderr, " special: unable to load '%s'\n", filename);
		goto out_lua_destroy;
	}
	
	//remove braces from params
	if (params) {
		params[strlen(params) - 1] = 0;
		params++;
	}
	
	//call init
	lua_getglobal(lua, "init");					//bool init(string params)
	if (params)
		lua_pushstring(lua, params);
	else
		lua_pushnil(lua);
	if (LUA_OK != lua_pcall(lua, 1, 1, 0)) {
		fprintf(stderr, " special: failed to call init: '%s'\n", lua_tostring(lua, -1));
		lua_pop(lua, 1);
		goto out_lua_destroy;
	}
	if (lua_type (lua, -1) == LUA_TNIL) {
		fprintf(stderr, " special: init failed\n");
		lua_pop(lua, 1);
		goto out_lua_destroy;
	}
	if (lua_type (lua, -1) != LUA_TNUMBER || lua_tonumber(lua, -1) != (uint32_t)lua_tonumber(lua, -1)) {
		fprintf(stderr, " special: init returned a bad value\n");
		lua_pop(lua, 1);
		goto out_lua_destroy;
	}
	*needFlagsP |= (uint32_t)lua_tonumber(lua, -1);
	lua_pop(lua, 1);
	
	return lua;
	
out_lua_destroy:
	if (freeName)
		free(filename);
	lua_close(lua);
	return NULL;
}

//lua's "registry" is not accessible to lua code so it cannot mess with our pointers that we store there
static void specialOpSetRegistry(lua_State *lua, uint32_t regIdx, void *data)
{
	lua_pushlightuserdata(lua, (void*)(uintptr_t)regIdx);
	lua_pushlightuserdata(lua, data);
	lua_settable(lua, LUA_REGISTRYINDEX);
}

static void* specialOpGetRegistry(lua_State *lua, uint32_t regIdx)
{
    void *ret;
    
    lua_pushlightuserdata(lua, (void*)(uintptr_t)regIdx);
    lua_gettable(lua, LUA_REGISTRYINDEX);
    ret = lua_touserdata(lua, -1);
    lua_pop(lua, 1);
    
    return ret;
}

static bool specialOpDo(void *toolOpData, uint32_t step, uint32_t flags, struct Debugger *dbg, struct Cpu *cpu, struct Script *scpt)
{
	lua_State *lua = (lua_State*)toolOpData;
	int status;
	bool ret;

	//no globals-> store our pointers in the lua registry
	specialOpSetRegistry(lua, LUA_REGISTRY_ADDR_DBG, dbg);
	specialOpSetRegistry(lua, LUA_REGISTRY_ADDR_CPU, cpu);
	specialOpSetRegistry(lua, LUA_REGISTRY_ADDR_SCPT, scpt);

	lua_getglobal(lua, "main");					//bool main(int step, bool haveDbg, bool haveCpu, bool haveScpt)
	lua_pushinteger(lua, step);
	lua_pushboolean(lua, !!dbg);
	lua_pushboolean(lua, !!cpu);
	lua_pushboolean(lua, !!scpt);
	status = lua_pcall(lua, 4, 1, 0);
	if (LUA_OK != status) {
		fprintf(stderr, " special: failed to call main: '%s'\n", lua_tostring(lua, -1));
		lua_pop(lua, 1);
		return false;
	}
	ret = lua_toboolean(lua, -1);
	lua_pop(lua, 1);

	return ret;
}

static void specialOpFree(void *toolOpData)
{
	lua_State *lua = (lua_State*)toolOpData;
	
	lua_close(lua);
}

static void specialOpHelp(const char *argv0)	//help for "script"
{
	fprintf(stderr,
		"USAGE: %s <scriptfile> [\"{scriptparam1 scriptparam2 ...}\"]\n"
		"\tExecute a user-defined script on the debugger/target.\n"
		"\tThis is most commonly used for unusual chip-specific\n"
		"\tthings like removing flash protection or unlocking\n"
		"\tfunctionality. <scriptfile> will be interpreted as\n"
		"\ta filename. If that produces no results, CortexProg\n"
		"\twill search through the following directories for\n"
		"\tfiles named <scriptfile> or <scriptfile>.lua to try.\n"
		"\tSee the CortexProg manual for specifics of how to\n"
		"\twrite a special script or see https://cortexprog.com\n"
		"\tto download useful scripts. Search directories in\n"
		"\t search order: 'SPECIAL', the directory stored in\n"
		"\tthe 'CORTEXPROG_SPECIAL_DIR' enviroment variable,\n"
		"\t'/usr/share/cortexprog/special', and lastly, the\n"
		"\tcurrent working directory.\n", argv0);
}

DEFINE_OP(special, specialOpParse, specialOpDo, specialOpFree, specialOpHelp);




////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////// LUA-exported funcs //////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////



#define SET_GLOBAL_NUM(_nm, _val)			do {lua_pushinteger(lua, _val); lua_setglobal(lua, _nm); }while(0)
#define SET_GLOBAL_FROM_DEFINE(_nm)			SET_GLOBAL_NUM(#_nm, _nm)
#define SET_GLOBAL_FUNC(_luaName, _cName)	do {lua_pushcfunction(lua, _cName); lua_setglobal(lua, _luaName); }while(0)



static struct Debugger* specialOpGetDbg(lua_State *lua)
{
	struct Debugger* dbg = (struct Debugger*)specialOpGetRegistry(lua, LUA_REGISTRY_ADDR_DBG);
	
	if (!dbg)
		luaL_error(lua, " special: debugger not available");
	
	return dbg;
}

static struct Cpu* specialOpGetCpu(lua_State *lua)
{
	struct Cpu* cpu = (struct Cpu*)specialOpGetRegistry(lua, LUA_REGISTRY_ADDR_CPU);
	
	if (!cpu)
		luaL_error(lua, " special: cpu not available");
	
	return cpu;
}

static struct Script* specialOpGetScpt(lua_State *lua)
{
	struct Script* scpt = (struct Script*)specialOpGetRegistry(lua, LUA_REGISTRY_ADDR_SCPT);
	
	if (!scpt)
		luaL_error(lua, " special: script not available");
	
	return scpt;
}

static uint32_t specialReadUint(lua_State *lua, int32_t luaStackIdx, uint32_t readMask, const char *nameForErr)
{
	double dV = luaL_checknumber(lua, luaStackIdx);
	int64_t iV = (int64_t)dV;
	
	if ((iV != dV) || (iV < 0) || (iV &~ (uint64_t)readMask))
		luaL_error(lua, " special: invalid '%s' param", nameForErr);		//this does not return so it is ok to not bother with any further processing
	
	return (uint32_t)iV;
}

static int specialExportDbgSwdRead(lua_State *lua)							//	dbgSwdRead(ap, a23) -> u32 OR nil
{
	uint32_t ap = specialReadUint(lua, 1, 0x01, "ap"), a23 = specialReadUint(lua, 2, 0x03, "a23"), ret;
	struct Debugger *dbg = specialOpGetDbg(lua);
	
	if (!debuggerRawSwdBusRead(dbg, ap, a23, &ret))
		lua_pushnil(lua);
	else
		lua_pushinteger(lua, ret);
	
	return 1;	//1 return val
}

static int specialExportDbgSwdWrite(lua_State *lua)							//	dbgSwdWrite(ap, a23, val) -> true OR nil
{
	uint32_t ap = specialReadUint(lua, 1, 0x01, "ap"), a23 = specialReadUint(lua, 2, 0x03, "a23"), val = specialReadUint(lua, 3, 0xFFFFFFFFUL, "val");
	struct Debugger *dbg = specialOpGetDbg(lua);
	
	if (!debuggerRawSwdBusWrite(dbg, ap, a23, val))
		lua_pushnil(lua);
	else
		lua_pushboolean(lua, true);
	
	return 1;	//1 return val
}

static int specialExportDbgClockSet(lua_State *lua)							//	dbgSetClock (speed) -> resultant_speed OR nil
{
	uint32_t speed = specialReadUint(lua, 1, 0xffffffff, "speed"), resultantSpeed = 0;
	struct Debugger *dbg = specialOpGetDbg(lua);
	
	if (debuggerCanSupportVariableClockSpeed(dbg) && debuggerClockCtlSet(dbg, speed, &resultantSpeed))
		lua_pushinteger(lua, resultantSpeed);
	else
		lua_pushnil(lua);
	
	return 1;	//1 return val
}

static int specialExportDbgResetPinSet(lua_State *lua)						//	dbgSetResetPin(bool high) -> true OR nil
{
	struct Debugger *dbg = specialOpGetDbg(lua);
	bool rstPinHigh = lua_toboolean(lua, 1);
	
	if (debuggerHasResetPin(dbg) && debuggerResetPinCtl(dbg, rstPinHigh))
		lua_pushboolean(lua, true);
	else
		lua_pushnil(lua);
	
	return 1;	//1 return val
}

static int specialExportDbgCodeUploadInit(lua_State *lua)					//	dbgCodeUploadInit() -> true OR nil
{
	struct Debugger *dbg = specialOpGetDbg(lua);
	
	if (debuggerCanSupportUploadableCode(dbg) && debuggerUploadableCodeInit(dbg))
		lua_pushboolean(lua, true);
	else
		lua_pushnil(lua);
	
	return 1;	//1 return val
}

static int specialExportDbgCodeUploadRun(lua_State *lua)					//	dbgCodeUploadRun(r0, r1, r2, r3) -> (r0, r2, r3, r4) OR nil
{
	struct Debugger *dbg = specialOpGetDbg(lua);
	uint32_t regs[4], i;
	
	for (i = 0; i < 4; i++) {
		char nm[3] = {'r', '0' + i, 0};
		 
		regs[i] = specialReadUint(lua, i + 1, 0xffffffff, nm);
	}
	
	if (!debuggerCanSupportUploadableCode(dbg) || !debuggerUploadableCodeRun(dbg, regs)) {
		lua_pushnil(lua);
		return 1;
	}
	
	for (i = 0; i < 4; i++)
		lua_pushinteger(lua, regs[i]);
	
	return 4;	//4 return vals
}

static int specialExportDbgCodeUploadAddOpcode(lua_State *lua)				//	dbgCodeUploadAddOpcode(opcode, param1, param2, param3) return_val or nil
{
	uint32_t opcode = specialReadUint(lua, 1, 0xff, "opcode"), params[3], retVal, i;
	struct Debugger *dbg = specialOpGetDbg(lua);
	
	for (i = 0; i < 3; i++) {
		char nm[3] = {'p', '0' + 1 + i, 0};
		 
		params[i] = specialReadUint(lua, i + 2, 0xffffffff, nm);
	}
	
	if (debuggerCanSupportUploadableCode(dbg) && debuggerUploadableCodeSendOpcode(dbg, opcode, params[0], params[1], params[2], &retVal))
		lua_pushinteger(lua, retVal);
	else
		lua_pushnil(lua);
	
	return 1;	//1 return val
}

static int specialExportCpuWordRead(lua_State *lua)							// cpuWordRead(u32 addr) -> u32 OR nil
{
	uint32_t val, addr = specialReadUint(lua, 1, 0xFFFFFFFFUL, "addr");
	struct Cpu *cpu = specialOpGetCpu(lua);
	
	if (!cpuMemReadEx(cpu, addr, 1, &val, true))
		lua_pushnil(lua);
	else
		lua_pushinteger(lua, val);
	
	return 1;	//1 return val
}

static int specialExportCpuWordWrite(lua_State *lua)						// cpuWordWrite(u32 addr, u32 val) -> true OR nil
{
	uint32_t addr = specialReadUint(lua, 1, 0xFFFFFFFFUL, "addr"), val = specialReadUint(lua, 2, 0xFFFFFFFFUL, "val");
	struct Cpu *cpu = specialOpGetCpu(lua);
	
	if (!cpuMemWrite(cpu, addr, 1, &val, true))
		lua_pushnil(lua);
	else
		lua_pushboolean(lua, true);
	
	return 1;	//1 return val
}

static bool specialExportIsValidRegNo(struct Cpu* cpu, uint32_t regNo)
{
	if (regNo >= SWD_REGS_NUM_Rx(0) && regNo <= SWD_REGS_NUM_Rx(15))
		return true;
	if (regNo == SWD_REGS_NUM_XPSR || regNo == SWD_REGS_NUM_MSP || regNo == SWD_REGS_NUM_PSP || regNo == SWD_REGS_NUM_CFBP)
		return true;
	if (cpuHasFpu(cpu)) {
		
		if (regNo == SWD_REGS_NUM_FPCSR)
			return true;
		if(regNo >= SWD_REGS_NUM_Sx(0) && regNo <= SWD_REGS_NUM_Sx(32))
			return true;
	}
	
	return false;
}

static int specialExportCpuRegGet(lua_State *lua)							// cpuRegGet(u32 regNo) -> u32 OR nil
{
	uint32_t regNo = specialReadUint(lua, 1, 0x3F, "regNo"), regSet, regOfst, regs[NUM_REGS];
	struct Cpu *cpu = specialOpGetCpu(lua);
	
	if (!specialExportIsValidRegNo(cpu, regNo))
		luaL_error(lua, " special: invalid regNo for this CPU");
	
	regSet = regNo / NUM_REGS;
	regOfst = regNo % NUM_REGS;
	
	if (!cpuRegsGet(cpu, regSet, regs))
		lua_pushnil(lua);
	else
		lua_pushinteger(lua, regs[regOfst]);
	
	return 1;	//1 return val
}

static int specialExportCpuRegSet(lua_State *lua)							// cpuRegSet(u32 regNo, u32 val) -> true OR nil
{
	uint32_t regNo = specialReadUint(lua, 1, 0x3F, "regNo"), val = specialReadUint(lua, 2, 0xFFFFFFFFUL, "regVal"), regSet, regOfst, regs[NUM_REGS];
	struct Cpu *cpu = specialOpGetCpu(lua);
	
	if (!specialExportIsValidRegNo(cpu, regNo))
		luaL_error(lua, " special: invalid regNo for this CPU");
	
	regSet = regNo / NUM_REGS;
	regOfst = regNo % NUM_REGS;
	
	if (!cpuRegsGet(cpu, regSet, regs))
		lua_pushnil(lua);
	else {
		regs[regOfst] = val;
		if (!cpuRegsSet(cpu, regSet, regs))
			lua_pushnil(lua);
		else
			lua_pushboolean(lua, true);
	}
	
	return 1;	//1 return val
}

static int specialExportCpuStop(lua_State *lua)								// cpuStop() -> CPU_STAT_CODE_* OR nil
{
	struct Cpu *cpu = specialOpGetCpu(lua);
	uint8_t ret;
	
	ret = cpuStop(cpu);
	if (ret == CPU_STAT_CODE_FAILED)
		lua_pushnil(lua);
	else
		lua_pushinteger(lua, ret);
	
	return 1;	//1 return val
}

static int specialExportCpuReset(lua_State *lua)							// cpuReset() -> true OR nil
{
	struct Cpu *cpu = specialOpGetCpu(lua);
	bool ret;
	
	ret = cpuReset(cpu);
	if (!ret)
		lua_pushnil(lua);
	else
		lua_pushboolean(lua, true);
	
	return 1;	//1 return val
}

static int specialExportCpuGo(lua_State *lua)								// cpuGo() -> true OR nil
{
	struct Cpu *cpu = specialOpGetCpu(lua);
	bool ret;
	
	ret = cpuGo(cpu);
	if (!ret)
		lua_pushnil(lua);
	else
		lua_pushboolean(lua, true);
	
	return 1;	//1 return val
}

static int specialExportCpuStep(lua_State *lua)								// cpuStep() -> CPU_STAT_CODE_* OR nil
{
	struct Cpu *cpu = specialOpGetCpu(lua);
	uint8_t ret;
	
	ret = cpuStep(cpu);
	if (ret == CPU_STAT_CODE_FAILED)
		lua_pushnil(lua);
	else
		lua_pushinteger(lua, ret);
	
	return 1;	//1 return val
}

static int specialExportCpuIsStoppedAndWhy(lua_State *lua)					// cpuIsStoppedAndWhy() -> CPU_STAT_CODE_* OR nil
{
	struct Cpu *cpu = specialOpGetCpu(lua);
	uint8_t ret;
	
	ret = cpuIsStoppedAndWhy(cpu);
	if (ret == CPU_STAT_CODE_FAILED)
		lua_pushnil(lua);
	else
		lua_pushinteger(lua, ret);
	
	return 1;	//1 return val
}

static int specialExportCpuHasFpu(lua_State *lua)							// cpuHasFpu() -> bool
{
	struct Cpu *cpu = specialOpGetCpu(lua);

	lua_pushboolean(lua, cpuHasFpu(cpu));
	return 1;	//1 return val
}

static int specialExportCpuIsV7(lua_State *lua)								// cpuIsV7() -> bool
{
	struct Cpu *cpu = specialOpGetCpu(lua);
	
	lua_pushboolean(lua, cpuIsV7(cpu));
	return 1;	//1 return val
}

static int specialExportScriptGetFlashWriteStageAreaAddr(lua_State *lua)	// scriptGetFlashWriteStageAreaAddr() -> u32
{
	struct Script *scpt = specialOpGetScpt(lua);
	
	lua_pushinteger(lua, scriptGetFlashWriteStageAreaAddr(scpt));
	return 1;	//1 return val
}

static int specialExportScriptGetSupportedOps(lua_State *lua)				// scriptGetSupportedOps() -> u32 *maskof SCRIPT_OP_FLAG_HAVE_*)
{
	struct Script *scpt = specialOpGetScpt(lua);
	
	lua_pushinteger(lua, scriptGetSupportedOps(scpt));
	return 1;	//1 return val
}

static int specialExportScriptGetFlashBlockSize(lua_State *lua)				// scriptGetFlashBlockSize(u32 base, bool forWrite) -> u32 or nil
{
	uint32_t base = specialReadUint(lua, 1, 0xFFFFFFFFUL, "baseAddr");
	struct Script *scpt = specialOpGetScpt(lua);
	bool forWrite = lua_toboolean(lua, 2);
	uint32_t ret;
	
	ret = scriptGetFlashBlockSize(scpt, base, forWrite);
	if (ret)
		lua_pushinteger(lua, ret);
	else
		lua_pushnil(lua);

	return 1;	//1 return val
}

static int specialExportScriptEraseAll(lua_State *lua)					// scriptEraseAll() -> true or nil
{
	struct Script *scpt = specialOpGetScpt(lua);
	
	if (!scriptEraseAll(scpt))
		lua_pushnil(lua);
	else
		lua_pushboolean(lua, true);

	return 1;	//1 return val
}

static int specialExportScriptEraseBlock(lua_State *lua)				// scriptEraseBlock(u32 base) -> true or nil
{
	uint32_t base = specialReadUint(lua, 1, 0xFFFFFFFFUL, "baseAddr");
	struct Script *scpt = specialOpGetScpt(lua);
	
	if (!scriptEraseBlock(scpt, base))
		lua_pushnil(lua);
	else
		lua_pushboolean(lua, true);

	return 1;	//1 return val
}

static int specialExportScriptWriteBlock(lua_State *lua)				// scriptWriteBlock(u32 base) -> true or nil
{
	uint32_t base = specialReadUint(lua, 1, 0xFFFFFFFFUL, "baseAddr");
	struct Script *scpt = specialOpGetScpt(lua);
	
	if (!scriptWriteBlock(scpt, base))
		lua_pushnil(lua);
	else
		lua_pushboolean(lua, true);

	return 1;	//1 return val
}

static void specialExportOurFuncs(lua_State *lua)
{
	SET_GLOBAL_FROM_DEFINE(TOOL_OP_WANTS_DEBUGGER);
	SET_GLOBAL_FROM_DEFINE(TOOL_OP_NEEDS_DEBUGGER);
	SET_GLOBAL_FROM_DEFINE(TOOL_OP_WANTS_CPU);
	SET_GLOBAL_FROM_DEFINE(TOOL_OP_NEEDS_CPU);
	SET_GLOBAL_FROM_DEFINE(TOOL_OP_WANTS_SCRIPT);
	SET_GLOBAL_FROM_DEFINE(TOOL_OP_NEEDS_SCRIPT);
	SET_GLOBAL_FROM_DEFINE(TOOL_OP_NEEDS_SCPT_WRITE);
	SET_GLOBAL_FROM_DEFINE(TOOL_OP_NEEDS_SCPT_ERASEBLOCK);
	SET_GLOBAL_FROM_DEFINE(TOOL_OP_NEEDS_SCPT_ERASEALL);
	
	SET_GLOBAL_FROM_DEFINE(SCRIPT_OP_FLAG_HAVE_ERASE_ALL);
	SET_GLOBAL_FROM_DEFINE(SCRIPT_OP_FLAG_HAVE_ERASE_BLOCK);
	SET_GLOBAL_FROM_DEFINE(SCRIPT_OP_FLAG_HAVE_WRITE_BLOCK);
	
	SET_GLOBAL_FROM_DEFINE(TOOL_OP_STEP_PRE_DEBUGGER);
	SET_GLOBAL_FROM_DEFINE(TOOL_OP_STEP_PRE_DEBUGGER_ID);
	SET_GLOBAL_FROM_DEFINE(TOOL_OP_STEP_PRE_CPUID);
	SET_GLOBAL_FROM_DEFINE(TOOL_OP_STEP_PRE_SCRIPT);
	SET_GLOBAL_FROM_DEFINE(TOOL_OP_STEP_POST_SCRIPT);
	SET_GLOBAL_FROM_DEFINE(TOOL_OP_STEP_POST_SCRIPT_INIT);
	SET_GLOBAL_FROM_DEFINE(TOOL_OP_STEP_MAIN);
	
	SET_GLOBAL_FROM_DEFINE(CPU_STAT_CODE_HALT_OR_STEP);
	SET_GLOBAL_FROM_DEFINE(CPU_STAT_CODE_BKPT);
	SET_GLOBAL_FROM_DEFINE(CPU_STAT_CODE_DWPT);
	SET_GLOBAL_FROM_DEFINE(CPU_STAT_CODE_VCATCH);
	SET_GLOBAL_FROM_DEFINE(CPU_STAT_CODE_EXTERNAL);
	
	SET_GLOBAL_FUNC("dbgSetClock", specialExportDbgClockSet);
	SET_GLOBAL_FUNC("dbgSetResetPin", specialExportDbgResetPinSet);
	SET_GLOBAL_FUNC("dbgCodeUploadInit", specialExportDbgCodeUploadInit);
	SET_GLOBAL_FUNC("dbgCodeUploadRun", specialExportDbgCodeUploadRun);
	SET_GLOBAL_FUNC("dbgCodeUploadAddOpcode", specialExportDbgCodeUploadAddOpcode);
	SET_GLOBAL_FUNC("dbgSwdRead", specialExportDbgSwdRead);
	SET_GLOBAL_FUNC("dbgSwdWrite", specialExportDbgSwdWrite);
	
	SET_GLOBAL_FUNC("cpuWordRead", specialExportCpuWordRead);
	SET_GLOBAL_FUNC("cpuWordWrite", specialExportCpuWordWrite);
	SET_GLOBAL_FUNC("cpuRegGet", specialExportCpuRegGet);
	SET_GLOBAL_FUNC("cpuRegSet", specialExportCpuRegSet);
	SET_GLOBAL_FUNC("cpuStop", specialExportCpuStop);
	SET_GLOBAL_FUNC("cpuReset", specialExportCpuReset);
	SET_GLOBAL_FUNC("cpuGo", specialExportCpuGo);
	SET_GLOBAL_FUNC("cpuStep", specialExportCpuStep);
	SET_GLOBAL_FUNC("cpuIsStoppedAndWhy", specialExportCpuIsStoppedAndWhy);
	SET_GLOBAL_FUNC("cpuHasFpu", specialExportCpuHasFpu);
	SET_GLOBAL_FUNC("cpuIsV7", specialExportCpuIsV7);
	
	SET_GLOBAL_FUNC("scriptGetFlashWriteStageAreaAddr", specialExportScriptGetFlashWriteStageAreaAddr);
	SET_GLOBAL_FUNC("scriptGetSupportedOps", specialExportScriptGetSupportedOps);
	SET_GLOBAL_FUNC("scriptGetFlashBlockSize", specialExportScriptGetFlashBlockSize);
	SET_GLOBAL_FUNC("scriptEraseAll", specialExportScriptEraseAll);
	SET_GLOBAL_FUNC("scriptEraseBlock", specialExportScriptEraseBlock);
	SET_GLOBAL_FUNC("scriptWriteBlock", specialExportScriptWriteBlock);
	
	
	//CODEGEN DEFINES

	//native funcs
	SET_GLOBAL_FROM_DEFINE(SWD_UPLOAD_NATIVE_FUNC_RESET_CTL);			// (u32 high) -> (bool success)
	SET_GLOBAL_FROM_DEFINE(SWD_UPLOAD_NATIVE_FUNC_SWD_WRITE);			// (u1 ap, u2 a23, u32 val) -> (bool success)
	SET_GLOBAL_FROM_DEFINE(SWD_UPLOAD_NATIVE_FUNC_SWD_READ);			// (u1 ap, u2 a23) -> (bool success, u32 val)
	SET_GLOBAL_FROM_DEFINE(SWD_UPLOAD_NATIVE_FUNC_SWD_WRITE_BITS);		// (u32 bits, u32 nbits) -> () //only 8..16 bits allowed
	SET_GLOBAL_FROM_DEFINE(SWD_UPLOAD_NATIVE_FUNC_SUPPLY_GET_V);		// () -> (i32 millivolts, or negative one if func is unable to run)
	SET_GLOBAL_FROM_DEFINE(SWD_UPLOAD_NATIVE_FUNC_SUPPLY_SET_V);		// (u32 millivolts) -> (bool set)

	//OPCODES: function calls
	SET_GLOBAL_FROM_DEFINE(SWD_UPLOAD_OPCODE_CALL_NATIVE);				//what func to call is in imm8, options are SWD_UPLOAD_NATIVE_FUNC_*
	SET_GLOBAL_FROM_DEFINE(SWD_UPLOAD_OPCODE_CALL_GENERATED);			//call a function we've generated (a CodegenLabel) in imm32
	SET_GLOBAL_FROM_DEFINE(SWD_UPLOAD_OPCODE_RETURN);					//return from a generated func
	SET_GLOBAL_FROM_DEFINE(SWD_UPLOAD_OPCODE_EXIT);						//exit from uploaded script with return code imm8  (any nonzero ret code is an error)

	//OCODES: data processing
	SET_GLOBAL_FROM_DEFINE(SWD_UPLOAD_OPCODE_MOV);						// dstReg = srcReg
	SET_GLOBAL_FROM_DEFINE(SWD_UPLOAD_OPCODE_NOT);						// dstReg = ~srcReg
	SET_GLOBAL_FROM_DEFINE(SWD_UPLOAD_OPCODE_ADD_REG);					// dstReg += srcReg
	SET_GLOBAL_FROM_DEFINE(SWD_UPLOAD_OPCODE_SUB_REG);					// dstReg -= srcReg
	SET_GLOBAL_FROM_DEFINE(SWD_UPLOAD_OPCODE_ADD_IMM);					// dstReg += imm8
	SET_GLOBAL_FROM_DEFINE(SWD_UPLOAD_OPCODE_SUB_IMM);					// dstReg -= imm8
	SET_GLOBAL_FROM_DEFINE(SWD_UPLOAD_OPCODE_AND);						// dstReg &= srcReg
	SET_GLOBAL_FROM_DEFINE(SWD_UPLOAD_OPCODE_ORR);						// dstReg |= srcReg
	SET_GLOBAL_FROM_DEFINE(SWD_UPLOAD_OPCODE_XOR);						// dstReg ^= srcReg
	SET_GLOBAL_FROM_DEFINE(SWD_UPLOAD_OPCODE_LSL_REG);					// dstReg <<= srcReg
	SET_GLOBAL_FROM_DEFINE(SWD_UPLOAD_OPCODE_LSR_REG);					// dstReg >>= srcReg
	SET_GLOBAL_FROM_DEFINE(SWD_UPLOAD_OPCODE_LSL_IMM);					// dstReg <<= imm5
	SET_GLOBAL_FROM_DEFINE(SWD_UPLOAD_OPCODE_LSR_IMM);					// dstReg >>= imm5
	SET_GLOBAL_FROM_DEFINE(SWD_UPLOAD_OPCODE_LDR_IMM);					// dstReg = imm32

	//STACK ops
	SET_GLOBAL_FROM_DEFINE(SWD_UPLOAD_OPCODE_PUSH);						// push dstReg
	SET_GLOBAL_FROM_DEFINE(SWD_UPLOAD_OPCODE_POP);						// pop  dstReg

	//LABEL: normal
	SET_GLOBAL_FROM_DEFINE(SWD_UPLOAD_OPCODE_LABEL_GET_CUR);			// cur label handle returned in imm32
	SET_GLOBAL_FROM_DEFINE(SWD_UPLOAD_OPCODE_LABEL_FREE);				// free the label handle (in imm32) returned by SWD_UPLOAD_OPCODE_LABEL_GET_CUR

	//LABEL: predeclared
	SET_GLOBAL_FROM_DEFINE(SWD_UPLOAD_OPCODE_PREDECL_LBL_ALLOC);		// declare a predeclared label (you can jump to it now and set its target later). returned in imm32
	SET_GLOBAL_FROM_DEFINE(SWD_UPLOAD_OPCODE_PREDECL_LBL_TO_LBL);		// convert a predeclared label to a normal label (to be used as a branch or call target) imm32 -> imm32
	SET_GLOBAL_FROM_DEFINE(SWD_UPLOAD_OPCODE_PREDECL_LBL_FILL);			// label is passed in imm32, target to point it to (a label) is passed in imm32_2
	SET_GLOBAL_FROM_DEFINE(SWD_UPLOAD_OPCODE_PREDECL_LBL_FREE);			// free the predeclared label handle (in imm32) returned by SWD_UPLOAD_OPCODE_PREDECL_LBL_ALLOC

	//BRANCHES
	SET_GLOBAL_FROM_DEFINE(SWD_UPLOAD_OPCODE_BRANCH_UNCONDITIONAL);		// goto label imm32
	SET_GLOBAL_FROM_DEFINE(SWD_UPLOAD_OPCODE_BRANCH_UNSIGNED_GE);		// if (dstReg >= srcReg) goto label imm32
	SET_GLOBAL_FROM_DEFINE(SWD_UPLOAD_OPCODE_BRANCH_UNSIGNED_GT);		// if (dstReg >  srcReg) goto label imm32
	SET_GLOBAL_FROM_DEFINE(SWD_UPLOAD_OPCODE_BRANCH_UNSIGNED_LE);		// if (dstReg <= srcReg) goto label imm32
	SET_GLOBAL_FROM_DEFINE(SWD_UPLOAD_OPCODE_BRANCH_UNSIGNED_LT);		// if (dstReg <  srcReg) goto label imm32
	SET_GLOBAL_FROM_DEFINE(SWD_UPLOAD_OPCODE_BRANCH_EQ);				// if (dstReg == srcReg) goto label imm32
	SET_GLOBAL_FROM_DEFINE(SWD_UPLOAD_OPCODE_BRANCH_NE);				// if (dstReg != srcReg) goto label imm32
	SET_GLOBAL_FROM_DEFINE(SWD_UPLOAD_OPCODE_BRANCH_NEG);				// if (dstReg < 0) goto label imm32
	SET_GLOBAL_FROM_DEFINE(SWD_UPLOAD_OPCODE_BRANCH_NOT_NEG);			// if (dstReg >= 0) goto label imm32
	SET_GLOBAL_FROM_DEFINE(SWD_UPLOAD_OPCODE_BRANCH_ZERO);				// if (dstReg == 0) goto label imm32
	SET_GLOBAL_FROM_DEFINE(SWD_UPLOAD_OPCODE_BRANCH_NOT_ZERO);			// if (dstReg != 0) goto label imm32
	SET_GLOBAL_FROM_DEFINE(SWD_UPLOAD_OPCODE_BRANCH_EQ_IMM);			// if (dstReg == imm8) goto label imm32
	SET_GLOBAL_FROM_DEFINE(SWD_UPLOAD_OPCODE_BRANCH_NOT_EQ_IMM);		// if (dstReg != imm8) goto label imm32
}

