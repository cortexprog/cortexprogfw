#ifdef WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#define SOCKTYP SOCKET
#define SOCKINVAL  INVALID_SOCKET
#define NFDS(maxfd, numfds)	(numfds)
#pragma comment(lib, "Ws2_32.lib")
#define swap32(x)				(((x >> 24) & 0xff) | ((x >> 8) & 0xff00) | ((x & 0xff00) << 8) | ((x & 0xf) << 24))
#define clz		clzslow
#define STDIN_FILENO	0
#else
#define SOCKTYP	int
#define SOCKINVAL (-1)
#define closesocket close
#define swap32	__builtin_bswap32
#define clz		__builtin_clz
#define NFDS(maxfd, numfds)	(maxfd + 1)
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <unistd.h>
#endif
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include "alloca.h"
#include "utilOp.h"
#include "memio.h"
#include "types.h"
#include "cpu.h"
#include "ops.h"


static bool debugGdbServer(struct Cpu* cpu, int port);

struct ToolOpDataDebug {
	int port;
	bool noreset;
};

static inline uint32_t clzslow(uint32_t val)
{
	uint32_t mask = 0x80000000, ret = 0;

	while(!(val & mask) && mask) {
		mask >>= 1;
		ret++;
	}

	return ret;
}

static void* debugOpParse(const char *argv0, int *argcP, char ***argvP, uint32_t *needFlagsP)
{
	struct ToolOpDataDebug *data = NULL;
	long long vPort;
	bool noreset = false;
	
	if (*argcP && !strcmp("noreset", *(*argvP))) {
		
		noreset = true;
		(*argvP)++;
		(*argcP)--;
	}
	
	if (!*argcP || 1 != sscanf((*argvP)[0], "%lli", &vPort) || !vPort || (vPort >> 16)) {
		
		fprintf(stderr, " debug: invalid or no port number given\n");
		return false;
	}

	(*argvP)++;
	(*argcP)--;
	
	*needFlagsP |= TOOL_OP_NEEDS_DEBUGGER | TOOL_OP_NEEDS_CPU;
	
	data = calloc(1, sizeof(struct ToolOpDataDebug));
	if (!data)
		return NULL;
	
	data->port = (int)vPort;
	data->noreset = noreset;
	
	return data;
}

static bool debugOpDo(void *toolOpData, uint32_t step, uint32_t flags, struct Debugger *dbg, struct Cpu *cpu, struct Script *scpt)
{
	struct ToolOpDataDebug *data = (struct ToolOpDataDebug*)toolOpData;
	
	if (step == TOOL_OP_STEP_MAIN) {
		
		uint8_t stat;
		
		if (data->noreset) {
			cpuStop(cpu);
			stat = cpuIsStoppedAndWhy(cpu);
		}
		else
			stat = cpuResetStop(cpu);
		
		if (CPU_STAT_CODE_FAILED == stat) {
			
			fprintf(stderr, " debug: failed to stop cpu\n");
			return false;
		}
		
		return debugGdbServer(cpu, data->port);
	}
	
	return true;
}

static void debugOpFree(void *toolOpData)
{
	struct ToolOpDataDebug *data = (struct ToolOpDataDebug*)toolOpData;
	
	free(data);
}

static void debugOpHelp(const char *argv0)	//help for "debug"
{
	fprintf(stderr,
		"USAGE: %s debug [\"noreset\"] port\n"
		"\tStart a GDB server to allow debugging. Target will be\n"
		"\treset and halted unless the \"noreset\" option is gone,\n"
		"\tin which case the target will stay in whatever state it\n"
		"\tcurrently is. The port number can then be passed to\n"
		"\t\"target rem \" GDB command to connect.\n", argv0);
}

DEFINE_OP(debug, debugOpParse, debugOpDo, debugOpFree, debugOpHelp);





////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////// GDB stub ///////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////

#define DBG(...)	fprintf(stderr, " debug: *X* " __VA_ARGS__)


#define GDB_REG_NUM_Rx(x)	(x)			//4 bytes each
#define GDB_REG_NUM_Fx(x)	(16 + (x))	//8 bytes each
#define GDB_REG_NUM_FPS		24			//fp status - 4 bytes
#define GDB_REG_NUM_CPSR	25			//4 bytes
#define GDB_REG_NUM			26

#define CPSR_BIT_T			0x01000000UL

#define GDB_BUF_SZ			4096		//all our packets should fit into this
#define GDB_MAX_READ_WRITE	1024		//we truncate all read/write to this length


//DWT is much fancier on V7M than on v6M, but we do not use that
#define ADDR_DWT_CTRL	0xE0001000UL
#define ADDR_DWT_COMP0	0xE0001020UL
#define ADDR_DWT_MASK0	0xE0001024UL
#define ADDR_DWT_FUNC0	0xE0001028UL
#define OFST_DWT_NEXT   0x00000010UL	//from reg_x to reg_x+1

//v6M (C-M0/M0+/M1) has a breakpoint unit
//v7M (C-M3/C-M4) has an FBP which is more advanced and has options like literal comparators & instr remap
// (M7 is similar but has extra registers). All are backwards compatible to v6-M
#define ADDR_FBP_CTRL	0xE0002000UL
#define ADDR_FBP_REMAP	0xE0002004UL
#define ADDR_FBP_COMP0	0xE0002008UL
#define OFST_FBP_NEXT   0x00000004UL	//from reg_x to reg_x+1


