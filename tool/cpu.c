#include <string.h>
#include <stdlib.h>
#include "scriptfiles.h"
#include "alloca.h"
#include "cpu.h"


struct Cpu {
	struct Debugger *dbg;
	struct PotentialScriptfile *scptOpts;
	uint8_t numCores;
	uint8_t curCore;
	struct CpuCoreInfo {
		uint16_t identifier;
		uint16_t cortexType;
		uint32_t romTableBase;
		bool isV7;
		bool haveFpu;
	} cores [];
};

#define ADDR_DEMCR					0xE000EDFCUL
#define DEMCR_BIT_CORERESET			0x00000001UL

static struct PotentialScriptfile* cpuDoCpuid(struct Cpu *cpu, uint32_t romTableBase, bool verbose)
{
	uint32_t i, peripheralIds[8];
	uint64_t periphId = 0;
	
	if ((romTableBase & 0x0FFF) != 3) {
		fprintf(stderr, "CPU ROMTABLE type invalid\n");
		return false;
	}
	
	romTableBase &= 0xFFFFF000UL;
	
	if (!cpuMemRead(cpu, romTableBase + 0xfd0, 8, peripheralIds)) {
		fprintf(stderr, "CPU identifying info read fail\n");
		return false;
	}
	
	periphId += ((uint64_t)peripheralIds[0]) << 32;
	periphId += ((uint64_t)peripheralIds[1]) << 40;
	periphId += ((uint64_t)peripheralIds[2]) << 48;
	periphId += ((uint64_t)peripheralIds[3]) << 56;
	periphId += ((uint64_t)peripheralIds[4]) <<  0;
	periphId += ((uint64_t)peripheralIds[5]) <<  8;
	periphId += ((uint64_t)peripheralIds[6]) << 16;
	periphId += ((uint64_t)peripheralIds[7]) << 24;
	
	if (verbose)
		fprintf(stderr, "periphId = 0x%016llx\n", (unsigned long long)periphId);
	
	if ((periphId >> 19) & 1) {	//has jedec id
		
		struct JedecInfo {
			uint8_t cont, code;
			const char *name;
		} static const jedecs[] = {
			#define JEDEC(a,b,c)	{a,(b) & 0x7f,c},
			#include "jedec.h"
			#undef JEDEC
		};
		uint32_t i, model = (uint32_t)periphId & 0xfff;
		uint8_t cont = (uint32_t)(periphId >> 32) & 0x0f;
		uint8_t code = (uint32_t)(periphId >> 12) & 0x7f;
		
		if (verbose)
			fprintf(stderr, "JEDEC = {%d, 0x%02X, 0x%03x}\n", cont, code, model);
		
		//we could do a binary search, but why?
		
		for (i = 0; i < sizeof(jedecs) / sizeof(*jedecs); i++) {
			if (jedecs[i].cont == cont && jedecs[i].code == code) {
				if (verbose)
					fprintf(stderr, "Chip is by '%s', model 0x%03X\n", jedecs[i].name, model);
				break;
			}
		}
	}
	if (verbose) {
		fprintf(stderr, "CPUID = {");
		for (i = 0; i < 8; i++) {
			fprintf(stderr, "0x%08x, ", peripheralIds[i]);
		}
		fprintf(stderr, "}\n");
	}
	
	return scriptfilesFind(cpu, peripheralIds);
}

static uint32_t cpuTranslateAttachPacketV1toV2(struct SwdCommsAttachRespPacketV2 *dst, const struct SwdCommsAttachRespPacketV1 *src)
{
	uint16_t cortexType = src->cortexType;
	uint8_t flags = src->flags;
	uint32_t romTableBase = src->romTableBase;
	
	if (!cortexType) {
		dst->error = src->error;
		return sizeof(struct SwdCommsAttachRespPacketV2);
	}
	
	dst->cores[0].flags = flags;
	dst->cores[0].cortexType = cortexType;
	dst->cores[0].romTableBase = romTableBase;
	dst->error = 0;
	
	return sizeof(struct SwdCommsAttachRespPacketV2) + sizeof(struct SwdCommsAttachRespCoreInfo);
}




