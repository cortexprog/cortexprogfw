#include "../swdCommsPacket.h"
#include "../cortex.h"
#include "scriptfiles.h"
#include <string.h>
#include "cpu.h"



#define CPU_DISP_NAME_UNKNOWN	"Unknown MCU"


#define ARM_REG_ADDR_CPUID		0xE000ED00UL


static hid_device *mHidDev;
static bool mIsV7 = false, mHaveFpu = false;
static DoOneSwdDevCmdF mOneCmdF = NULL;
static uint32_t mMaxPacketSz;



bool cpuIsV7(void)
{
	return mIsV7;
}

bool cpuHasFpu(void)
{
	return mHaveFpu;
}

static uint32_t oneCmd(const struct CommsPacket *pktTx, uint32_t txPayloadLen, struct CommsPacket *pktRx)
{
	return mOneCmdF(mHidDev, pktTx, txPayloadLen, pktRx);
}

static uint8_t simpleCmd(uint8_t cmd)
{
	uint8_t buf[mMaxPacketSz];
	struct CommsPacket *pkt = (struct CommsPacket*)buf;
	
	pkt->cmd = cmd;

	if (1 != oneCmd(pkt, 0, pkt)) {
		fprintf(stderr, "SIMPLE CMD ERR\n");
		return 0xFF;
	}
	
	return pkt->payload[0];
}

uint8_t cpuStop(void)
{
	return simpleCmd(SWD_COMMS_CMD_STOP);
}

bool cpuReset(void)
{
	return !!simpleCmd(SWD_COMMS_CMD_RESET);
}

bool cpuGo(void)
{
	return !!simpleCmd(SWD_COMMS_CMD_GO);
}

uint8_t cpuStep(void)
{
	return simpleCmd(SWD_COMMS_CMD_SINGLE_STEP);
}

uint8_t cpuIsStoppedAndWhy(void)
{
	return simpleCmd(SWD_COMMS_CMD_IS_STOPPED);
}

static uint32_t cpuConvertPacketSzToNumWordsXferred(uint32_t packetSz)
{
	return (packetSz - sizeof(struct CommsPacket) - sizeof(struct SwdCommsMemPacket)) / sizeof(uint32_t);
}

uint32_t cpuGetOptimalReadNumWords(void)
{
	return cpuConvertPacketSzToNumWordsXferred(mMaxPacketSz);
}

uint32_t cpuGetOptimalWriteNumWords(void)
{
	return cpuConvertPacketSzToNumWordsXferred(mMaxPacketSz);
}

