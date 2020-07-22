#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "scriptfiles.h"
#include "script.h"
#include "utilOp.h"
#include "memio.h"
#include "cpu.h"

#define SCRIPT_TIMEOUT_ERASE_ALL	90000	//90 sec timeout for erase all
#define SCRIPT_TIMEOUT_ERASE_BLOCK	5000	//5 sec timeout for erase block
#define SCRIPT_TIMEOUT_WRITE_BLOCK	2000	//2 sec timeout for write block
#define SCRIPT_TIMEOUT_INIT			500		//0.5 sec timeout for each init step
#define SCRIPT_TIMEOUT_CPUID		333		//0.333 sec timeout for cpuid verification

#define SCRIPT_CPUID_KEY			0xACEFACE5UL

struct FlashInfoRaw {
	uint32_t numPieces;
	struct FlashInfoPieceRaw {
		uint32_t base;
		uint8_t eraseSz;
		uint8_t writeSz;
		uint16_t numIdentical;			//zero means 65536
	} pieces[];
};

struct FlashNamesRaw {
	uint32_t numPieces;
	struct FlashNamePieceRaw {
		uint32_t base;
		uint32_t nameStrAddr;
	} pieces[];
};

struct FlashInfo {
	uint32_t numPieces;
	struct FlashInfoPiece {
		uint32_t base;
		uint32_t size;
	} pieces[];
};

struct FlashNamedInfo {
	uint32_t numPieces;
	struct FlashNamedInfoPiece {
		uint32_t base;
		uint32_t size;
		char *name;
	} pieces[];
};

struct Script {
	uint32_t scriptLoadAddr;
	uint32_t flashStagingAddr;
	uint32_t opHaveFlags;

	struct FlashInfo* fiWrite;			//write-size blocks
	struct FlashInfo* fiErase;			//erase-size blocks
	struct FlashNamedInfo* fiContig;	//contiguous blocks
	struct Cpu *cpu;
};

struct ScriptFileHeader {
	//syscall
	uint32_t syscallOp;				//two instrs that are used as a syscall by script (we set a watchpoint there and script can call it to syscall to us)
	
	//ops
	uint32_t initStage1;
	uint32_t initStage2;
	uint32_t initStage3;
	uint32_t allErase;				//void allErase(void)
	uint32_t pageErase;				//void pageErase(uint32_t addr);
	uint32_t pageWrite;				//void pageWrite(uint32_t addr);
	uint32_t cpuid;					//return (in r0) nonzero if script supports this chip, 0 else, must be idempotent. if this entrypoint is missing, support is assumed
};

#define SCRIPTFILEOFST(_nm)		offsetof(struct ScriptFileHeader, _nm)




static bool scriptRun(struct Script *scpt, uint32_t ofst, uint32_t param1, uint32_t param2, uint32_t param3, uint32_t param4, bool allowEmpty, uint32_t *retVal0P, uint32_t *retVal1P, uint64_t timeout);



struct Script* scriptLoad(struct Cpu* cpu, bool verbose)
{
	const struct PotentialScriptfile *scptOpt;
	uint32_t tmp = 0, tmp2;
	struct Script* scpt;
	