#define WPT_TYPE_PC_ADDR		4
#define WPT_TYPE_DATA_READ		5
#define WPT_TYPE_DATA_WRITE		6
#define WPT_TYPE_DATA_RW		7


struct DebugTargetLimits {
	
	uint8_t numWpts;
	uint8_t wptMaxLogSz;
	bool haveReadDataWpts;
	bool haveWriteDataWpts;
	bool haveRwDataWpts;
	
	uint8_t numBpts;
	bool haveBptRemapSupport;
	bool haveFbpV2;
};

struct DebugBptWptTrackingStruct {
	uint32_t addr;
	uint32_t len;
	bool used;
	char type;
};














static uint32_t debugHtoiEx(const char** cP, int maxChars)
{
	uint32_t i = 0;
	const char* in = *cP;
	char c;
	
	while(maxChars-- && (c = *in)) {
		
		if (c >= '0' && c <= '9')
			i = (i * 16) + (c - '0');
		else if (c >= 'a' && c <= 'f')
			i = (i * 16) + (c + 10 - 'a');
		else if (c >= 'A' && c <= 'F')
			i = (i * 16) + (c + 10 - 'A');
		else
			break;
		in++;
	}
	
	*cP = in;
	
	return i;
}

static uint32_t debugHtoi(const char** cP)
{
	return debugHtoiEx(cP, 8);
}

static SOCKTYP debugAcceptGdbConn(int port)
{
	struct sockaddr_in sa = {AF_INET, htons(port)};
	socklen_t sl = sizeof(sa);
	SOCKTYP sock, accepted;
	int ret;
	
	sa.sin_addr.s_addr = inet_addr("127.0.0.1");
	
	sock = socket(PF_INET, SOCK_STREAM, 0);
	if (sock == SOCKINVAL) {
		fprintf(stderr, " debug: GDB socket creation fails: %d\n", errno);
		return SOCKINVAL;
	}
	
	ret = bind(sock, (struct sockaddr*)&sa, sizeof(sa));
	if (ret) {
		fprintf(stderr, " debug: GDB socket bind fails: %d\n", errno);
		closesocket(sock);
		return SOCKINVAL;
	}
	
	ret = listen(sock, 1);
	if (ret) {
		fprintf(stderr, " debug: GDB socket listen fails: %d\n", errno);
		closesocket(sock);
		return SOCKINVAL;
	}
	
	accepted = accept(sock, (struct sockaddr*)&sa, &sl);
	if (accepted == SOCKINVAL) {
		fprintf(stderr, " debug: GDB socket accept fails: %d\n", errno);
		closesocket(sock);
		return SOCKINVAL;
	}
	closesocket(sock);
	
	return accepted;
}

static bool debugCheckForWptTypeSupport(struct Cpu *cpu, uint32_t type, bool *resultP)
{
	uint32_t t = type;
	
	if (!cpuMemWrite(cpu, ADDR_DWT_FUNC0, 1, &t, true))
		return false;
	if (!cpuMemRead(cpu, ADDR_DWT_FUNC0, 1, &t))
		return false;
	if ((t & 0x0f) == type)
		*resultP = true;
	return true;
}

static bool debugGatherTargetLimitations(struct Cpu *cpu, struct DebugTargetLimits *limits)
{
	uint32_t t, tt, zero = 0;
	
	//read number of watchpoints
	if (!cpuMemRead(cpu, ADDR_DWT_CTRL, 1, &t))
		return false;
	limits->numWpts = t >> 28;
	
	//check on what watchpoints can do
	if (limits->numWpts) {
		
		if (!debugCheckForWptTypeSupport(cpu, WPT_TYPE_DATA_READ, &limits->haveReadDataWpts))
			return false;
		
		if (!debugCheckForWptTypeSupport(cpu, WPT_TYPE_DATA_WRITE,  &limits->haveWriteDataWpts))
			return false;
		
		if (!debugCheckForWptTypeSupport(cpu, WPT_TYPE_DATA_RW,  &limits->haveRwDataWpts))
			return false;
		
		t = 0x1F;
		if (!cpuMemWrite(cpu, ADDR_DWT_MASK0, 1, &t, true)) {
			fprintf(stderr, "Failed to write WPT mask\n");
			return false;
		}
		if (!cpuMemRead(cpu, ADDR_DWT_MASK0, 1, &t))
			return false;
		limits->wptMaxLogSz = t & 0x1F;
		
		fprintf(stderr, " debug: found support for %u watchpoints:%s%s%s%s. Max size: %ub\n", limits->numWpts, 
				limits->haveReadDataWpts ? " R" : "",
				limits->haveWriteDataWpts ? " W" : "",
				limits->haveRwDataWpts ? " RW" : "",
				(limits->haveReadDataWpts || limits->haveWriteDataWpts || limits->haveRwDataWpts) ? "" : " <UNKNOWN TYPES>",
				1 << limits->wptMaxLogSz);
		
		for (t = 0; t < limits->numWpts; t++) {
			
			if (!cpuMemWrite(cpu, ADDR_DWT_FUNC0 + t * OFST_DWT_NEXT, 1, &zero, true)) {
				fprintf(stderr, " debug: failed to disable watchpoint %u\n", t);
				return false;
			}
		}
	}

	//read number of breakpoints
	if (!cpuMemRead(cpu, ADDR_FBP_CTRL, 1, &t))
		return false;
	
	limits->numBpts = (t >> 4) & 0x0f;
	if (cpuIsV7(cpu))	//v7 has more
		limits->numBpts += (t >> 8) & 0x70;
	
	if (limits->numBpts) {
		
		limits->haveBptRemapSupport = false;
		
		if (cpuIsV7(cpu)) {
			
			if (!cpuMemRead(cpu, ADDR_FBP_REMAP, 1, &tt))
				return false;
			
			limits->haveBptRemapSupport = !!((tt >> 29) & 1);
		}
		
		switch(t >> 28) {
			case 0:	//FBP v1
				limits->haveFbpV2 = false;
				break;
			case 1:	//FBP v2
				limits->haveFbpV2 = true;
				break;
			default:
				fprintf(stderr, " debug: FBP version unknown: %u\n", (t >> 28));
				return false;
		}
		
		t |= 3;
		if (!cpuMemWrite(cpu, ADDR_FBP_CTRL, 1, &t, true)) {
			fprintf(stderr, " debug: failed to enable breakpoints\n");
			return false;
		}
		
		fprintf(stderr, " debug: found FBPv%u with support for %u breakpoints%s\n", limits->haveFbpV2 ? 2 : 1, limits->numBpts, limits->haveBptRemapSupport ? " and REMAP support" : "");
		
		for (t = 0; t < limits->numBpts; t++) {
			
			if (!cpuMemWrite(cpu, ADDR_FBP_COMP0 + t * OFST_FBP_NEXT, 1, &zero, true)) {
				fprintf(stderr, " debug: failed to disable breakpoint %u\n", t);
				return false;
			}
		}
	}
	
	return true;
}