struct Cpu* cpuAttach(struct Debugger *dbg, bool verbose)
{
	uint8_t *buf = alloca(debuggerGetMaxXferSz(dbg));
	struct CommsPacket *pkt = (struct CommsPacket*)buf;
	struct SwdCommsAttachRespPacketV1 *attachRspPktV1 = (struct SwdCommsAttachRespPacketV1*)pkt->payload;
	struct SwdCommsAttachRespPacketV2 *attachRspPktV2 = (struct SwdCommsAttachRespPacketV2*)pkt->payload;
	uint32_t i, nopts, replySz, numCores;
	struct PotentialScriptfile *psc;
	char *cpuDispName = NULL;
	bool haveCoreIds = true;
	struct Cpu *ret;
	
	pkt->cmd = SWD_COMMS_CMD_ATTACH;
	replySz = debuggerDoOneDevCmd(dbg, pkt, 0, pkt);
	
	if (replySz == sizeof(struct SwdCommsAttachRespPacketV1)) {
		replySz = cpuTranslateAttachPacketV1toV2(attachRspPktV2, attachRspPktV1);
		haveCoreIds = false;
	}
	
	if (replySz < sizeof(struct SwdCommsAttachRespPacketV2) || (replySz - sizeof(struct SwdCommsAttachRespPacketV2)) % sizeof(struct SwdCommsAttachRespCoreInfo)) {
		fprintf(stderr, "ATTACH CMD ERR\n");
		return NULL;
	}
	
	numCores = (replySz - sizeof(struct SwdCommsAttachRespPacketV2)) / sizeof(struct SwdCommsAttachRespCoreInfo);
	
	if (numCores && attachRspPktV2->error) {
		fprintf(stderr, "ATTACH CMD PARSE ERR (both cores and error)\n");
		return NULL;
	}
	if (!numCores && !attachRspPktV2->error) {
		fprintf(stderr, "ATTACH CMD PARSE ERR (no cores and no error)\n");
		return NULL;
	}
	if (attachRspPktV2->error) {
		const char **mErrors;
		static const char *mErrorsSwd[16] = {
				"SWD",
				"IDCODE read failed",
				"DAP is V0",
				"REG write failed",
				"REG read failed",
				"RDBUF read failed",
				"CTRL/STAT is weird",
				"AHB not found",
		};
		static const char *mErrorsMemap[16] = {
				"MEMAP",
				"CFG read failed",
				"MEM is BigEndian",
				"ADDR SIZE cannot be set",
				"ROMTAB addr read failed",
				"ROMTAB reg invalid",
				"AP SEL failed",
		};
		static const char *mErrorsCortex[16] = {
				"CORTEX",
				"CPUID read failed",
				"CPU type unknown",
				"DEMCR write failed",
				"WPT setup failed",
				"BPT setup failed",
				"FPU check failed",
		};

		fprintf(stderr, "Attach error: ");
		switch (attachRspPktV2->error & ERR_FLAG_TYPE_MASK) {
			case ERR_FLAG_TYPE_SWD:
				mErrors = mErrorsSwd;
				break;
				
			case ERR_FLAG_TYPE_MEMAP:
				mErrors = mErrorsMemap;
				break;
			
			case ERR_FLAG_TYPE_CORTEX:
				mErrors = mErrorsCortex;
				break;
			default:
				mErrors = NULL;
				break;
		}
		fprintf(stderr, "ERROR {");
		if (!mErrors)
			fprintf(stderr, "0x%02X", attachRspPktV2->error);
		else {
			const uint8_t errNum = attachRspPktV2->error &~ ERR_FLAG_TYPE_MASK;
			const char *errStr = mErrors[errNum];
			
			fprintf(stderr, "'%s', ", mErrors[0]);
			if (errStr)
				fprintf(stderr, "'%s'", errStr);
			else
				fprintf(stderr, "0x%02X", errNum);
		}

		fprintf(stderr, "}\n");
		return NULL;
	}
	
	if (numCores > 1 && verbose)
		fprintf(stderr, " Multiple cores found: %u\n", numCores);
	
	ret = (struct Cpu*)calloc(1, sizeof(struct Cpu) + numCores * sizeof(struct CpuCoreInfo));
	if (!ret)
		return NULL;
	