	for (scptOpt = cpuGetScriptfileOptions(cpu); scptOpt; scptOpt = scptOpt->next) {
		
		struct Script tmpScpt = {0,};					//for running init script we need a skeleton script struct - make one
		tmpScpt.scriptLoadAddr = scptOpt->loadAddr;
		tmpScpt.flashStagingAddr = scptOpt->stageAddr;
		tmpScpt.cpu = cpu;
		
		//verify file is opened
		if (!scptOpt->scriptfile)
			continue;

		if (verbose)
			fprintf(stderr, " trying script file '%s' (%u bytes to 0x%08X)\n", scptOpt->scriptfilePath, scptOpt->loadSz, scptOpt->loadAddr);
		
		//verify the file can be loaded, that the load area is writeable and at least a word of stage area is too (quick safe check)
		tmp = 0;	//write a predictable value
		if (!cpuMemWrite(cpu, scptOpt->loadAddr, 1, &tmp, true)) {
			if (verbose)
				fprintf(stderr, "  write at load addr failed - skipping\n");
			continue;
		}
		if (!cpuMemWrite(cpu, (scptOpt->loadAddr + scptOpt->loadSz - 1) &~ 3, 1, &tmp, true)) {
			if (verbose)
				fprintf(stderr, "  write at load addr + sz failed - skipping\n");
			continue;
		}
		if (!cpuMemWrite(cpu, scptOpt->stageAddr, 1, &tmp, true)) {
			if (verbose)
				fprintf(stderr, "  write at stage addr + sz failed - skipping\n");
			continue;
		}

		if (cpuStop(cpu) == CPU_STAT_CODE_FAILED) {
			fprintf(stderr, "  CHIP RESET & STOP FAIL\n");
			return NULL;
		}
	
		if (fseek(scptOpt->scriptfile, 0, SEEK_SET)) {
			fprintf(stderr, "  fseek FAIL\n");
			return NULL;
		}
		
		if (!cpuSetCpsrTbitAndDisableInterrupts(cpu)) {
			fprintf(stderr, "  SR preparation FAIL 1\n");
			return NULL;
		}
		
		if (!memioWriteFromFile(cpu, scptOpt->loadAddr, scptOpt->loadSz, scptOpt->scriptfile, true, false, false)) {
			if (verbose)
				fprintf(stderr, "  upload failed - skipping\n");
			continue;
		}
		
		if (verbose)
			fprintf(stderr, "  script uploaded\n");
	
		if (!cpuSetCpsrTbitAndDisableInterrupts(cpu)) {
			fprintf(stderr, "  SR preparation FAIL 2\n");
			return NULL;
		}
		
		//set script hypercall watchpoint
		if (!cpuSetWatchpoint(cpu, 0, scptOpt->loadAddr + SCRIPTFILEOFST(syscallOp), 2, CPU_WPT_TYPE_PC))	{ //for syscalls to debugger
			fprintf(stderr, "  script BP preparation FAIL\n");
			return NULL;
		}
		
		//run cpuid entrypt
		tmp = 1;					//in case no entrypt, we must treat that as a success - prepare for that
		tmp2 = SCRIPT_CPUID_KEY;
		if (!scriptRun(&tmpScpt, SCRIPTFILEOFST(cpuid), 0, 0, 0, 0, true, &tmp, &tmp2, SCRIPT_TIMEOUT_CPUID) || tmp2 != SCRIPT_CPUID_KEY) {
			if (verbose)
				fprintf(stderr, "  cpuid step failed - skipping\n");
			continue;
		}
		
		//see if it was a match
		if (!tmp) {
			if (verbose)
				fprintf(stderr, "  cpuid rejected - skipping\n");
			continue;
		}
		
		if (verbose) {
			fprintf(stderr, " CPU identified as '%s', will use scriptfile %s\n", scptOpt->cpuName, scptOpt->scriptfileBaseName);
			fprintf(stderr, " loaded at 0x%08X. Staging area will be 0x%08X\n", scptOpt->loadAddr, scptOpt->stageAddr);
		}
		
		scpt = (struct Script*)calloc(1, sizeof(struct Script));
		
		if (!scpt)
			return NULL;

		*scpt = tmpScpt;
		
		return scpt;
	}
	
	return NULL;
}

void scriptFree(struct Script *scpt)
{
	uint32_t i;
	
	if (scpt->fiContig) {
		for (i = 0; i < scpt->fiContig->numPieces; i++)
			free(scpt->fiContig->pieces[i].name);
	}
	
	free(scpt->fiContig);
	free(scpt->fiWrite);
	free(scpt->fiErase);
	free(scpt);
}

uint32_t scriptGetFlashWriteStageAreaAddr(const struct Script *scpt)
{
	return scpt->flashStagingAddr;
}

uint32_t scriptGetSupportedOps(const struct Script *scpt)
{
	return scpt->opHaveFlags;
}

bool scriptGetNthContiguousFlashAreaInfo(const struct Script *scpt, uint32_t n, uint32_t *areaStartP, uint32_t *areaLenP, const char **nameP)
{
	if (n >= scpt->fiContig->numPieces)
		return false;
	
	if (areaStartP)
		*areaStartP = scpt->fiContig->pieces[n].base;
	
	if (areaLenP)
		*areaLenP = scpt->fiContig->pieces[n].size;
	
	if (nameP)
		*nameP = scpt->fiContig->pieces[n].name;
	
	return true;
}

const char *scriptGetAreaName(const struct Script *scpt, uint32_t addr)
{
	uint32_t i;
	
	for (i = 0; i < scpt->fiContig->numPieces; i++) {
		
		if (addr >= scpt->fiContig->pieces[i].base && addr - scpt->fiContig->pieces[i].base < scpt->fiContig->pieces[i].size)
			return scpt->fiContig->pieces[i].name;
	}
	
	return NULL;
}