static bool debugSendRaw(SOCKTYP sock, const char *data, uint32_t len)
{
	unsigned ofst = 0;
	
	while(len > ofst) {
		int ret = send(sock, data + ofst, len - ofst, 0);
		
		if (ret == -1) {
			if (errno == EINTR)
				continue;
			return false;
		}
		if (ret == 0)			//0 = =closed. RX will catch it. let it go
			return true;
		
		ofst += ret;
	}

	return true;	
}

static bool debugSendPacket(SOCKTYP sock, const char *packet, bool withAck)
{
	static const char hexch[] = "0123456789abcdef";
	int len = (int)strlen(packet), finalLen = 0;
	uint8_t checksum = 0;
	char *toSend;
	int i;

	toSend = alloca(len * 2 /* max expansion for escaping */ + 6 /* ack, start marker, stop marker, checksumX2, NULL */);

	//ACK if needed
	if (withAck)
		toSend[finalLen++] = '+';
	
	//start of packet marker
	toSend[finalLen++] = '$';
	
	//copy packet in (escape as needed, keep track of checksum)
	for (i = 0; i < len; i++) {
		char c = packet[i];
		
		if (c == 0x23 || c == 0x24 || c == 0x7D) {
			checksum += 0x7D;
			toSend[finalLen++] = 0x7D;
			c ^= 0x20;
		}
		
		checksum += c;
		toSend[finalLen++] = c;
	}
	
	//end of packet marker
	toSend[finalLen++] = '#';
	
	//checksum
	toSend[finalLen++] = hexch[checksum >> 4];
	toSend[finalLen++] = hexch[checksum & 15];
	
	//zero (to make printing it easier)
	toSend[finalLen] = 0;
	
	//send it
	DBG("TX: <%s>'%s'\n", withAck ? "ACK" : "", packet);
	
	return debugSendRaw(sock, toSend, finalLen);
}

static bool debugSendAck(SOCKTYP sock)
{
	return debugSendRaw(sock, "+", 1);
}

static bool debugSendNak(SOCKTYP sock)
{
	return debugSendRaw(sock, "-", 1);
}

static bool debugRegsCacheFill(struct Cpu* cpu, bool *regsValidP, bool* regsDirtyP, uint32_t *regsP)
{
	if (*regsValidP)
		return true;
	
	if (!cpuRegsGet(cpu, SWD_COMMS_REG_SET_BASE, regsP + SWD_REGS_NUM_Rx(0)))
		return false;
	
	if (!cpuRegsGet(cpu, SWD_COMMS_REG_SET_CTRL, regsP + SWD_REGS_NUM_XPSR))
		return false;
		
	if (cpuHasFpu(cpu)) {
		if (!cpuRegsGet(cpu, SWD_COMMS_REG_SET_FP0, regsP + SWD_REGS_NUM_Sx(0)))
			return false;
		
		if (!cpuRegsGet(cpu, SWD_COMMS_REG_SET_FP1, regsP + SWD_REGS_NUM_Sx(1)))
			return false;
	}
	
	*regsValidP = true;
	*regsDirtyP = false;
	return true;
}

static bool debugRegsCacheFlush(struct Cpu* cpu, bool *regsValidP, bool* regsDirtyP, uint32_t *regsP)		//todo: print own error to stderr
{
	if (!*regsValidP || !*regsDirtyP)
		return true;
		
	if (!cpuRegsSet(cpu, SWD_COMMS_REG_SET_BASE, regsP + SWD_REGS_NUM_Rx(0)))
		return false;
	
	if (!cpuRegsSet(cpu, SWD_COMMS_REG_SET_CTRL, regsP + SWD_REGS_NUM_XPSR))
		return false;
		
	if (cpuHasFpu(cpu)) {
		if (!cpuRegsSet(cpu, SWD_COMMS_REG_SET_FP0, regsP + SWD_REGS_NUM_Sx(0)))
			return false;
		
		if (!cpuRegsSet(cpu, SWD_COMMS_REG_SET_FP1, regsP + SWD_REGS_NUM_Sx(1)))
			return false;
	}
	
	*regsDirtyP = false;
	return true;
}

