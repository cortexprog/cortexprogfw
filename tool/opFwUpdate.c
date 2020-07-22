#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#ifdef WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif
#include "utilOp.h"
#include "memio.h"
#include "ops.h"

struct ToolOpDataFwUpdate {
	FILE *file;
};

static void* fwupdateOpParse(const char *argv0, int *argcP, char ***argvP, uint32_t *needFlagsP)
{
	struct ToolOpDataFwUpdate *data;
	const char *filename = NULL;
	FILE *fil;
	
	if (*argcP < 1) {
		fprintf(stderr, " fwupdate: not enough arguments given for \"fwupdate\" command\n");
		return NULL;
	}
	
	filename = *(*argvP)++;
	(*argcP)--;
	
	//now open the file (early in case we need its size)
	fil = fopen(filename, "rb");
	if (!fil) {
		fprintf(stderr, " fwupdate: unable to open '%s' for \"fwupdate\" command\n", filename);
		return NULL;
	}
	
	*needFlagsP |= TOOL_OP_NEEDS_DEBUGGER;
	
	data = calloc(1, sizeof(struct ToolOpDataFwUpdate));
	if (!data) {
		fclose(fil);
		return NULL;
	}
	
	data->file = fil;
	
	return data;
}

static bool modularGetVerAndFlashInfo(struct Debugger *dbg, uint32_t *flashBaseP, uint32_t *flashSizeP, uint32_t *flashBlockP, bool *encrP, bool* needPaddingP)
{
	uint8_t buf[MODULAR_MAX_PACKET];
	struct CommsPacket *pktP = (struct CommsPacket*)buf;
	struct CommsInfoPacket *nfo = (struct CommsInfoPacket*)&pktP->payload;
	char stringInfo[sizeof(nfo->info) + 1];
	
	pktP->cmd = COMMS_CMD_GET_INFO;
	
	if (debuggerDoOneDevCmd(dbg, pktP, 0, pktP) < (int)sizeof(struct CommsInfoPacket)) {
		fprintf(stderr, "NO BL INFO AVAIL\n");
		return false;
	}
	
	if (nfo->magix != BL_INFO_MAGIX) {
		fprintf(stderr, "BL does not speak our language\n");
		return false;
	}
	
	memcpy(stringInfo, nfo->info, sizeof(nfo->info));
	stringInfo[sizeof(nfo->info)] = 0;
	*encrP = !!(nfo->flags & BL_FLAGS_NEED_ENCR);
	*needPaddingP = !!(nfo->flags & BL_FLAGS_NEED_PADDING);
	
	fprintf(stderr, "BL '%s' v%u (proto v%u%s). Flash: 0x%04X - 0x%04X with (%u x %u-byte blocks)%s\n",
		stringInfo, nfo->blVer, nfo->protoVer, *needPaddingP ? " <PAD>":"", nfo->base, nfo->size + nfo->base, nfo->size / nfo->blockSz, nfo->blockSz,
		*encrP ? " <ENCRYPTION REQUIRED>" : "");
	
	if (nfo->blVer < BL_PROT_VER_CUR) {
		fprintf(stderr, "BL proto too old\n");
		return false;
	}
	
	if ((nfo->size % nfo->blockSz) || (nfo->blockSz & (nfo->blockSz - 1)) || (nfo->blockSz > WRITE_SIZE)) {
		fprintf(stderr, "BL provided flash info that is implausible\n");
		return false;
	}
	
	if (flashBaseP)
		*flashBaseP = nfo->base;
	if (flashSizeP)
		*flashSizeP = nfo->size;
	if (flashBlockP)
		*flashBlockP = nfo->blockSz;

	return true;
}