	for (i = 0; i < numCores; i++) {
		
		ret->cores[i].identifier = haveCoreIds ? attachRspPktV2->cores[i].identifier : 0;
		ret->cores[i].cortexType = attachRspPktV2->cores[i].cortexType;
		ret->cores[i].romTableBase = attachRspPktV2->cores[i].romTableBase;
		ret->cores[i].isV7 = false;
		ret->cores[i].haveFpu = false;
		
		switch (attachRspPktV2->cores[i].cortexType) {
			case CPUID_PART_M0:
				if (verbose)
					fprintf(stderr, "  Cortex-M0 found\n");
				break;
			
			case CPUID_PART_M1:
				if (verbose)
					fprintf(stderr, "  Cortex-M1 found\n");
				break;
			
			case CPUID_PART_M3:
				if (verbose)
					fprintf(stderr, "  Cortex-M3 found\n");
				ret->cores[i].isV7 = true;
				break;
			
			case CPUID_PART_M4:
				ret->cores[i].haveFpu = !!(attachRspPktV2->cores[i].flags & SWD_FLAG_HAS_FPU);
				if (verbose)
					fprintf(stderr, "  Cortex-M4%s found\n", ret->cores[i].haveFpu ? "F" : "");
				ret->cores[i].isV7 = true;
				break;
			
			case CPUID_PART_M7:
				ret->cores[i].haveFpu = !!(attachRspPktV2->cores[i].flags & SWD_FLAG_HAS_FPU);
				if (verbose)
					fprintf(stderr, "  Cortex-M7%s found\n", ret->cores[i].haveFpu ? "F" : "");
				ret->cores[i].isV7 = true;
				break;
			
			case CPUID_PART_M0p:
				if (verbose)
					fprintf(stderr, "  Cortex-M0+ found\n");
				break;
			
			case CPUID_PART_UNREADABLE:
				if (verbose)
					fprintf(stderr, "  'CPUID unreadable'\n");
				break;
			
			default:
				if (verbose)
					fprintf(stderr, "  'Unknown(0x%04X)'\n", attachRspPktV2->cores[i].cortexType);
				break;
		}
		if (verbose) {
			if (haveCoreIds)
				fprintf(stderr, "   CORE ID 0x%04x\n", attachRspPktV2->cores[i].identifier);
			fprintf(stderr, "   ROMTABLE base 0x%08x\n", attachRspPktV2->cores[i].romTableBase);
		}
	}
	ret->dbg = dbg;
	ret->numCores = numCores;
	if (ret->numCores <= 1)
		ret->curCore = 0;			//the only core is always already selected
	else {							//in multi-core, laways actively select a core
		ret->curCore = numCores;	//definiteily an invalid number for curCore
		if(!cpuSelectCore(ret, ret->cores[0].identifier))
			fprintf(stderr, " core selection failed - expect issues\n");
	}
	
	if (numCores > 1 && verbose)
		fprintf(stderr, " Will select core 0x%04x by default\n", ret->cores[ret->curCore].identifier);
	
	ret->scptOpts = cpuDoCpuid(ret, ret->cores[ret->curCore].romTableBase, verbose);
	
	for (nopts = 0, psc = ret->scptOpts; psc; nopts++, psc = psc->next);	//cout potions
	
	if (verbose) {
		fprintf(stderr, "CPUID/script options found: %u%s\n", nopts, nopts ? ":" : "");
		
		for (psc = ret->scptOpts; psc; psc = psc->next) {
			
			fprintf(stderr, "  %s @ '%s'\n", psc->cpuName, psc->scriptfilePath);
		}
	}
	
	return ret;
}

const struct PotentialScriptfile *cpuGetScriptfileOptions(const struct Cpu* cpu)
{
	return cpu->scptOpts;
}

void cpuFree(struct Cpu* cpu)
{
	struct PotentialScriptfile *t;
	
	while(cpu->scptOpts) {
		
		t = cpu->scptOpts;
		cpu->scptOpts = cpu->scptOpts->next;
		
		free(t->cpuName);
		free(t->scriptfileBaseName);
		free(t->scriptfilePath);
		fclose(t->scriptfile);
		free(t);
	}
	
	free(cpu);
}