static void debugWriteRegToStr(char *dst, uint32_t val)
{
	//32-bit vals need to be set little endian
	sprintf(dst + strlen(dst), "%08x", swap32(val));
}

static bool debugAddRegToStr(struct Cpu* cpu, char *packet, const uint32_t *regs, uint32_t idx)
{
	if (idx >= GDB_REG_NUM_Rx(0) && idx <= GDB_REG_NUM_Rx(14))
		debugWriteRegToStr(packet, regs[SWD_REGS_NUM_Rx(idx - GDB_REG_NUM_Rx(0))]);
	else if (idx == GDB_REG_NUM_Rx(15))
		debugWriteRegToStr(packet, (regs[SWD_REGS_NUM_Rx(15)] &~ 1) | ((regs[SWD_REGS_NUM_XPSR] & CPSR_BIT_T) ? 1 : 0));	//clear bottom bit unless thumb, then set it
	else if (idx == GDB_REG_NUM_CPSR)
		debugWriteRegToStr(packet, regs[SWD_REGS_NUM_XPSR]);
	else if (idx >= GDB_REG_NUM_Fx(0) || idx <= GDB_REG_NUM_Fx(7)) {
		
		idx -= GDB_REG_NUM_Fx(0);
		idx *= 2;
		
		if (!cpuHasFpu(cpu))
			strcat(packet, "0000000000000000");	//no fpu -> no fpu regs
		else {
			//XXX: each F regs is two half regs	(not sure which first, we'll assume lowest first , that is S0 then S1, etc...)
			
			debugWriteRegToStr(packet, regs[SWD_REGS_NUM_Sx(idx + 0)]);
			debugWriteRegToStr(packet, regs[SWD_REGS_NUM_Sx(idx + 1)]);
		}
	}
	else if (idx == GDB_REG_NUM_FPS) {
		
		if (!cpuHasFpu(cpu))
			strcat(packet, "00000000");	//no fpu -> no fpu regs
		else
			debugWriteRegToStr(packet, regs[SWD_REGS_NUM_FPCSR]);
	}
	else {
		fprintf(stderr, " debug: cannot reg DGB reg %u!\n", idx);
		return false;
	}
	
	return true;
}

static bool debugGetRegFromStr32(struct Cpu* cpu, const char **strP, uint32_t *retVal, bool* regsDirtyP)
{
	const char *str = *strP;
	uint32_t val;
	
	val = swap32(debugHtoiEx(&str, 8));
	
	if (str - *strP != 8)
		return false;
	
	*strP = str;
	*retVal = val;
	*regsDirtyP = true;		//this makes callingit easier. Yes, it is approximately same as the return val

	return true;
}

static bool debugGetRegFromStr(struct Cpu* cpu, uint32_t idx, const char **strP, uint32_t *regs, bool* regsDirtyP)
{
	if (idx >= GDB_REG_NUM_Rx(0) || idx <= SWD_REGS_NUM_Rx(15))
		return debugGetRegFromStr32(cpu, strP, regs + SWD_REGS_NUM_Rx(idx - GDB_REG_NUM_Rx(0)), regsDirtyP);
	else if (idx == GDB_REG_NUM_CPSR)
		return debugGetRegFromStr32(cpu, strP, regs + SWD_REGS_NUM_XPSR, regsDirtyP);
	else if (idx == GDB_REG_NUM_FPS)
		return debugGetRegFromStr32(cpu, strP, regs + SWD_REGS_NUM_FPCSR, regsDirtyP);
	else if (idx >= GDB_REG_NUM_Fx(0) || idx <= GDB_REG_NUM_Fx(7)) {
		
		idx -= GDB_REG_NUM_Fx(0);
		idx *= 2;
		
		//XXX: each F regs is two half regs	(not sure which first, we'll assume lowest first , that is S0 then S1, etc...)

		return debugGetRegFromStr32(cpu, strP, regs + SWD_REGS_NUM_Sx(idx + 0), regsDirtyP) && debugGetRegFromStr32(cpu, strP, regs + SWD_REGS_NUM_Sx(idx + 1), regsDirtyP);
	}

	return false;
}

//only fail if we cannot read ANYTHING, else return whatever we did read
static bool debugMemRead(struct Cpu* cpu, char *dst, uint32_t addr, uint32_t len)
{
	uint8_t *buf = alloca(len);
	uint32_t i, bytesRead;
	
	bytesRead = memioReadToBuffer(cpu, addr, len, buf);
	if (!bytesRead)
		return false;
	
	for (i = 0; i < bytesRead; i++)
		sprintf(dst + 2 * i, "%02x", buf[i]);
	
	return true;
}

//returns fals on fatal error, sets *doneP if we sent GDB an error, but debugging can continue
static bool debugMemWritePartial(SOCKTYP sock, struct Cpu *cpu, uint32_t wordAddr, uint32_t byteOfst, uint32_t byteLen, const uint8_t *data, bool *doneP)
{
	uint8_t nowBuf[4];
	
	//read memory
	if (!cpuMemReadEx(cpu, wordAddr, 1, (uint32_t*)nowBuf, true)) {
		fprintf(stderr, " debug: cannot read memory word at 0x%08X\n", wordAddr);
		*doneP = true;
		return debugSendPacket(sock, "E00", true);
	}
	
	//apply our change
	memcpy(nowBuf + byteOfst, data, byteLen);
	
	//now write it back
	if (!cpuMemWrite(cpu, wordAddr, 1, (uint32_t*)nowBuf, true)) {
		fprintf(stderr, " debug: cannot write memory word at 0x%08X\n", wordAddr);
		*doneP = true;
		return debugSendPacket(sock, "E00", true);
	}
	
	return true;
}