uint32_t scriptGetFlashBlockSize(const struct Script *scpt, uint32_t base, bool forWrite)
{
	const struct FlashInfo *fi = forWrite ? scpt->fiWrite : scpt->fiErase;
	uint32_t i;
	
	for (i = 0; i < fi->numPieces; i++) {
		if (fi->pieces[i].base == base)
			return fi->pieces[i].size;
	}
	
	return 0;
}

bool scriptIsValidFlashRange(const struct Script *scpt, uint32_t start, uint32_t len, bool forWrites, uint32_t *suggestedLenP)
{
	uint64_t end = (uint64_t)start + len;
	uint32_t lenAccountedFor = 0;
	
	if (!len)									//all zero-length things are valid
		return true;
	
	//check start
	if (!scriptGetFlashBlockSize(scpt, start, forWrites))
		goto out_start;
	
	//if we got here, we found the start index, now check on the end
	//this is slower than optimal, but simpler to read
	while (len > lenAccountedFor) {
		
		uint32_t blkLen = scriptGetFlashBlockSize(scpt, start + lenAccountedFor, forWrites);
		if (!blkLen)
			goto out_end_nonexistent;
		
		lenAccountedFor += blkLen;
	}
	
	if (len == lenAccountedFor)		//perfect match
		return true;
	
	//if we got here, we have enough flash to write,  end address is not an end of a block.
	//fallthrough

	if (suggestedLenP)
		*suggestedLenP = lenAccountedFor;
	else
		fprintf(stderr, "ERROR: address 0x%08llX not an end of a known flash block\n", (unsigned long long)end);
	return false;

out_start:
	if (suggestedLenP)
		*suggestedLenP = 0;
	fprintf(stderr, "ERROR: address 0x%08X is not a start of a known flash block\n", start);
	return false;

out_end_nonexistent:
	if (suggestedLenP)
		*suggestedLenP = 0;
	fprintf(stderr, "ERROR: end address 0x%08llX not in a known flash block\n", (unsigned long long)end);
	return false;
}

static void scriptPrintSizeNum(uint32_t val, int minWidth)
{
	char buf[64];
	
	if (val < 1024 || ((val >> 10) << 10) != val)
		minWidth -= sprintf(buf, "%uB", val);
	else if (val < 1024 * 1024 || ((val >> 20) << 20) != val)
		minWidth -= sprintf(buf, "%uKB", val >> 10);
	else
		minWidth -= sprintf(buf, "%uMB", val >> 20);

	while (minWidth-- > 0)
		fprintf(stderr, " ");
	fprintf(stderr, "%s", buf);
}

static void scriptSortChunksMergeSort(struct FlashInfoPiece *data, struct FlashInfoPiece *tmp, uint32_t numPieces)
{
	uint32_t sz1, sz2, pos1, pos2, pos;
	
	//end case
	if (numPieces <= 1)
		return;
	
	//calculate
	sz1 = numPieces / 2;
	sz2 = numPieces - sz1;
	
	//recurse
	scriptSortChunksMergeSort(data, tmp, sz1);
	scriptSortChunksMergeSort(data + sz1, tmp + sz1, sz2);
	
	//merge into tmp
	pos1 = 0;
	pos2 = sz1;
	pos = 0;
	while(pos1 < sz1 && pos2 < numPieces) {
		
		if (data[pos1].base < data[pos2].base)
			tmp[pos++] = data[pos1++];
		else
			tmp[pos++] = data[pos2++];
	}
	
	while(pos1 < sz1)
		tmp[pos++] = data[pos1++];
	while(pos2 < numPieces)
		tmp[pos++] = data[pos2++];
	
	//copy back into data
	memcpy(data, tmp, sizeof(struct FlashInfoPiece) * numPieces);
}

static void scriptSortChunks(struct FlashInfo *fi)
{
	struct FlashInfoPiece *tmp = (struct FlashInfoPiece*)malloc(sizeof(struct FlashInfoPiece) * fi->numPieces);
	scriptSortChunksMergeSort(fi->pieces, tmp, fi->numPieces);
	free(tmp);
}