//cannot do both at once!
static bool cpuMemReadWrite(uint32_t base, uint32_t numWords, uint32_t *dst, const uint32_t *src, bool withAck, bool silent)
{
	uint32_t xferWords = dst ? cpuGetOptimalWriteNumWords() : cpuGetOptimalReadNumWords(), i;
	uint8_t bufIn[mMaxPacketSz], bufOut[mMaxPacketSz];
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
			
			payloadSzOut = oneCmd(pktOut, payloadSzIn, withAckNow ? pktIn : NULL);
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

bool cpuMemReadEx(uint32_t base, uint32_t numWords, uint32_t *dst, bool silent/* do not write any errors to stdout*/)
{
	return cpuMemReadWrite(base, numWords, dst, NULL, true, silent);
}

bool cpuMemRead(uint32_t base, uint32_t numWords, uint32_t *dst)
{
	return cpuMemReadEx(base, numWords, dst, false);
}

bool cpuMemWrite(uint32_t base, uint32_t numWords, const uint32_t *src, bool withAck)
{
	return cpuMemReadWrite(base, numWords, NULL, src, withAck, false);
}

//cannot do both at once!
static bool cpuRegsGetSet(uint8_t regSet, uint32_t *dst, const uint32_t *src)
{
	uint8_t buf[mMaxPacketSz];
	struct CommsPacket *pkt = (struct CommsPacket*)buf;
	struct SwdCommsRegsPacket *rpp = (struct SwdCommsRegsPacket*)pkt->payload;
	int8_t payloadSzIn = sizeof(struct SwdCommsRegsPacket), payloadSzOut;
	int i;
	
	
	pkt->cmd = dst ? SWD_COMMS_CMD_REGS_READ : SWD_COMMS_CMD_REGS_WRITE;
	rpp->regSet = regSet;
	
	if (src) {
		for (i = 0; i < 16; i++)
			rpp->regs[i] = src[i];
		payloadSzIn += sizeof(uint32_t) * NUM_REGS;
	}

	payloadSzOut = oneCmd(pkt, payloadSzIn, pkt);
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

bool cpuRegsGet(uint8_t regSet, uint32_t *dst)
{
	return cpuRegsGetSet(regSet, dst, NULL);
}

bool cpuRegsSet(uint8_t regSet, const uint32_t *src)
{
	return cpuRegsGetSet(regSet, NULL, src);
}

static bool doCpuid(uint32_t targetid, uint32_t romTableBase, uint32_t *loadSzP, uint32_t *loadAddrP, uint32_t *stageAddrP, char **nameP, char** scptFileName, FILE **scptFileHandle)
{
	const struct CpuidMap *mapEnt;
	uint32_t i, peripheralIds[8];
	uint64_t periphId = 0;
	
	fprintf(stderr, "base=0x%08x\n", romTableBase);

	if (!cpuMemRead(romTableBase + 0xfd0, 8, peripheralIds)) {
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
		uint32_t i, model = periphId & 0xfff;
		uint8_t cont = (periphId >> 32) & 0x0f;
		uint8_t code = (periphId >> 12) & 0x7f;
		
		fprintf(stderr, "JEDEC = {%d, 0x%02X, 0x%03x}\n", cont, code, model);
		
		//we could do a binary search, but why?
		
		for (i = 0; i < sizeof(jedecs) / sizeof(*jedecs); i++) {
			if (jedecs[i].cont == cont && jedecs[i].code == code) {
				fprintf(stderr, "Chip is by '%s', model 0x%03X\n", jedecs[i].name, model);
				break;
			}
		}
	}
	if (targetid) {
		fprintf(stderr, "DAPv2 TARGETID: 0x%08X\n", targetid);
	}
	fprintf(stderr, "CPUID = {");
	for (i = 0; i < 8; i++) {
		fprintf(stderr, "0x%08x, ", peripheralIds[i]);
	}
	fprintf(stderr, "}\n");
	
	return scriptfileFind(targetid, peripheralIds, loadSzP, loadAddrP, stageAddrP, nameP, scptFileName, scptFileHandle);
}

bool cpuIdentify(uint32_t *loadSzP, uint32_t *loadAddrP, uint32_t *stageAddrP, char **nameP, char** scptFileName, FILE **scptFileHandle)
{
	uint8_t buf[mMaxPacketSz];
	struct CommsPacket *pkt = (struct CommsPacket*)buf;
	struct SwdCommsAttachRespPacket *attachRspPkt = (struct SwdCommsAttachRespPacket*)pkt->payload;
	uint32_t romTableBase;
	char *cpuDispName = NULL;
	bool ret;
	
	pkt->cmd = SWD_COMMS_CMD_ATTACH;
	if (sizeof(struct SwdCommsAttachRespPacket) != oneCmd(pkt, 0, pkt)) {
		fprintf(stderr, "ATTACH CMD ERR\n");
		return NULL;
	}
	switch (attachRspPkt->cortexType) {
		case 0:;
			const char **mErrors;
			static const char *mErrorsSwd[16] = {
					"SWD",
					"IDCODE read failed",
					"DAP is V0",
					"REG read failed",
					"REG write failed",
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
			switch (attachRspPkt->error & ERR_FLAG_TYPE_MASK) {
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
				fprintf(stderr, "0x%02X", attachRspPkt->error);
			else {
				const uint8_t errNum = attachRspPkt->error &~ ERR_FLAG_TYPE_MASK;
				const char *errStr = mErrors[errNum];
				
				fprintf(stderr, "'%s', ", mErrors[0]);
				if (errStr)
					fprintf(stderr, "'%s'", errStr);
				else
					fprintf(stderr, "0x%02X", errNum);
			}

			fprintf(stderr, "}\n");
			return NULL;
		
		case CPUID_PART_M0:
			fprintf(stderr, "Cortex-M0 found\n");
			break;
		
		case CPUID_PART_M1:
			fprintf(stderr, "Cortex-M1 found\n");
			break;
		
		case CPUID_PART_M3:
			fprintf(stderr, "Cortex-M3 found\n");
			mIsV7 = true;
			break;
		
		case CPUID_PART_M4:
			mHaveFpu = !!(attachRspPkt->flags & SWD_FLAG_HAS_FPU);
			fprintf(stderr, "Cortex-M4%s found\n", mHaveFpu ? "F" : "");
			mIsV7 = true;
			break;
		
		case CPUID_PART_M7:
			mHaveFpu = !!(attachRspPkt->flags & SWD_FLAG_HAS_FPU);
			fprintf(stderr, "Cortex-M7%s found\n", mHaveFpu ? "F" : "");
			mIsV7 = true;
			break;
		
		case CPUID_PART_M0p:
			fprintf(stderr, "Cortex-M0+ found\n");
			break;
		
		case CPUID_PART_UNREADABLE:
			fprintf(stderr, "CPUID unreadable. Proceeding optimistically\n");
			break;
		
		default:
			fprintf(stderr, "Unknown cortex (code 0x%04X) found. Proceeding optimistically\n", attachRspPkt->cortexType);
			break;
	}
	
	if (scptFileName)
		*scptFileName = NULL;
		
	ret = doCpuid(attachRspPkt->targetid, attachRspPkt->romTableBase, loadSzP, loadAddrP, stageAddrP, &cpuDispName, scptFileName, scptFileHandle);
	
	if (ret)
		fprintf(stderr, "CPU ID found -> '%s'\n", cpuDispName);
	else {
		fprintf(stderr, "CPU ID not found\n");
		cpuDispName = strdup("Unknown MCU");
	}

	if (nameP)
		*nameP = cpuDispName;

	return ret;
}

uint32_t cpuTraceLogRead(uint32_t addr, uint8_t *dst)
{
	uint8_t buf[mMaxPacketSz];
	struct CommsPacket *pkt = (struct CommsPacket*)buf;
	uint32_t retLen;
	
	pkt->cmd = SWD_TRACE_LOG_READ;
	*(uint32_t*)pkt->payload = addr;
	
	retLen = oneCmd(pkt, sizeof(addr), pkt);
	
	if (retLen == PACKET_RX_FAIL || retLen > mMaxPacketSz)
		return 0;
	
	memcpy(dst, pkt->payload, retLen);
	
	return retLen;
}

bool cpuInit(hid_device *dev, DoOneSwdDevCmdF cmdF, uint32_t maxXferBytes)
{
	mHidDev = dev;
	mIsV7 = false;
	mHaveFpu = false;
	mOneCmdF = cmdF;
	mMaxPacketSz = maxXferBytes;
	
	return true;
}

static uint8_t swdLlWireBusReadWrite(uint8_t cmd, uint8_t ap, uint8_t a23, uint32_t *valP)
{
	uint8_t ret, buf[mMaxPacketSz];
	struct CommsPacket *pkt = (struct CommsPacket*)buf;
	struct SwdCommsWireBusPacket *rpp = (struct SwdCommsWireBusPacket*)pkt->payload;
	
	memset(buf, 0, mMaxPacketSz);
	
	pkt->cmd = cmd;
	rpp->ap = ap;
	rpp->a23 = a23;
	rpp->val = *valP;
	
	ret = oneCmd(pkt, sizeof(struct SwdCommsWireBusPacket), pkt);
	if (ret != sizeof(struct SwdCommsWireBusPacket))
		return PACKET_RX_FAIL;
	
	*valP = rpp->val;
	
	return rpp->returnVal;
}

uint8_t swdLlWireBusRead(uint8_t ap, uint8_t a23, uint32_t *valP)
{
	return swdLlWireBusReadWrite(SWD_COMMS_SWD_WIRE_BUS_R, ap, a23, valP);
}

uint8_t swdLlWireBusWrite(uint8_t ap, uint8_t a23, uint32_t val)
{
	return swdLlWireBusReadWrite(SWD_COMMS_SWD_WIRE_BUS_W, ap, a23, &val);
}

bool cpuPwrSet(bool on)
{
	uint8_t buf[mMaxPacketSz];
	struct CommsPacket *pkt = (struct CommsPacket*)buf;
	
	pkt->cmd = SWD_POWER_CTRL;
	pkt->payload[0] = on ? 1 : 0;

	if (1 != oneCmd(pkt, 1, pkt)) {
		fprintf(stderr, "PWR CMD ERR\n");
		return false;
	}
	
	return !!pkt->payload[0];
}