//sends own reply
static bool debugMemWrite(SOCKTYP sock, struct Cpu* cpu, char *dataAsText, uint32_t addr, uint32_t len, bool dataAlreadyBinary)
{
	uint8_t *data = (uint8_t*)dataAsText;
	uint32_t i;
	
	//first convert to binary data, if needed
	if (!dataAlreadyBinary) {
		for (i = 0; i < len; i++) {
			char *startCharPtr = dataAsText;
			uint32_t byteVal = debugHtoiEx((const char**)&dataAsText, 2);
			
			if (dataAsText - startCharPtr != 2)	//we must have enough chars and they must be two hexchars each
				return debugSendAck(sock);
			
			data[i] = byteVal;
		}
	}
	
	//if we have an unaligned set of start bytes, write them
	if (addr & 3) {
		
		uint32_t offset = addr & 3, nowLen = 4 - offset;
		bool done = false;
		
		if (nowLen > len)
			nowLen = len;
		
		if (!debugMemWritePartial(sock, cpu, addr &~ 3, offset, nowLen, data, &done))	//serious error and debugging is aborted
			return false;
		else if (done)																	//write error but debuggnig cna continue
			return true;
		
		addr += nowLen;
		len -= nowLen;
		data += nowLen;
	}
	
	//while we have complete words to write, write them
	while (len >= sizeof(uint32_t)) {
		
		uint32_t numWords = len / sizeof(uint32_t), *data32 = (uint32_t*)data;
		
		if (numWords > cpuGetOptimalWriteNumWords(cpu))
			numWords = cpuGetOptimalWriteNumWords(cpu);
		
		if (!cpuMemWrite(cpu, addr, numWords, data32, true)) {
			
			fprintf(stderr, " debug: cannot write %u memory word(s) at 0x%08X\n", numWords, addr);
			return debugSendPacket(sock, "E00", true);
		}

		len -= sizeof(uint32_t) * numWords;
		data += sizeof(uint32_t) * numWords;
		addr += sizeof(uint32_t) * numWords;
	}
	
	//if we have bytes left to write (unaligned tail bytes), write them
	if (len) {
		
		bool done = false;
		
		if (!debugMemWritePartial(sock, cpu, addr, 0, len, data, &done))	//serious error and debugging is aborted
			return false;
		else if (done)														//write error but debuggnig cna continue
			return true;
		
		addr += len;
		len -= len;
		data += len;
	}

	return debugSendPacket(sock, "OK", true);
}

static bool debugBreakpointSet(struct Cpu* cpu, const struct DebugTargetLimits *limits, uint32_t idx, bool enabled, uint32_t addr, uint32_t len, uint32_t bkptType)
{
	uint32_t val = 0;
	
	//we always set brakpoint sized 2 (we have no ARM mode, and it works fine for thumb 1 & 2)
	
	//disabling is easy, enabling depends on FBP version (and we assume here that caller vetted "size" and "addr" for us already)
	if (!enabled)
		val = 0;
	else if (limits->haveFbpV2)
		val = addr | 1;
	else
		val = (addr &~ 2) | ((addr & 2) ? (2 << 30) : (1 << 30)) | 1;
	
	return cpuMemWrite(cpu, ADDR_FBP_COMP0 + OFST_FBP_NEXT * idx, 1, &val, true);
}

static bool debugWatchpointSet(struct Cpu* cpu, const struct DebugTargetLimits *limits, uint32_t idx, bool enabled, uint32_t addr, uint32_t len, uint32_t bkptType)
{
	uint32_t wptConfig[3] = {0, };
	
	if (enabled) {			//prepare data (assumes error checking has already been done by the caller)
		
		wptConfig[0] = addr;
		wptConfig[1] = 31 - clz(len);		//calculate log_2(len)
		
		switch (bkptType) {
			case '2':									//write wpt
				wptConfig[2] = WPT_TYPE_DATA_WRITE;
				break;
			case '3':									//read wpt
				wptConfig[2] = WPT_TYPE_DATA_READ;
				break;
			case '4':									//rw wpt
				wptConfig[2] = WPT_TYPE_DATA_RW;
				break;
			default:
				return false;
		}
	}
	
	return cpuMemWrite(cpu, ADDR_DWT_COMP0 + OFST_DWT_NEXT * idx, 3, wptConfig, true);
}