static bool scriptBlockSizeDecode(uint32_t encodedVal, uint32_t *decodedValP, uint32_t idxForErr, const char *nameForErr)
{
	uint32_t val;
	
	if (encodedVal < 32)
		val = 1 << encodedVal;
	else if ((encodedVal & 0xE0) > 0x80 && (encodedVal & 0x1F) < 30)
		val = ((encodedVal & 0xE0) >> 5) << (encodedVal & 0x1F);
	else {
		fprintf(stderr, "FLASH INFO ERROR: pieces[%u] %s size(0x%02X) not decodable\n", idxForErr, nameForErr, encodedVal);
		return false;
	}
	
	if (decodedValP)
		*decodedValP = val;
	
	return true;
}

static struct FlashInfo* scriptExpandAndCheckFlashInfoByType(struct Script *scpt, const struct FlashInfoRaw *rfi, bool forWrite, bool *blockSizesEverDifferedP)
{
	struct FlashInfo *fi = calloc(1, sizeof(struct FlashInfo));
	uint32_t i, j, blkSz, blkSzErz, blkSzWri;
	uint64_t end, totalSz = 0;
	
	
	if (!fi)
		return NULL;
	
	//create pieces
	for (i = 0; i < rfi->numPieces; i++) {
		
		//get number of erase blocks
		uint32_t curBase = rfi->pieces[i].base, numIdentical = rfi->pieces[i].numIdentical ? rfi->pieces[i].numIdentical : 0x10000;
		
		//find block sizes
		if (!scriptBlockSizeDecode(rfi->pieces[i].writeSz, &blkSzWri, i, "write"))
			goto out_err;
		if (!scriptBlockSizeDecode(rfi->pieces[i].eraseSz, &blkSzErz, i, "erase"))
			goto out_err;
		blkSz = forWrite ? blkSzWri : blkSzErz;
		
		//sanity: verify some things
		if (blkSzWri > blkSzErz) {
			
			fprintf(stderr, "FLASH INFO ERROR: pieces[%u] erase size(0x%08X) smaller than write size(0x%08X)\n", i, blkSzErz, blkSzWri);
			goto out_err;
		}
		
		if (blkSzWri != blkSzErz)
			*blockSizesEverDifferedP = true;
		
		//multiply accordingly
		numIdentical *= blkSzErz / blkSz;
		
		//keep track of total sz for sanity checks
		totalSz += (uint64_t)numIdentical * blkSz;
		
		//create chunks
		for (j = 0; j < numIdentical; j++) {
			
			struct FlashInfo* tmp = realloc(fi, sizeof(struct FlashInfo) + sizeof(struct FlashInfoPiece) * (fi->numPieces + 1));
			if (!tmp)
				goto out_err;

			fi = tmp;
			
			fi->pieces[fi->numPieces].base = curBase;
			fi->pieces[fi->numPieces].size = blkSz;
			curBase += blkSz;
			fi->numPieces++;
		}
	}
	
	//more sanity checks
	if (!fi->numPieces) {
		fprintf (stderr, "FLASH INFO ERROR: no flash found\n");
		goto out_err;
	}
	
	if (totalSz > (1ULL << 32)) {
		fprintf(stderr, "FLASH INFO ERROR: total size over 4GB: 0x%016llx\n", (unsigned long long)totalSz);
		goto out_err;
	}

	//sort them
	scriptSortChunks(fi);
	
	//verify no overlap (assumes chunks are sorted)
	for (i = 0; i < fi->numPieces; i++) {
		
		end = fi->pieces[i].base + fi->pieces[i].size;
		
		for (j = i + 1; j < fi->numPieces; j++) {
			
			if (fi->pieces[j].base < end) {
				
				fprintf(stderr, "FLASH INFO ERROR: pieces overlap\n");
				goto out_err;
			}
			
			if (fi->pieces[j].base > end)
				break;
		}
	}

	return fi;

out_err:
	free(fi);
	return NULL;
}

static char* scriptReadString(struct Script *scpt, uint32_t addr)
{
	uint32_t curLen = 0, maxLen = 64;
	char *ret = NULL;
	
	while(curLen <= maxLen) {
		char *t = realloc(ret, curLen + 1);
		
		if (!t)
			break;

		ret = t;
		if (1 != memioReadToBuffer(scpt->cpu, addr + curLen, 1, &ret[curLen]))
			break;

		if (!ret[curLen])
			goto out;
			
		curLen++;
	}

	free(ret);
	ret = NULL;

out:

	return ret;
}

static struct FlashNamedInfo* scriptExpandAndCheckFlashRegions(struct Script *scpt, const struct FlashInfo* fiErz, const struct FlashNamesRaw *rfn)	//input "struct FlashInfo" must be sorted and sane. Caller makes sure of it
{
	struct FlashNamedInfo *tmp, *fi = calloc(1, sizeof(struct FlashNamedInfo));
	uint32_t i, j, k, numAreas = 0;
	