//cannot do both at once!
static bool cpuRegsGetSet(struct Cpu *cpu, uint8_t regSet, uint32_t *dst, const uint32_t *src)
{
	uint8_t *buf = alloca(debuggerGetMaxXferSz(cpu->dbg));
	struct CommsPacket *pkt = (struct CommsPacket*)buf;
	struct SwdCommsRegsPacket *rpp = (struct SwdCommsRegsPacket*)pkt->payload;
	uint32_t payloadSzIn = sizeof(struct SwdCommsRegsPacket), payloadSzOut;
	int i;
	
	pkt->cmd = dst ? SWD_COMMS_CMD_REGS_READ : SWD_COMMS_CMD_REGS_WRITE;
	rpp->regSet = regSet;
	
	if (src) {
		for (i = 0; i < 16; i++)
			rpp->regs[i] = src[i];
		payloadSzIn += sizeof(uint32_t) * NUM_REGS;
	}

	payloadSzOut = debuggerDoOneDevCmd(cpu->dbg, pkt, payloadSzIn, pkt);
	if (payloadSzOut == PACKET_RX_FAIL || payloadSzOut < sizeof(struct SwdCommsRegsPacket)) {
		fprintf(stderr, "REG R/W CMD ERR 0\n");
		return false;
	}
	
	if (rpp->regSet == SWD_COMMS_REG_SET_ERROR) {
		fprintf(stderr, "REG R/W remote ERR\n");
		return false;
	}
	
	payloadSzOut -= sizeof(struct SwdCommsRegsPacket);
	
	if (src && payloadSzOut) {
		fprintf(stderr, "REG R/W CMD ERR 1\n");
		return false;
	}
	
	if (!src && payloadSzOut != sizeof(uint32_t) * NUM_REGS) {
		fprintf(stderr, "REG R/W CMD ERR 2\n");
		return false;
	}
	
	if (dst) {
		for (i = 0; i < 16; i++)
			dst[i] = rpp->regs[i];
	}
	
	return true;
}

bool cpuRegsGet(struct Cpu *cpu, uint8_t regSet, uint32_t *dst)
{
	return cpuRegsGetSet(cpu, regSet, dst, NULL);
}

bool cpuRegsSet(struct Cpu *cpu, uint8_t regSet, const uint32_t *src)
{
	return cpuRegsGetSet(cpu, regSet, NULL, src);
}

//cannot do both at once!
static bool cpuMemReadWrite(struct Cpu *cpu, uint32_t base, uint32_t numWords, uint32_t *dst, const uint32_t *src, bool withAck, bool silent)
{
	uint8_t *bufIn = alloca(debuggerGetMaxXferSz(cpu->dbg)), *bufOut = alloca(debuggerGetMaxXferSz(cpu->dbg));
	uint32_t xferWords = dst ? cpuGetOptimalWriteNumWords(cpu) : cpuGetOptimalReadNumWords(cpu), i;
	struct CommsPacket *pktIn = (struct CommsPacket*)bufIn, *pktOut = (struct CommsPacket*)bufOut;
	struct SwdCommsMemPacket *rppIn = (struct SwdCommsMemPacket*)pktIn->payload;
	struct SwdCommsMemPacket *rppOut = (struct SwdCommsMemPacket*)pktOut->payload;

	
	while (numWords) {
		uint32_t wordsNow = numWords > xferWords ? xferWords : numWords;
		uint32_t payloadSzIn = sizeof(struct SwdCommsMemPacket), payloadSzOut;
		
		pktOut->cmd = dst ? SWD_COMMS_CMD_MEM_READ : SWD_COMMS_CMD_MEM_WRITE;
		rppOut->addr = base;
		rppOut->numWords = wordsNow;
		
		//this loop just handles the case of us getting a read "REPLY "to a previous read request that somehow was stuck in the queue
		//yes, it happens :(
		if (src) {
			for (i = 0; i < wordsNow; i++)
				rppOut->words[i] = *src++;
			payloadSzIn += sizeof(uint32_t) * wordsNow;
		}
		
		do {
			
			bool withAckNow = true;
			
			if (!withAck && wordsNow == xferWords) {
				rppOut->numWords = SWD_COMMS_MAX_XFER_WORDS_NO_ACK;
				withAckNow = false;
			}
			
			payloadSzOut = debuggerDoOneDevCmd(cpu->dbg, pktOut, payloadSzIn, withAckNow ? pktIn : NULL);
			if (!withAckNow)
				break;
			
			if (payloadSzOut == PACKET_RX_FAIL || payloadSzOut < sizeof(struct SwdCommsMemPacket)) {
				if (!silent)
					fprintf(stderr, "MEM R/W CMD ERR 0\n");
				return false;
			}
			
			if (rppIn->numWords == SWD_MEM_NUM_WORDS_ERROR) {
				if (!silent)
					fprintf(stderr, "MEM R/W remote ERR\n");
				return false;
			}
			
			payloadSzOut -= sizeof(struct SwdCommsMemPacket);
			
			if (src && payloadSzOut) {
				if (!silent)
					fprintf(stderr, "MEM R/W CMD ERR 1\n");
				return false;
			}
			
			if (!src && payloadSzOut != sizeof(uint32_t) * wordsNow) {
				if (!silent)
					fprintf(stderr, "MEM R/W CMD ERR 2\n");
				return false;
			}
			
		} while (rppOut->addr != rppIn->addr);
		
		if (dst) {
			for (i = 0; i < wordsNow; i++)
				*dst++ = rppIn->words[i];
		}
		
		base += wordsNow * sizeof(uint32_t);
		numWords -= wordsNow;
	}
	
	return true;
}