static bool debugReplyToGdb(struct Cpu* cpu, SOCKTYP sock, char *packet, const struct DebugTargetLimits *limits, struct DebugBptWptTrackingStruct *bptsTracking, struct DebugBptWptTrackingStruct *wptsTracking, bool *isRunningP, bool *regsValidP, bool* regsDirtyP, uint32_t *regs, bool *exitAsSuccessP)
{
	uint32_t i, addr, len;
	
	if (packet == strstr(packet, "qSupported"))						//Q: what do we support? -> answer
		return debugSendPacket(sock, "PacketSize=1024", true);
	
	if (packet == strstr(packet, "qAttached"))						//Q: did we attach to anexisting process (as opposed to making new one)? -> yes
		return debugSendPacket(sock, "1", true);
	
	if (packet[0] == 'v')											// we support none of the v-packets -> empty packet is the appropriate reply here
		goto send_empty_packet;
	
	if (!strcmp(packet, "qTStatus"))								//we support none of these queries -> empty packet is the appropriate reply here
		goto send_empty_packet;
	
	if (!strcmp(packet, "qfThreadInfo"))							//we support none of these queries -> empty packet is the appropriate reply here
		goto send_empty_packet;
	
	if (!strcmp(packet, "k")) {										//"kill" = quite GDB debugging, no reply expected
		*exitAsSuccessP = true;
		return true;
	}
	
	if (!strcmp(packet, "qC"))										//we support none of these queries -> empty packet is the appropriate reply here
		goto send_empty_packet;
	
	if (!strcmp(packet, "vCont?"))									//"do we support vCont?" -> no
		goto send_empty_packet;
	
	if (packet == strstr(packet, "qL"))								//query from rtos, we support none of these queries -> empty packet is the appropriate reply here
		goto send_empty_packet;
	
	if (packet[0] == 'H' && (packet[1] == 'c' || packet[1] == 'g'))	//"set thread id for next commands" -> set thread...ok...we only have one
		goto send_ok_packet;
	
	if (!strcmp(packet, "qSymbol::"))								//"do we want to use gdb to look up a symbol?"	-> reply "OK" means no (yes, you read that right)
		goto send_ok_packet;

	if (!strcmp(packet, "?"))										//"why did we stop?"
		return debugSendPacket(sock, "S05", true);

	if (!strcmp(packet, "qOffsets"))								// get our offsets (generally zeros)
		return debugSendPacket(sock,  "Text=0;Data=0;Bss=0", true);

	if (packet[0] == 's' || packet[0] == 'S') {						//"step"
		
		if (!debugRegsCacheFlush(cpu, regsValidP, regsDirtyP, regs))
			return false;
		
		*regsValidP = false;
		if (CPU_STAT_CODE_FAILED == cpuStep(cpu)) {
			fprintf(stderr, " debug: failed to step\n");
			return false;
		}
		
		return debugSendPacket(sock, "S05", true);
	}

	if (packet[0] == 'c' || packet[0] == 'C') {						//"continue"
		
		if (!debugRegsCacheFlush(cpu, regsValidP, regsDirtyP, regs))
			return false;
		
		*regsValidP = false;
		if (!cpuGo(cpu)) {
			fprintf(stderr, " debug: failed to go\n");
			return false;
		}
		
		*isRunningP = true;
		goto send_just_ack;				//no reply expected, buck ACK is
	}

	if (!strcmp(packet, "D")) {										//"detach"
		
		//TODO: detach ops (if any)

		*exitAsSuccessP = true;
		goto send_ok_packet;
	}

	if (!strcmp(packet, "g")) {										//get regs
		
		//cache regs if needed
		if (!debugRegsCacheFill(cpu, regsValidP, regsDirtyP, regs))
			return false;
		
		//produce the result packet
		packet[0] = 0;
		for(i = 0; i < GDB_REG_NUM; i++) {
			if (!debugAddRegToStr(cpu, packet, regs, i))
				return false;
		}
		return debugSendPacket(sock, packet, true);
	}
	
	if (packet[0] == 'p') {											//read reg
		
		const char *p = packet + 1;
		
		//cache regs if needed
		if (!debugRegsCacheFill(cpu, regsValidP, regsDirtyP, regs))
			return false;
		
		i = debugHtoi(&p);
		if (*p++)
			goto malformed_packet_send_nak;
		
		packet[0] = 0;
		if (!debugAddRegToStr(cpu, packet, regs, i))
			return false;

		return debugSendPacket(sock, packet, true);
	}
	
	if (packet[0] == 'P') {											//write reg
		
		const char *p = packet + 1;
		
		//cache regs if needed
		if (!debugRegsCacheFill(cpu, regsValidP, regsDirtyP, regs))
			return false;
		
		i = debugHtoi(&p);
		if (*p++ != '=')
			goto malformed_packet_send_nak;
		
		if (!debugGetRegFromStr(cpu, i, &p, regs, regsDirtyP) || *p)
			return false;

		goto send_ok_packet;
	}
	
	if (!strcmp(packet, "G")) {										//write regs
		
		const char *p = packet + 1;
		
		//cache regs if needed
		if (!debugRegsCacheFill(cpu, regsValidP, regsDirtyP, regs))
			return false;
		
		//produce the result packet
		for(i = 0; i < GDB_REG_NUM; i++) {
			if (!debugGetRegFromStr(cpu, i, &p, regs, regsDirtyP))
				return false;
		}
		if (*p)
			return false;

		goto send_ok_packet;
	}
	
	if (packet[0] == 'm') {											//read memory
		
		const char *p = packet + 1;
		
		addr = debugHtoi(&p);
		if (*p++ != ',')
			goto malformed_packet_send_nak;
		len = debugHtoi(&p);
		if (*p++)
			goto malformed_packet_send_nak;
		
		//truncate the read
		if (len > GDB_MAX_READ_WRITE)
			len = GDB_MAX_READ_WRITE;
		
		//sanity-check the read and truncate as needed
		if (((uint64_t)addr + len) > (1ULL << 32))
			len = (1ULL << 32) - addr;
		
		//do the read
		if (!debugMemRead(cpu, packet, addr, len))
			goto send_error_packet;

		 return debugSendPacket(sock, packet, true);
	}
	
	if (packet[0] == 'M' || packet[0] == 'X') {						//write memory
		
		char *p = packet + 1;
		
		addr = debugHtoi((const char**)&p);
		if (*p++ != ',')
			goto malformed_packet_send_nak;
		len = debugHtoi((const char**)&p);
		if (*p++ != ':')
			goto malformed_packet_send_nak;
		
		//truncate the read
		if (len > GDB_MAX_READ_WRITE)
			goto malformed_packet_send_nak;
		
		//sanity-check the write
		if (((uint64_t)addr + len) > (1ULL << 32))
			goto malformed_packet_send_nak;
		
		//do the write (sends own reply)
		return debugMemWrite(sock, cpu, p, addr, len, packet[0] == 'X');
	}
	
	if (packet[0] == 'Z' || packet[0] == 'z') {						//set/clear a breakpoint/watchpoint
		
		struct DebugBptWptTrackingStruct *ts = wptsTracking;
		uint32_t maxItems = limits->numWpts;
		const char *p = packet + 1;
		char bkptType;
		uint32_t kind;
		
		bkptType = *p++;
		
		if (bkptType == '2') { 								//write watchpoints may be unsupported
			if (!limits->haveWriteDataWpts)
				goto send_empty_packet;
		}
		else if (bkptType == '3') { 								//read watchpoints may be unsupported
			if (!limits->haveReadDataWpts)
				goto send_empty_packet;
		}
		else if (bkptType == '4') {									//access watchpoints may be unsupported
			if (!limits->haveRwDataWpts)
				goto send_empty_packet;
		}
		else if (bkptType != '0' && bkptType != '1')				//anything that is not a hw or a sw breakpoint isnt supported
			goto send_empty_packet;
		
		//if we got this far, we're being asked to set or clear a breakpoint/watchpoint of a type that we support -> keep parsing the packet
		if (*p++ != ',')
			goto malformed_packet_send_nak;
		addr = debugHtoi(&p);
		if (*p++ != ',')
			goto malformed_packet_send_nak;
		kind = debugHtoi(&p);
		if (*p++)
			goto malformed_packet_send_nak;
		
		if (bkptType == '1' || bkptType == '0') {					//for breakpoints, length is determined in a special way
			
			switch(kind) {
				case 2:
					len = 2;					//16-bit Thumb mode breakpoint
					break;
				case 3:
					len = 4;					//32-bit Thumb mode (Thumb-2) breakpoint
					break;
				case 4:
					len = 4;					//32-bit ARM mode breakpoint
					break;
				default:
					goto send_empty_packet;		//some other kind we do not support
			}
			
			//for FBPv1, only some addresses are valid. Check request against that limist
			if (!limits->haveFbpV2 && (addr >> 29))
				goto send_error_packet;
			
			//clear bottom bit always
			addr &=~ 1;
			
			ts = bptsTracking;
			maxItems = limits->numBpts;
		}
		else {									//for watchpoints, "kind" *IS* length
			
			len = kind;
			if (!len || (len & (len - 1)) || (1ULL << limits->wptMaxLogSz) < len) {
				fprintf(stderr, " debug: cannot set %u-byte watchpoint\n", len);
				goto send_error_packet;
			}
		}
		
		//some sanity checks
		if (((uint64_t)addr + len) > (1ULL << 32))
			goto malformed_packet_send_nak;
		
		//at this point we're sure the request makes sense - see if we can perform it
		if (packet[0] == 'Z') {		//insert
			
			//find a spot for it
			for (i = 0; i < maxItems; i++) {
				
				if (!ts[i].used)
					break;
			}
			
			if (i == maxItems)				//found a spot
				goto send_error_packet;
			
			//record it
			ts[i].addr = addr;
			ts[i].len = len;
			ts[i].type = bkptType;
			ts[i].used = true;
		}
		else {						//remove
			
			//find its record
			for (i = 0; i < maxItems; i++) {
				
				if (ts[i].addr == addr && ts[i].len == len && ts[i].type == bkptType && ts[i].used)
					break;
			}
			
			if (i == maxItems)				//did not find it
				goto send_error_packet;
			
			//mark struct entry as free
			ts[i].used = false;
		}
		
		DBG("breakpoint action\n");
		
		if (bkptType == '0' || bkptType == '1') {
			if (!debugBreakpointSet(cpu, limits, i, packet[0] == 'Z', addr, len, bkptType))
				goto send_error_packet;
		}
		else {
			if (!debugWatchpointSet(cpu, limits, i, packet[0] == 'Z', addr, len, bkptType))
				goto send_error_packet;
		}
		
		goto send_ok_packet;
	}

	fprintf(stderr, " debug: how do i respond to packet '%s'\n", packet);
	goto send_empty_packet;

send_empty_packet:
	return debugSendPacket(sock, "", true);

send_ok_packet:
	return debugSendPacket(sock, "OK", true);

send_error_packet:
	return debugSendPacket(sock, "E00", true);

malformed_packet_send_nak:
	fprintf(stderr, " debug: malformed packet '%s'\n", packet);
	return debugSendNak(sock);

send_just_ack:
	return debugSendAck(sock);
}