	for (i = 0; i < fiErz->numPieces;) {
		
		for (j = i + 1; j < fiErz->numPieces && fiErz->pieces[j].base == fiErz->pieces[j - 1].base + fiErz->pieces[j - 1].size; j++);
	
		tmp = realloc(fi, sizeof(struct FlashNamedInfo) + sizeof(struct FlashNamedInfoPiece) * (fi->numPieces + 1));
		if (!tmp) {
			free(fi);
			return NULL;
		}
		fi = tmp;
		
		fi->pieces[fi->numPieces].base = fiErz->pieces[i].base;
		fi->pieces[fi->numPieces].size = fiErz->pieces[j - 1].base + fiErz->pieces[j - 1].size - fiErz->pieces[i].base;
		fi->pieces[fi->numPieces].name = NULL;
		
		//get name if names struct has that address
		if (rfn) {
			for (k = 0; k < rfn->numPieces; k++) {
				
				if (rfn->pieces[k].base == fi->pieces[fi->numPieces].base) {
					
					fi->pieces[fi->numPieces].name = scriptReadString(scpt, rfn->pieces[k].nameStrAddr);
					break;
				}
			}
		}
		
		fi->numPieces++;
		i = j;
	}
	
	return fi;
}

static void scriptShowVerboseFlashInfo(struct Script *scpt, const struct FlashInfo* fi, bool forWrite, bool blockSizesEverDiffered)
{
	uint32_t i, j, totalSz = 0;

	for (i = 0; i < fi->numPieces; i++)
		totalSz += fi->pieces[i].size;

	//count areas, and maybe show them
	if (!forWrite || blockSizesEverDiffered) {	//summarize only if calculating for "Erase" or if "erase" and "write" block sizes differ
		uint32_t numAreas = 0;
		
		for (i = 0; i < fi->numPieces;) {
			
			for (j = i + 1; j < fi->numPieces && fi->pieces[j].base == fi->pieces[j - 1].base + fi->pieces[j - 1].size; j++);
			numAreas++;
			i = j;
		}

		//show info
		fprintf(stderr, "Chip has %u flash area%s %s(", numAreas, numAreas == 1 ? "" : "s", blockSizesEverDiffered ? (forWrite ? "for write " : "for erase ") : "");
		scriptPrintSizeNum(totalSz, 0);
		fprintf(stderr, " total):\n");
	
	
		//summarise part 2: show them
		for (i = 0; i < fi->numPieces;) {
			
			const char *nm;
			
			for (j = i + 1; j < fi->numPieces && fi->pieces[j].base == fi->pieces[j - 1].base + fi->pieces[j - 1].size && fi->pieces[j].size == fi->pieces[i].size; j++);
			
			nm = scriptGetAreaName(scpt, fi->pieces[i].base);
			
			scriptPrintSizeNum(fi->pieces[j - 1].base + fi->pieces[j - 1].size - fi->pieces[i].base, 8);
			fprintf (stderr, " at 0x%08X (", fi->pieces[i].base);
			fprintf(stderr, "%4u x ", j - i);
			scriptPrintSizeNum(fi->pieces[i].size, 0);
			fprintf(stderr, ")");
			if (nm)
				fprintf(stderr, " /* %-32s */", nm);
			fprintf(stderr, "\n");
			i = j;
		}
	}
}

static bool scriptExpandAndCheckFlashInfo(struct Script *scpt, const struct FlashInfoRaw *rfi, const struct FlashNamesRaw *rfn, bool verbose)
{
	bool blockSizesEverDiffered = false;
	
	scpt->fiErase = scriptExpandAndCheckFlashInfoByType(scpt, rfi, false, &blockSizesEverDiffered);
	scpt->fiWrite = scriptExpandAndCheckFlashInfoByType(scpt, rfi, true, &blockSizesEverDiffered);
	if (scpt->fiErase)
		scpt->fiContig = scriptExpandAndCheckFlashRegions(scpt, scpt->fiErase, rfn);
	
	if (!scpt->fiErase || !scpt->fiWrite || !scpt->fiContig)
		return false;
	
	//show info if verbose
	if (verbose) {
		
		scriptShowVerboseFlashInfo(scpt, scpt->fiErase, false, blockSizesEverDiffered);
		if (blockSizesEverDiffered)
			scriptShowVerboseFlashInfo(scpt, scpt->fiWrite, true, blockSizesEverDiffered);
	}
	
	return true;
}