bool cpuMemReadEx(struct Cpu *cpu, uint32_t base, uint32_t numWords, uint32_t *dst, bool silent/* do not write any errors to stdout*/)
{
	return cpuMemReadWrite(cpu, base, numWords, dst, NULL, true, silent);
}

bool cpuMemRead(struct Cpu *cpu, uint32_t base, uint32_t numWords, uint32_t *dst)
{
	return cpuMemReadEx(cpu, base, numWords, dst, false);
}

bool cpuMemWrite(struct Cpu *cpu, uint32_t base, uint32_t numWords, const uint32_t *src, bool withAck)
{
	return cpuMemReadWrite(cpu, base, numWords, NULL, src, withAck, false);
}

static uint32_t cpuConvertPacketSzToNumWordsXferred(uint32_t packetSz)
{
	return (packetSz - sizeof(struct CommsPacket) - sizeof(struct SwdCommsMemPacket)) / sizeof(uint32_t);
}

uint32_t cpuGetOptimalReadNumWords(struct Cpu *cpu)
{
	return cpuConvertPacketSzToNumWordsXferred(debuggerGetMaxXferSz(cpu->dbg));
}

uint32_t cpuGetOptimalWriteNumWords(struct Cpu *cpu)
{
	return cpuConvertPacketSzToNumWordsXferred(debuggerGetMaxXferSz(cpu->dbg));
}

static uint8_t cpuSimpleCmd(struct Cpu *cpu, uint8_t cmd)
{
	uint8_t *buf = alloca(debuggerGetMaxXferSz(cpu->dbg));
	struct CommsPacket *pkt = (struct CommsPacket*)buf;
	
	pkt->cmd = cmd;
	if (1 != debuggerDoOneDevCmd(cpu->dbg, pkt, 0, pkt)) {
		fprintf(stderr, "SIMPLE CMD ERR\n");
		return 0xFF;
	}
	
	return pkt->payload[0];
}

uint8_t cpuStop(struct Cpu *cpu)
{
	return cpuSimpleCmd(cpu, SWD_COMMS_CMD_STOP);
}

bool cpuReset(struct Cpu *cpu)
{
	return !!cpuSimpleCmd(cpu, SWD_COMMS_CMD_RESET);
}

bool cpuGo(struct Cpu *cpu)
{
	return !!cpuSimpleCmd(cpu, SWD_COMMS_CMD_GO);
}

uint8_t cpuStep(struct Cpu *cpu)
{
	return cpuSimpleCmd(cpu, SWD_COMMS_CMD_SINGLE_STEP);
}

uint8_t cpuIsStoppedAndWhy(struct Cpu *cpu)
{
	return cpuSimpleCmd(cpu, SWD_COMMS_CMD_IS_STOPPED);
}