static bool debugGdbServer(struct Cpu* cpu, int port)
{
	struct DebugBptWptTrackingStruct *bptsTracking = NULL, *wptsTracking = NULL;
	bool isRunning = false, regsValid = false, regsDirty = false;
	struct DebugTargetLimits limits;
	uint32_t regs[4 * NUM_REGS];
	bool gdbReturnVal = false;
	SOCKTYP sock;
	
	fprintf(stderr, " debug: GDB server listening on port %u. Press [ENTER] to close connection\n", port);
	
	//accept a connection
	sock = debugAcceptGdbConn(port);
	if (sock == SOCKINVAL)
		return false;
	
	fprintf(stderr, " debug: accepted a connection\n");
	
	
	//get limits
	debugGatherTargetLimitations(cpu, &limits);
	
	//alloc arrays to track availability of each breakpoint and watchpoint
	bptsTracking = calloc(sizeof(struct DebugBptWptTrackingStruct), limits.numBpts);
	wptsTracking = calloc(sizeof(struct DebugBptWptTrackingStruct), limits.numWpts);
	
	if (!bptsTracking || !wptsTracking)
		goto out_close_sock;
	
	while(1) {

		uint32_t v32;
		int ret;
		
		do {
			//if it is running, wait with no timout so we dont delay delivery of signal from target
			//if it is stopped, we can hang here since target will go nowhere
			struct timeval tv = {0,};
			fd_set set;
			
			FD_ZERO(&set);
			FD_SET(sock, &set);
			FD_SET(STDIN_FILENO, &set);
			tv.tv_sec = isRunning ? 0 : 100;
			ret = select(NFDS(sock, 2), &set, NULL, NULL, &tv);
			
		}while(ret == -1 && errno == EINTR);
		
		if (utilGetKey()) {
			fprintf(stderr, " debug: key detected. Closing connection.\n");
			goto out_ok;
		}
		
		//see if target stopped while we were busy
		if (isRunning && (v32 = cpuIsStoppedAndWhy(cpu)) != CPU_STAT_CODE_FAILED) {
			
			DBG("stop %u\n", v32);
			
			if (!debugSendPacket(sock, "S05", false))
				goto out_close_sock;
			isRunning = false;
		}
		
		//if we got a packet from the debugger, address it
		if (ret == 1) {
			
			char c;
			
			ret = recv(sock, &c, 1, 0);
			if (ret == 0)	//EOF
				goto out_ok;
			
			if (ret != 1) {
				fprintf(stderr, " debug: failed to receive byte (1)\n");
				goto out_close_sock;
			}
			
			if (c == 3) {		//Ctrl + C
				if (!debugSendPacket(sock, "S11", false))
					goto out_close_sock;
				if (!cpuStop(cpu)) {
					fprintf (stderr, " debug: CPU STOP failed\n");
					goto out_close_sock;
				}
				if (!debugRegsCacheFill(cpu, &regsValid, &regsDirty, regs))
					goto out_close_sock;
				regsDirty = false;
			}
			else if (c == '+') {
				
				//we got an ack - ignore it
			}
			else if (c == '-') {
				
				//we got a nack - we failed. quit
				fprintf(stderr, " debug: GDB does not understand us (got a NAK). Giving up.\n");
				goto out_close_sock;
			}
			else if (c != '$') {
				
				printf(" debug: unknown packet header '%c'(0x%02X)\n", c, c);
				goto out_close_sock;
			}
			else {
				uint8_t checksumClaimed = 0, checksumCalced = 0;
				bool inEscape = false, exitAsSuccess = false;
				int len = 0, endTrackingState = 0;
				char packet[GDB_BUF_SZ];

				while(1) {
					
					ret = recv(sock, &c, 1, 0);
					if (ret == 0)	//EOF
						goto out_ok;
					
					if (ret != 1) {
						fprintf(stderr, " debug: failed to receive byte (2)\n");
						goto out_close_sock;
					}
					
					if (c == '#') {
						endTrackingState = 1;
						continue;
					}
					
					if (!endTrackingState)
						checksumCalced += c;
					
					if (inEscape) {
						c = c ^ 0x20;
						inEscape = 0;
					}
					else if (c == 0x7d)
						inEscape = 1;
					
					if (inEscape)		//escape char need sno further handling
						continue;
					
					if (endTrackingState) {
						
						if (c >= '0' && c <= '9')
							c -= '0';
						else if (c >= 'A' && c <= 'F')
							c -= 'A' - 10;
						else if (c >= 'a' && c <= 'f')
							c -= 'a' - 10;
						else {
							
							fprintf(stderr, " debug: unexpected checksum byte '%c'(0x%02X)\n", c, c);
							goto out_close_sock;
						}
						checksumClaimed = (checksumClaimed << 4) + c;
						if (endTrackingState == 2) {
							
							if (checksumClaimed != checksumCalced) {
								fprintf(stderr, " debug: checksum error. Claimed 0x%02X, calced 0x%02X\n", checksumClaimed, checksumCalced);
								goto out_close_sock;
							}
							
							//terminate for convenience
							packet[len] = 0;
							break;
						}

						endTrackingState++;
						continue;
					}
					
					packet[len++] = c;
					if (len == sizeof(packet) - 1) {	//leave space for NULL-terminator
						fprintf (stderr, " debug: packet too long. Aborting debug\n");
						goto out_close_sock;
					}
				}
				
				DBG("RX: '%s'\n", packet);
				
				if (!debugReplyToGdb(cpu, sock, packet, &limits, bptsTracking, wptsTracking, &isRunning, &regsValid, &regsDirty, regs, &exitAsSuccess))
					goto out_close_sock;
				else if (exitAsSuccess)
					goto out_ok;
			}
		}
	}
	
out_ok:
	gdbReturnVal = true;
	//fallthrough
	
out_close_sock:
	free(bptsTracking);
	free(wptsTracking);
	closesocket(sock);
	return gdbReturnVal;
}