static bool scriptRun(struct Script *scpt, uint32_t ofst, uint32_t param1, uint32_t param2, uint32_t param3, uint32_t param4, bool allowEmpty, uint32_t *retVal0P, uint32_t *retVal1P, uint64_t timeout)	//timeout of 0 means never times out, else num of msec to wait maximum
{
	uint32_t regs[CPU_NUM_REGS_PER_SET], checkval, addr = scpt->scriptLoadAddr + ofst;
	uint64_t startTime;
	
	if (cpuIsDebuggerHwSlow(scpt->cpu))
		timeout *= 2;
	
	if(!cpuMemRead(scpt->cpu, addr, 1, &checkval)) {
		fprintf(stderr, "RUN: RM err\n");
		return false;
	}
	
	if (!checkval) {
		
		if (allowEmpty)
			return true;
		
		fprintf(stderr, "script offset 0x%08X empty\n", ofst);
		return false;
	}
	
	cpuStep(scpt->cpu);		//do not ask...
	cpuStop(scpt->cpu);		//still do not ask (if we were in LOCKUP, step can act as continue on some chips)
	
	if (!cpuSetCpsrTbitAndDisableInterrupts(scpt->cpu))
		return false;
	
	if(!cpuRegsGet(scpt->cpu, SWD_COMMS_REG_SET_BASE, regs)) {
		fprintf(stderr, "RUN: RG err\n");
		return false;
	}
	
	regs[0] = param1;
	regs[1] = param2;
	regs[2] = param3;
	regs[3] = param4;
	regs[15] = addr | 1;
	
	if(!cpuRegsSet(scpt->cpu, SWD_COMMS_REG_SET_BASE, regs)) {
		fprintf(stderr, "RUN: RS err\n");
		return false;
	}
	
	startTime = getTicks();
	while(1) {
		uint8_t sta;
		
		if (!cpuGo(scpt->cpu)) {
			fprintf(stderr, "RUN: GO err\n");
			return false;
		}
		
		
		while (CPU_STAT_CODE_FAILED == (sta = cpuIsStoppedAndWhy(scpt->cpu))) {
			
			if (timeout && getTicks() - startTime > timeout) { 
			
				uint32_t i;
				
				fprintf(stderr, "scriptRun (ofst=0x%x, param={0x%x 0x%x 0x%x 0x%x}) timed out\n", ofst, param1, param2, param3, param4);
				if(cpuStop(scpt->cpu) == CPU_STAT_CODE_FAILED || !cpuRegsGet(scpt->cpu, SWD_COMMS_REG_SET_BASE, regs)) {
					fprintf(stderr, "further error\n");
					return false;
				}
				
				for (i = 0; i < 16; i++)
					fprintf(stderr, "R%02d = 0x%08X\n", i, regs[i]);
				
				return false;
			}
		}
		
		if (sta & CPU_STAT_CODE_DWPT) {
			if(!cpuRegsGet(scpt->cpu, SWD_COMMS_REG_SET_BASE, regs)) {
				fprintf(stderr, "RUN: RG.s err\n");
				return false;
			}
			
			if ((regs[15] &~ 3) == scpt->scriptLoadAddr + SCRIPTFILEOFST(syscallOp)) {
				
	//			fprintf(stderr, "syscall %d\n", regs[0]);
				sta &=~ CPU_STAT_CODE_DWPT;
				
				switch (regs[0]) {
					case 0:	//set bpt/wpt
						cpuSetWatchpoint(scpt->cpu, 1, regs[1], regs[2], regs[3]);
						break;
					case 1:	//clear bpt
						cpuSetWatchpoint(scpt->cpu, 1, 0, 0, 0);
						break;
					case 2:	//check readability of an address
						regs[0] = cpuMemReadEx(scpt->cpu, regs[1], 1, &regs[1], true) ? 1 : 0;	//(#2, addr) -> (bool success, u32 val)
						break;
					default:
						fprintf(stderr, "\nunknown syscall %u\n\n", regs[0]);
						break;
				}
				regs[15] = regs[14];
				
				if(!cpuRegsSet(scpt->cpu, SWD_COMMS_REG_SET_BASE, regs)) {
					fprintf(stderr, "RUN: RS.s err\n");
					return false;
				}
				continue;
			}
			
			//we hit a watchpoint/breakpoint. R0 has no useful status. pretend it contained 1
			regs[0] = 1;
			goto out_no_reg_read;
		}
		break;
	}
	
	if(!cpuRegsGet(scpt->cpu, SWD_COMMS_REG_SET_BASE, regs)) {
		fprintf(stderr, "RUN: RG err 2\n");
		return false;
	}
	
out_no_reg_read:
	if (retVal0P)
		*retVal0P = regs[0];
	if (retVal1P)
		*retVal1P = regs[1];
	
	return true;
}