uint8_t cpuResetStop(struct Cpu *cpu)
{
	uint32_t demcr, demcrNew;
	
	if (!cpuMemRead(cpu, ADDR_DEMCR, 1, &demcr))
		return CPU_STAT_CODE_FAILED;

	demcrNew = demcr | DEMCR_BIT_CORERESET;
	
	if (!cpuMemWrite(cpu, ADDR_DEMCR, 1, &demcrNew, true))
		return CPU_STAT_CODE_FAILED;
	
	if (!cpuReset(cpu))
		return CPU_STAT_CODE_FAILED;
	
	if (!cpuMemWrite(cpu, ADDR_DEMCR, 1, &demcr, true))
		return CPU_STAT_CODE_FAILED;
	
	return cpuIsStoppedAndWhy(cpu);
}

bool cpuSetWatchpoint(struct Cpu* cpu, uint32_t idx, uint32_t addr, uint32_t size, uint32_t type)
{
	uint32_t vals[3] = {addr, size, type};
	
	return cpuMemWrite(cpu, 0xE0001020 + idx * 0x10, 3, vals, true);
}

bool cpuSetCpsrTbitAndDisableInterrupts(struct Cpu *cpu)
{
	uint32_t regs[CPU_NUM_REGS_PER_SET], tBit = (1ul << 24), iBit = 1;
	
	if (!cpuRegsGet(cpu, SWD_COMMS_REG_SET_CTRL, regs))  {
		fprintf(stderr, "CHIP REG GET FAIL\n");
		return false;
	}
	
	if (!(regs[0] & tBit) || !(regs[3] & iBit)) {
	
		regs[0] |= tBit;		//set t bit
		regs[3] |= iBit;		//disable interrupts
		if (!cpuRegsSet(cpu, SWD_COMMS_REG_SET_CTRL, regs))  {
			fprintf(stderr, "CHIP REG SET FAIL\n");
			return false;
		}
	}
	
	return true;
}

int32_t cpuTraceLogRead(struct Cpu *cpu, uint32_t addr, void **bufP)
{
	uint8_t *buf = alloca(debuggerGetMaxXferSz(cpu->dbg));
	struct CommsPacket *pkt = (struct CommsPacket*)buf;
	uint32_t retLen;
	
	if (!bufP)		//we simply need buf ptr
		return -1;
	
	pkt->cmd = SWD_TRACE_LOG_READ;
	*(uint32_t*)pkt->payload = addr;
	
	retLen = debuggerDoOneDevCmd(cpu->dbg, pkt, sizeof(uint32_t), pkt);
	if (retLen == PACKET_RX_FAIL) {
		fprintf(stderr, "TRACE CMD ERR\n");
		return -2;
	}
	
	*bufP = malloc(retLen);
	if (!*bufP)
		return -3;
	
	memcpy(*bufP, pkt->payload, retLen);
	return retLen;
}

bool cpuHasFpu(const struct Cpu* cpu)
{
	return cpu->cores[cpu->curCore].haveFpu;
}

bool cpuIsV7(const struct Cpu* cpu)
{
	return cpu->cores[cpu->curCore].isV7;
}

bool cpuSelectCore(struct Cpu* cpu, uint16_t coreId)
{
	uint8_t *buf = alloca(debuggerGetMaxXferSz(cpu->dbg));
	struct CommsPacket *pkt = (struct CommsPacket*)buf;
	uint32_t i;
	
	for (i = 0; i < cpu->numCores; i++) {
		if (cpu->cores[i].identifier == coreId)
			break;
	}
	
	if (i == cpu->numCores) {		//no such core?
		fprintf(stderr, "INVALID CORE ID\n");
		return false;
	}
	
	pkt->cmd = SWD_COMMS_CMD_SELECT_CPU;
	*(uint16_t*)pkt->payload = coreId;
	
	if (debuggerDoOneDevCmd(cpu->dbg, pkt, sizeof(uint16_t), pkt) != 1) {
		fprintf(stderr, "CPU_SW CMD ERR\n");
		return false;
	}
	
	if (!pkt->payload[0])
		return false;
	
	cpu->curCore = i;
	return true;
}

bool cpuIsDebuggerHwSlow(const struct Cpu *cpu)
{
	return debuggerIsSlowHw(cpu->dbg);
}
