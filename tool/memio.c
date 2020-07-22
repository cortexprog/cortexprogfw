#include <string.h>
#include <stdio.h>
#include "alloca.h"
#include "utilOp.h"
#include "memio.h"


static void readFileBytes(void *buf, uint32_t len, FILE *f)	//fill with zero after file ends
{
	uint8_t *dst = (uint8_t*)buf;
	
	while(len && !feof(f)) {
		size_t readBytes = fread(dst, 1, len, f);
		
		dst += readBytes;
		len -= (uint32_t)readBytes;
	}
	
	memset(dst, 0, len);
}

bool memioWriteFromFile(struct Cpu* cpu, uint32_t dstAddr, uint32_t dstLen, FILE *f, bool withAck, bool showProgress, bool showSpeedInProgress)
{
	uint32_t wordsDone = 0, wordsAtOnce = cpuGetOptimalWriteNumWords(cpu), wordsTotal = (dstLen + sizeof(uint32_t) - 1) / sizeof(uint32_t);	//round up to word (safe as script data always continues past actual loaded script)
	uint32_t *words = alloca(sizeof(uint32_t) * wordsAtOnce);
	struct UtilProgressState state = {0,};
	
	while (wordsDone < wordsTotal) {
		
		uint32_t wordsNow = wordsTotal - wordsDone, addrNow = dstAddr + sizeof(uint32_t) * wordsDone;
		
		if (wordsNow > wordsAtOnce)
			wordsNow = wordsAtOnce;
		
		readFileBytes(words, sizeof(uint32_t) * wordsNow, f);
		
		if (showProgress)
			utilOpShowProgress(&state, "UPLOADING", dstAddr, dstLen, sizeof(uint32_t) * wordsDone, showSpeedInProgress);
		
		if (!cpuMemWrite(cpu, addrNow, wordsNow, words, withAck)) {
			fprintf(stderr, "FILE UPLOAD: ERROR writing %u words @ 0x%08X\n", wordsNow, addrNow);
			return false;
		}
		
		wordsDone += wordsNow;
	}
	
	if (showProgress)
			utilOpShowProgress(&state, "UPLOADING", dstAddr, dstLen, dstLen, showSpeedInProgress);
	
	return true;
}

uint32_t memioRead(struct Cpu *cpu, uint32_t srcAddr, uint32_t srcLen, bool (*dataCbkF)(const void *data, uint32_t len, void *cbkData), void* cbkData, bool showProgress, bool showSpeedInProgress)
{
	uint32_t readBase = srcAddr &~ 3, skipFront = srcAddr - readBase, readWords = (srcLen + skipFront + 3) / 4, skipEnd = readWords * 4 - skipFront - srcLen;
	uint32_t wordsDone = 0, wordsAtOnce = cpuGetOptimalReadNumWords(cpu), bytesDone = 0;
	uint32_t *words = alloca(sizeof(uint32_t) * wordsAtOnce);
	struct UtilProgressState state = {0,};
	
	while (wordsDone < readWords) {
		
		uint32_t wordsNow = readWords - wordsDone, addrNow = readBase + sizeof(uint32_t) * wordsDone, bytesNow;
		uint8_t *bytes = (uint8_t*)words;
		
		if (wordsNow > wordsAtOnce)
			wordsNow = wordsAtOnce;
		
		if (showProgress)
			utilOpShowProgress(&state, "READING", srcAddr, srcLen, sizeof(uint32_t) * wordsDone, showSpeedInProgress);
		
		if (!cpuMemRead(cpu, addrNow, wordsNow, words)) {
			fprintf(stderr, "ERROR read %u words @ 0x%08X\n", wordsNow, addrNow);
			return bytesDone;
		}
		wordsDone += wordsNow;
		bytesNow = sizeof(uint32_t) * wordsNow - skipFront;
		bytes += skipFront;										//at start, skip front
		skipFront = 0;
		if (wordsDone == readWords)								//at end, skip end
			bytesNow -= skipEnd;
		
		//this can either fail or succeed. Patial success isnt counted
		if (!dataCbkF(bytes, bytesNow, cbkData))
			return bytesDone;
		
		bytesDone += bytesNow;
	}
	
	if (showProgress)
		utilOpShowProgress(&state, "READING", srcAddr, srcLen, srcLen, showSpeedInProgress);

	return bytesDone;
}

static bool memioReadToFileDataCbk(const void *data, uint32_t len, void *cbkData)
{
	FILE *f = (FILE*)cbkData;
	
	if (len == fwrite(data, 1, len, f))
		return true;
	
	fprintf(stderr, "FILE WRITE ERROR writing %u bytes\n", len);
	
	return false;
}

bool memioReadToFile(struct Cpu *cpu, uint32_t srcAddr, uint32_t srcLen, FILE *f, bool showProgress, bool showSpeedInProgress)
{
	return srcLen == memioRead(cpu, srcAddr, srcLen, memioReadToFileDataCbk, f, showProgress, showSpeedInProgress);
}

static bool memioReadToBufferDataCbk(const void *data, uint32_t len, void *cbkData)
{
	uint8_t **dstP = (uint8_t**)cbkData;
	
	memcpy(*dstP, data, len);
	(*dstP) += len;
	
	return true;
}

uint32_t memioReadToBuffer(struct Cpu *cpu, uint32_t srcAddr, uint32_t srcLen, void* dst)
{
	return memioRead(cpu, srcAddr, srcLen, memioReadToBufferDataCbk, &dst, false, false);
}