bool scriptEraseAll(struct Script *scpt)
{
	uint32_t retVal = 0;
	
	return scriptRun(scpt, SCRIPTFILEOFST(allErase), 0, 0, 0, 0, false, &retVal, NULL, SCRIPT_TIMEOUT_ERASE_ALL) && retVal;
}

bool scriptEraseBlock(struct Script *scpt, uint32_t base)
{
	uint32_t retVal = 0;
	
	return scriptRun(scpt, SCRIPTFILEOFST(pageErase), base, 0, 0, 0, false, &retVal, NULL, SCRIPT_TIMEOUT_ERASE_BLOCK) && retVal;
}

bool scriptWriteBlock(struct Script *scpt, uint32_t base)
{
	uint32_t retVal = 0;
	
	return scriptRun(scpt, SCRIPTFILEOFST(pageWrite), base, 0, 0, 0, false, &retVal, NULL, SCRIPT_TIMEOUT_WRITE_BLOCK) && retVal;
}

bool scriptInit(struct Script *scpt, bool verbose)
{
	uint32_t initRetVal0 = 0xFFFFFFFF, initRetVal1 = 0, i, bufsz;
	struct FlashNamesRaw *rfn = NULL, tmpNamesHdr;
	struct FlashInfoRaw *rfi, tmpInfoHdr;
	struct ScriptFileHeader hdr;
	bool ret;
	
	if (verbose)
		fprintf(stderr, "running stage 1 init...\n");
	if (!scriptRun(scpt, SCRIPTFILEOFST(initStage1), 0, 0, 0, 0, true, &initRetVal0, &initRetVal1, SCRIPT_TIMEOUT_INIT)) {
		fprintf(stderr, "STAGE 1 init failed\n");
		return false;
	}
	if (verbose)
		fprintf(stderr, "running stage 2 init...\n");
	if (!scriptRun(scpt, SCRIPTFILEOFST(initStage2), 0, 0, 0, 0, true, &initRetVal0, &initRetVal1, SCRIPT_TIMEOUT_INIT)) {
		fprintf(stderr, "STAGE 2 init failed\n");
		return false;
	}
	if (verbose)
		fprintf(stderr, "running stage 3 init...\n");
	if (!scriptRun(scpt, SCRIPTFILEOFST(initStage3), 0, 0, 0, 0, true, &initRetVal0, &initRetVal1, SCRIPT_TIMEOUT_INIT)) {
		fprintf(stderr, "STAGE 3 init failed\n");
		return false;
	}
	
	if (!cpuMemRead(scpt->cpu, scpt->scriptLoadAddr, sizeof(struct ScriptFileHeader) / sizeof(uint32_t), (uint32_t*)&hdr)) {
		fprintf(stderr, "NFO read fail\n");
		return false;
	}
	
	if (verbose)
		fprintf(stderr, "Supported ops:");
	if (hdr.allErase) {
		scpt->opHaveFlags |= SCRIPT_OP_FLAG_HAVE_ERASE_ALL;
		if (verbose)
			fprintf(stderr, " ERASEALL");
	}
	if (hdr.pageErase) {
		scpt->opHaveFlags |= SCRIPT_OP_FLAG_HAVE_ERASE_BLOCK;
		if (verbose)
			fprintf(stderr, " ERASEBLOCK");
	}
	if (hdr.pageWrite) {
		scpt->opHaveFlags |= SCRIPT_OP_FLAG_HAVE_WRITE_BLOCK;
		if (verbose)
			fprintf(stderr, " WRITEBLOCK");
	}
	if (verbose)
		fprintf(stderr, "\n");
	
	if (!initRetVal0 || (initRetVal0 & 3)) {
		fprintf(stderr, "FLASH INFO addr invalid 0x%08X\n", initRetVal0);
		return false;
	}
	
	if (initRetVal1 & 3) {
		fprintf(stderr, "FLASH NAMES addr invalid 0x%08X\n", initRetVal1);
		return false;
	}
	
	if (!cpuMemRead(scpt->cpu, initRetVal0, sizeof(struct FlashInfoRaw) / sizeof(uint32_t), (uint32_t*)&tmpInfoHdr)) {
		fprintf(stderr, "FLASH INFO hdr read fail\n");
		return false;
	}
	
	if (tmpInfoHdr.numPieces > 64) {
		fprintf(stderr, "FLASH INFO hdr.numPieces is implausible (%u)\n", tmpInfoHdr.numPieces);
		return false;
	}
	
	bufsz = sizeof(struct FlashInfoRaw) + sizeof(struct FlashInfoPieceRaw) * tmpInfoHdr.numPieces;
	rfi = (struct FlashInfoRaw*)calloc(1, bufsz);
	
	if (!rfi)
		return false;
	
	i = memioReadToBuffer(scpt->cpu, initRetVal0, bufsz, rfi);
	if (bufsz != i) {
		
		fprintf(stderr, "FLASH INFO READ ERROR at 0x%08X\n", initRetVal0 + i);
		free(rfi);
		return false;
	}

	if (initRetVal1) {
		if (!cpuMemRead(scpt->cpu, initRetVal1, sizeof(struct FlashNamesRaw) / sizeof(uint32_t), (uint32_t*)&tmpNamesHdr)) {
			fprintf(stderr, "FLASH NAMES hdr read fail\n");
			return false;
		}
		
		if (tmpNamesHdr.numPieces > 32) {
			fprintf(stderr, "FLASH NAMES hdr.numPieces is implausible (%u)\n", tmpNamesHdr.numPieces);
			return false;
		}
		
		bufsz = sizeof(struct FlashNamesRaw) + sizeof(struct FlashNamePieceRaw) * tmpNamesHdr.numPieces;
		rfn = (struct FlashNamesRaw*)calloc(1, bufsz);
		if (!rfn) {
			free(rfi);
			return false;
		}

		i = memioReadToBuffer(scpt->cpu, initRetVal1, bufsz, rfn);
		if (bufsz != i) {
			
			fprintf(stderr, "FLASH NAMES READ ERROR at 0x%08X\n", initRetVal1 + i);
			free(rfi);
			free(rfn);
			return false;
		}
	}
	
	
	
	/*
	fprintf(stderr, "rad %u words from 0x%08X. info: num=%u, {base: 0x%08X, erz: %u, wri: %u, num: %u}\n",
		nwords, initRetVal0, rfi->numPieces,
		rfi->pieces[0].base, rfi->pieces[0].eraseSz, rfi->pieces[0].writeSz, rfi->pieces[0].numIdentical);
	
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	
	rfi = (struct FlashInfoRaw*)calloc(1, sizeof(struct FlashInfoRaw) + sizeof(struct FlashInfoPieceRaw[64]));
	rfi->pieces[rfi->numPieces].base = 0x08000000;
	rfi->pieces[rfi->numPieces].eraseSz = 14;
	rfi->pieces[rfi->numPieces].writeSz = 14;
	rfi->pieces[rfi->numPieces].numIdentical = 4;
	rfi->numPieces++;
	
	rfi->pieces[rfi->numPieces].base = 0x08010000;
	rfi->pieces[rfi->numPieces].eraseSz = 16;
	rfi->pieces[rfi->numPieces].writeSz = 14;
	rfi->pieces[rfi->numPieces].numIdentical = 1;
	rfi->numPieces++;
	
	rfi->pieces[rfi->numPieces].base = 0x08020000;
	rfi->pieces[rfi->numPieces].eraseSz = 17;
	rfi->pieces[rfi->numPieces].writeSz = 14;
	rfi->pieces[rfi->numPieces].numIdentical = 3;
	rfi->numPieces++;

	rfi->pieces[rfi->numPieces].base = 0x1fff0000;
	rfi->pieces[rfi->numPieces].eraseSz = 10;
	rfi->pieces[rfi->numPieces].writeSz = 10;
	rfi->pieces[rfi->numPieces].numIdentical = 30;
	rfi->numPieces++;
	
	rfi->pieces[rfi->numPieces].base = 0x1fffc000;
	rfi->pieces[rfi->numPieces].eraseSz = 4;
	rfi->pieces[rfi->numPieces].writeSz = 4;
	rfi->pieces[rfi->numPieces].numIdentical = 1;
	rfi->numPieces++;
	
	
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	*/


	ret = scriptExpandAndCheckFlashInfo(scpt, rfi, rfn, verbose);
	free(rfi);
	free(rfn);
	
	return ret;
}