static bool fwupdateOpDo(void *toolOpData, uint32_t step, uint32_t flags, struct Debugger *dbg, struct Cpu *cpu, struct Script *scpt)
{
	uint8_t buf[MODULAR_MAX_PACKET];
	struct CommsPacket *pkt = (struct CommsPacket*)buf;
	struct CommsWritePacket *wpkt = (struct CommsWritePacket*)pkt->payload;
	struct ToolOpDataFwUpdate *data = (struct ToolOpDataFwUpdate*)toolOpData;
	
	//this implements the ModulaR FW update protocol
	if (step == TOOL_OP_STEP_PRE_DEBUGGER_ID) {
		
		uint32_t flashBase, flashSize, flashBlockSz, nChunks, fileSz, addr, i, ret;
		struct UtilProgressState stateErz = {0,}, stateWri = {0,};
		bool needEncr, needPadding;
		
		//verify connectivity
		if (!modularGetVerAndFlashInfo(dbg, &flashBase, &flashSize, &flashBlockSz, &needEncr, &needPadding))
			return false;
		
		if (flashBlockSz != WRITE_SIZE) {
			fprintf (stderr, "flash block size not as expected\n");
			return false;
		}
		
		//get file length
		if (fseek(data->file, 0, SEEK_END) < 0) {
			fprintf (stderr, "FIO error 1. Bad.\n");
			return false;
		}
		fileSz = ftell(data->file);
		rewind(data->file);
		
		
		if (needEncr) {
			
			if (fileSz % (flashBlockSz + sizeof(SPECK_DATA_TYPE[SPECK_DATA_WORDS]))) {
				fprintf (stderr, "update not properly sized for encrypted data\n");
				return false;
			}
			
			nChunks = fileSz / (flashBlockSz + sizeof(SPECK_DATA_TYPE[SPECK_DATA_WORDS]));
			if (nChunks < 2) {
				fprintf (stderr, "Update size (%u bytes) too small to be meaningful\n", fileSz);
				return false;
			}
			nChunks--;	//do not count the "sig"
		}
		else {
			nChunks = (fileSz + flashBlockSz - 1) / flashBlockSz;
		}
		
		if (nChunks * flashBlockSz > flashSize) {
			fprintf (stderr, "Update size (%u bytes) larger than flash size (%u bytes)\n", fileSz, flashSize);
			return false;
		}
		
		fprintf(stderr, "Applying %u-byte update (%u blocks)\n", nChunks * flashBlockSz, nChunks);
		
		//in theory, erase should be exactly the same number of blocks as we heard. assume that for progress bar purposes
		addr = flashBase;
		do {
			pkt->cmd = COMMS_CMD_SECURE_ERASE;
			utilOpShowProgress(&stateErz, "ERASING", flashBase,  flashSize, addr - flashBase, true);
			addr += flashBlockSz;
			ret = debuggerDoOneDevCmd(dbg, pkt, 0, pkt);
			if (ret < 1) {
				fprintf (stderr, "USB error. Bad.\n");
				return false;
			}
			
		} while(!pkt->payload[0]);
		utilOpShowProgress(&stateErz, "ERASING", flashBase, flashSize, flashBase + flashSize, true);			//complete the progress bar
		
		//write
		addr = flashBase;
		for (i = 0; i < nChunks; i++, addr += flashBlockSz) {
			
			memset(buf, 0, sizeof(buf));
			pkt->cmd = COMMS_CMD_WRITE_BLOCK;
			
			if (needEncr) {
				if (sizeof(wpkt->iv) != fread(&wpkt->iv, 1, sizeof(wpkt->iv), data->file)) {
					fprintf (stderr, "FIO error 3. Bad.\n");
					return false;
				}
			}
			
			ret = (uint32_t)fread(wpkt->data, 1, flashBlockSz, data->file);
			if ((needEncr || i != nChunks - 1) && ret != flashBlockSz) {
				fprintf (stderr, "FIO error 2. Bad.\n");
				return false;
			}
			
			utilOpShowProgress(&stateWri, "WRITING", flashBase, nChunks * flashBlockSz, addr - flashBase, true);
			ret = debuggerDoOneDevCmd(dbg, pkt, sizeof(struct CommsWritePacket), pkt);
			if (ret < 1) {
				fprintf (stderr, "USB error. Bad.\n");
				return false;
			}
	
			if (!pkt->payload[0]) {
				fprintf (stderr, "Write error. Bad.\n");
				return false;
			}
		}
		utilOpShowProgress(&stateWri, "WRITING", flashBase, flashSize, flashBase + flashSize, true);			//complete the progress bar
		
		//send "DONE" packet
		fprintf(stderr, "upload done - telling device\n");
		if (needEncr) {
			
			if (sizeof(wpkt->iv) != fread(&wpkt->iv, 1, sizeof(wpkt->iv), data->file)) {
				fprintf (stderr, "FIO error 4. Bad.\n");
				return false;
			}
			
			ret = (uint32_t)fread(wpkt->data, 1, flashBlockSz, data->file);
			if (ret != flashBlockSz) {
				fprintf (stderr, "FIO error 5. Bad.\n");
				return false;
			}
		}
		
		wpkt->flags = FLAG_UPLOAD_DONE;
		ret = debuggerDoOneDevCmd(dbg, pkt, sizeof(struct CommsWritePacket), pkt);
		if (ret < 1) {
			fprintf (stderr, "USB error 2 . Bad.\n");
			return false;
		}
	
		if (!pkt->payload[0]) {
			fprintf (stderr, "Write complete error. Bad.\n");
			return false;
		}
		
		//reboot device
		fprintf(stderr, "rebooting device - please wait\n");
		pkt->cmd = COMMS_CMD_BOOT_APP;
		ret = debuggerDoOneDevCmd(dbg, pkt, 0, pkt);
		if (ret < 1) {
			fprintf (stderr, "USB error 3 . Bad.\n");
			return false;
		}
		
		#ifdef WIN32
			Sleep(1000);
		#else
			sleep(1);
		#endif
		fprintf(stderr, "CortexProg will now exit\n");
		exit(0);
	}
	
	return true;
}



void utilOpShowProgress(struct UtilProgressState *state, const char *op, uint32_t startAddr, uint32_t length, uint32_t curAddr, bool showSpeed);







static void fwupdateOpFree(void *toolOpData)
{
	struct ToolOpDataFwUpdate *data = (struct ToolOpDataFwUpdate*)toolOpData;
	
	fclose(data->file);
	free(data);
}

static void fwupdateOpHelp(const char *argv0)	//help for "fwupdate"
{
	fprintf(stderr,
		"USAGE: %s fwupdate filename\n"
		"\tPerforms a fimrwre update on the CortexProg device.\n"
		"\t Get firmware updtes on https://CortexProg.com\n", argv0);
}

DEFINE_OP(fwupdate, fwupdateOpParse, fwupdateOpDo, fwupdateOpFree, fwupdateOpHelp);