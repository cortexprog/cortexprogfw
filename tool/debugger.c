#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "debugger.h"
#undef UNALIGNED
#include "alloca.h"
#include "../wire.h"
#include "hidapi.h"
#include "utilOp.h"

#define MY_VID		0x4744
#define MY_PID		0x5043


#if defined(WIN32) || defined(__APPLE__)
#define USE_WIN_PADDING 1
#else
#define USE_WIN_PADDING 0
#endif


struct HidEnumedDevice {
	struct HidEnumedDevice *nextUnrelated;		//linked list of devices that are not this one //only in first device in a chain of related devices
	struct HidEnumedDevice *nextRelated;		//linked list of related devices (doubly linked list is easier here)
	struct HidEnumedDevice *prevRelated;		//linked list of related devices (doubly linked list is easier here)
	wchar_t *snum;								//local copy
	char *path;									//local copy
	
	unsigned short usage;						//only valid on windows - not needed on linux anyways
};


struct Debugger {
	hid_device *handle;
	hid_device *handleWinNumber2;	//windows seems to find each device twice...this is the second handle if we found it
	struct SwdCommsVerInfoRespPacketV5 verInfo;
	bool needPadding;
	bool haveSimplePowerCtl;	//on/off only for 3.3V, low current or the debugger browns out
	bool haveVariablePowerCtl;	//settable voltage, high current
	bool haveVariableClkSpeed;	//clock speed control
	bool isSlowHardware;		//for slow debugger hardware
	bool haveCodeUpload;		//code upload supported
	bool haveResetPin;			//reest pin supported
};

//At least linux will queue up bufers from device even while we're not expecting them. This will drain them.
static void drainOsBuf(hid_device *handle)
{
	struct CommsPacket pkt;
	
	while (hid_read_timeout(handle, (uint8_t*)&pkt, sizeof(struct CommsPacket), 100) > 0);
}

static bool readRemoteVer(struct Debugger *dbg, bool verbose)
{
	int rspSz, bufSz = dbg->verInfo.maxXferBytes;
	char *hwNames[256] = {0,};
	wchar_t snum[129] = {0,};
	struct CommsPacket *pkt;
	const char *hwName;
	uint8_t *buf;
	
	hwNames[SWD_COMMS_HW_TYP_AVR_PROTO] = "AVR simple CortexProg";
	hwNames[SWD_COMMS_HW_TYP_EFM] = "ARM CortexProg";
	hwNames[SWD_COMMS_HW_TYP_UNKNOWN] = "Unknown CortexProg type";


	buf = alloca(bufSz);
	pkt = (struct CommsPacket*)buf;

	pkt->cmd = SWD_COMMS_CMD_VER_INFO;
	rspSz = debuggerDoOneDevCmd(dbg, pkt, 0, pkt);
	
	if (rspSz == sizeof(struct SwdCommsVerInfoRespPacketV1) || rspSz == sizeof(struct SwdCommsVerInfoRespPacketV2) || rspSz == sizeof(struct SwdCommsVerInfoRespPacketV3) || rspSz == sizeof(struct SwdCommsVerInfoRespPacketV4) || rspSz == sizeof(struct SwdCommsVerInfoRespPacketV5)) {
		
		//because they overlay nicely (on purpose), this is doable
		memcpy(&dbg->verInfo, pkt->payload, rspSz);
	}
	else {
		fprintf(stderr, "VER GET INFO ERR\n");
		return false;
	}
	
	if ((uint32_t)dbg->verInfo.hwType >= sizeof(hwNames) / sizeof(*hwNames) || !hwNames[dbg->verInfo.hwType])
		dbg->verInfo.hwType = SWD_COMMS_HW_TYP_UNKNOWN;
	
	hwName = hwNames[dbg->verInfo.hwType];
	
	if (hid_get_serial_number_string(dbg->handle, snum, sizeof(snum) / sizeof(*snum) - 1))
		snum[0] = 0;

	if (verbose) {
		fprintf(stderr, "SWD HW: '%s' ver. %u, serial '%S'\nSWD FW: %u.%u.%u.%u, maxXfer: %u, flags 0x%08X%s\n",
			hwName, dbg->verInfo.hwVer, snum,
			(uint8_t)(dbg->verInfo.swdAppVer >> 24), (uint8_t)(dbg->verInfo.swdAppVer >> 16), (uint8_t)(dbg->verInfo.swdAppVer >> 8), (uint8_t)(dbg->verInfo.swdAppVer >> 0),
			dbg->verInfo.maxXferBytes, dbg->verInfo.flags, dbg->verInfo.flags ? ":" : "");
		
		if (dbg->verInfo.flags & USB_FLAGS_FTR_OUT_SUPPORTED)
			fprintf(stderr, "  flag 0x%08lX: Using FeatureOut transfer mechanism (faster)\n", USB_FLAGS_FTR_OUT_SUPPORTED);
		if (dbg->verInfo.flags & USB_FLAGS_NEED_PACKET_PADDING)
			fprintf(stderr, "  flag 0x%08lX: This hw needs packet padding (slower)\n", USB_FLAGS_NEED_PACKET_PADDING);
		if (dbg->verInfo.flags & UART_FLAG_UART_EXISTS)
			fprintf(stderr, "  flag 0x%08lX: Has built-in UART (for serial I/O to device)\n", UART_FLAG_UART_EXISTS);
		if (dbg->verInfo.flags & PWR_FLAG_PWR_CTRL_ON_OFF) {
			fprintf(stderr, "  flag 0x%08lX: Supports on/off power control (", PWR_FLAG_PWR_CTRL_ON_OFF);
			
			if (dbg->verInfo.millivoltsMin && dbg->verInfo.milliampsMax && dbg->verInfo.millivoltsMax == dbg->verInfo.millivoltsMin)
				fprintf(stderr, "%u.%03uV, %u", dbg->verInfo.millivoltsMin / 1000, dbg->verInfo.millivoltsMin % 1000, dbg->verInfo.milliampsMax);
			else	//default
				fprintf(stderr, "3.300V, 30");
			fprintf(stderr, " mA max)\n");
		}
		if (dbg->verInfo.flags & PWR_FLAG_PWR_CTRL_SETTABLE) {
			fprintf(stderr, "  flag 0x%08lX: Supports supplying voltage", PWR_FLAG_PWR_CTRL_SETTABLE);
			if ((dbg->verInfo.millivoltsMin || dbg->verInfo.millivoltsMax) && (dbg->verInfo.millivoltsMin < dbg->verInfo.millivoltsMax) && dbg->verInfo.milliampsMax)
				fprintf(stderr, " (%u.%03uV - %u.%03uV, %u mA max)", dbg->verInfo.millivoltsMin / 1000, dbg->verInfo.millivoltsMin % 1000, dbg->verInfo.millivoltsMax / 1000, dbg->verInfo.millivoltsMax % 1000, dbg->verInfo.milliampsMax);
			fprintf(stderr, "\n");
		}
		if (dbg->verInfo.flags & SWD_FLAG_CLOCK_SPEED_SETTABLE) {
			fprintf(stderr, "  flag 0x%08lX: Supports clock speed control", SWD_FLAG_CLOCK_SPEED_SETTABLE);
			if (dbg->verInfo.maxClockRate)
				fprintf(stderr, " (%u.%06u MHz max)", dbg->verInfo.maxClockRate / 1000000, dbg->verInfo.maxClockRate % 1000000);
			fprintf(stderr, "\n");
		}
		if (dbg->verInfo.flags & SWD_FLAG_MULTICORE_SUPPORT)
			fprintf(stderr, "  flag 0x%08lX: Supports multi-core targets\n", SWD_FLAG_MULTICORE_SUPPORT);
		if (dbg->verInfo.flags & SWD_FLAG_SLOW_DEBUGGER)
			fprintf(stderr, "  flag 0x%08lX: Slow hardware - timeouts increased\n", SWD_FLAG_SLOW_DEBUGGER);
		if (dbg->verInfo.flags & SWD_FLAG_UPLOADABLE_CODE)
			fprintf(stderr, "  flag 0x%08lX: Code Uploading - fast custom ops run on debugger\n", SWD_FLAG_UPLOADABLE_CODE);
		if (dbg->verInfo.flags & SWD_FLAG_RESET_PIN)
			fprintf(stderr, "  flag 0x%08lX: Reset Pin - dedicated reset pin (rare chips need)\n", SWD_FLAG_RESET_PIN);
		
	}

	dbg->needPadding = !!(dbg->verInfo.flags & USB_FLAGS_NEED_PACKET_PADDING);
	dbg->haveSimplePowerCtl = !!(dbg->verInfo.flags & PWR_FLAG_PWR_CTRL_ON_OFF);
	dbg->haveVariablePowerCtl = !!(dbg->verInfo.flags & PWR_FLAG_PWR_CTRL_SETTABLE);
	dbg->haveVariableClkSpeed = !!(dbg->verInfo.flags & SWD_FLAG_CLOCK_SPEED_SETTABLE);
	dbg->isSlowHardware = !!(dbg->verInfo.flags & SWD_FLAG_SLOW_DEBUGGER);
	dbg->haveCodeUpload = !!(dbg->verInfo.flags & SWD_FLAG_UPLOADABLE_CODE);
	dbg->haveResetPin = !!(dbg->verInfo.flags & SWD_FLAG_RESET_PIN);
	
	return true;
}
bool debuggerHasResetPin(const struct Debugger *dbg)
{
	return dbg->haveResetPin;
}

bool debuggerCanSupportUploadableCode(const struct Debugger *dbg)
{
	return dbg->haveCodeUpload;
}

bool debuggerCanSupportOnOffPowerCtl(const struct Debugger *dbg)
{
	return dbg->haveSimplePowerCtl;
}

bool debuggerCanSupportVariablePowerCtl(const struct Debugger *dbg)
{
	return dbg->haveVariablePowerCtl;
}

bool debuggerCanSupportVariableClockSpeed(const struct Debugger *dbg)
{
	return dbg->haveVariableClkSpeed;
}

bool debuggerIsSlowHw(const struct Debugger *dbg)
{
	return dbg->isSlowHardware;
}

static struct HidEnumedDevice* debuggersEnumerationCreate(void)
{
	struct HidEnumedDevice *ret = NULL, *t, *pt, *n, *tt, *ptt;
	struct hid_device_info *devs, *dev;
		
	devs = hid_enumerate (MY_VID, MY_PID);
	for (dev = devs; dev; dev = dev->next) {
		
		//snums are a must
		if (!dev->serial_number || !*dev->serial_number)
			continue;
		
		//alloc a struct and fill it in
		n = (struct HidEnumedDevice*)calloc(1, sizeof(struct HidEnumedDevice));
		if (n) {
			
			n->snum = wcsdup(dev->serial_number);
			n->path = strdup(dev->path);
			
			#ifdef WIN32
				n->usage = dev->usage;
			#else
				n->usage = 1;	//always on linux
			#endif
			
			if (n->snum && n->path) {
			
				//first see if it is the same serial number as an existing device
				for (pt = NULL, t = ret; t && wcscmp (t->snum, n->snum); pt = t, t = t->nextUnrelated);
				
				if (t) {	//duplicate snum in the "t" family - handle this
					
					//verify not duplicate usage (and while we're at it find isertion plce in order if not dupe)
					for (ptt = NULL, tt = t; tt && tt->usage < n->usage; ptt = tt, tt = tt->nextRelated);
					if (!tt || tt->usage != n->usage) {
						
						//not duplicate - link it in, in order from lowest to higest usage number
						//tt currently points to whatever will be AFTER our insertion, ptt to before
						n->prevRelated = ptt;
						n->nextRelated = tt;
						if (ptt)
							ptt->nextRelated = n;
						else if (pt)
							pt->nextUnrelated = n;
						else {
							n->nextUnrelated = ret->nextUnrelated;
							ret = n;
						}
						if (tt)
							tt->prevRelated = n;
						
						continue;
					}
					
					fprintf(stderr, "duplicate usage %u on duplicate snum '%S' - not allowed\n", n->usage, n->snum);
				}
				else {		//not duplicate - new device - link it in
					
					n->nextUnrelated = ret;
					ret = n;
					continue;
				}
			}
			
			free(n->path);
			free(n->snum);
			free(n);
		}
	}
	hid_free_enumeration(devs);
	
	return ret;
}

static void debuggersEnumerationFree(struct HidEnumedDevice* enumeration)
{
	struct HidEnumedDevice *t, *tt;
	
	while (enumeration) {
		
		//grab next chain link for now
		t = enumeration;
		enumeration = enumeration->nextUnrelated;
		
		//free the chain
		while (t) {
			
			free(t->snum);
			free(t->path);
			tt = t->nextRelated;
			free(t);
			t = tt;
		}
	}
}

struct Debugger *debuggerOpen(const char *snum, bool verbose)					//snum can be null
{
	struct HidEnumedDevice *devs = debuggersEnumerationCreate(), *dev, *found = NULL;
	wchar_t snumWbuf[128], *snumW = snumWbuf;
	hid_device *wantedDev[2] = {0,};	//two usages max for now
	struct Debugger *dbg = NULL;
	int nDevs = 0;
	
	/*
	So on windows this shit gets really messy. since windows insists on
	always writing full report size to a device, we have two report sizes
	in the fast device. But, windows then shows it to us (via hidapi) as
	two devices. We can disambiguate them via the "usage" flag in the
	report and thus know what size each is, but that still leaves us
	having to manage two devices. Sadly I know of no solution to this.
	This does mean that we have to listen to both at once. And since
	there is no way to cancel a read this means busy waiting in cases
	like tracing. We also have to properly manage opening alltogether.
	The nasty code that follows manages all that by finding duplicate
	devices that are really one device and storing the info for us to
	digest later. For linux-only use cases, or if we were ok using the
	device on windows only in slow mode, this would not be needed. For
	extra fun, we must also remember that we have the AVR CortexProg
	which only supports small packets, and in the future other versions
	might have other packet sizes too. What a mess windows makes for us
	here! And all this in the name of not needing drivers - almost not
	worth it.
	
	Assumptions:	No cortexprog devices ever share a serial number!
					They absolutely must be unique!
	*/
	
	
	//convert given snum to wide if given
	if (snum == DEBUGGER_SNUM_GUARANTEED_ILLEGAL)
		snumW = DEBUGGER_SNUM_GUARANTEED_ILLEGAL;
	else if (snum)
		snumWbuf[mbstowcs(snumW, snum, sizeof(snumWbuf) / sizeof(*snumWbuf) - 1)] = 0;
	else
		snumW = NULL;
	
	//count devices and see if any match the requested snum
	for (dev = devs; dev; dev = dev->nextUnrelated) {
		
		if (snumW && snumW != DEBUGGER_SNUM_GUARANTEED_ILLEGAL && !wcscmp (dev->snum, snumW))
			found = dev;
		
		nDevs++;
	}

 	//if no snum given and only one device exists, assume it is the one
 	if (nDevs == 1 && !snumW)
 		found = devs;
 	
 	if (!found) {
 		
 		bool showList = true;
 		
 		if (snumW == DEBUGGER_SNUM_GUARANTEED_ILLEGAL)
 			{}/*nothing*/
 		else if (snumW)
 			fprintf(stderr, "Requested device '%s' not found.", snum);
 		else if (nDevs)
 			fprintf(stderr, "More than one device found.");
 		else {
 			fprintf(stderr, "No devices found.");
 			showList = false;
 		}
 		
 		if (showList) {
 		
	 		fprintf(stderr, " Available %u device(s):\n", nDevs);
	 		
	 		for (dev = devs; dev; dev = dev->nextUnrelated)
	 			fprintf(stderr, "\t'%S'\n", dev->snum);
		 }
		 
		 fprintf(stderr, "\n");
 	}
	
	if (found) {
	
		//for now we only support devices with one or two usages ands they should be "1" and "2" or just "1"
		if (found->usage != 1)
			fprintf(stderr, "Device does not have proper first usage (%u). Unable to proceed.\n", found->usage);
		else if (found->nextRelated && found->nextRelated->usage != 2)
			fprintf(stderr, "Device does not have proper second usage (%u). Unable to proceed.\n", found->nextRelated->usage);
		else if (found->nextRelated && found->nextRelated->nextRelated)
			fprintf(stderr, "Device has too many usages. Unable to proceed.\n");
		else {

			wantedDev[0] = hid_open_path(found->path);
			if (wantedDev[0] && found->nextRelated)
				wantedDev[1] = hid_open_path(found->nextRelated->path);
			
			if (!wantedDev[0] || (found->nextRelated && !wantedDev[1])) {
				fprintf(stderr, "Failed to open device '%s' or subdevices. Unable to proceed.\n", found->path);
				if (wantedDev[0])
					hid_close(wantedDev[0]);
			}
			else {
			
				dbg = calloc(1, sizeof(struct Debugger));
				if (!dbg) {
					hid_close(wantedDev[0]);
					if (wantedDev[1])
						hid_close(wantedDev[1]);
					return NULL;
				}
				
				dbg->handle = wantedDev[0];
				dbg->handleWinNumber2 = wantedDev[1];
				dbg->verInfo.maxXferBytes = MODULAR_MAX_USEFUL_BYTES;	//a safe value to start with
				dbg->verInfo.hwType = SWD_COMMS_HW_TYP_UNKNOWN;
				dbg->verInfo.flags = 0;
				dbg->needPadding = true;								//a safe default to start with
				
				drainOsBuf(dbg->handle);
				if(dbg->handleWinNumber2)
					drainOsBuf(dbg->handleWinNumber2);
			}
		}
	}
	
	debuggersEnumerationFree(devs);
	return dbg;
}

bool debuggerInit(struct Debugger *dbg, bool verbose)
{
	return readRemoteVer(dbg, verbose);
}

void debuggerClose(struct Debugger *dbg)
{
	if(dbg->handleWinNumber2)
		hid_close(dbg->handleWinNumber2);
	hid_close(dbg->handle);
	free(dbg);
}

//return num bytes of payload or PACKET_RX_FAIL
uint32_t debuggerDoOneDevCmd(struct Debugger *dbg, const struct CommsPacket *pktTx, uint32_t txPayloadLen, struct CommsPacket *pktRx)
{
	int nRetries = 8, rxPayloadLen, rxLen, bufSzTx, bufSzRx, reportNoTx, maxPktSzAsWeHaveIt;
	hid_device *hidTxHandle = dbg->handle;
	struct CommsPacket *pktTxReal;
	unsigned char *bufTx, *bufRx;
	
	if (USE_WIN_PADDING && txPayloadLen + sizeof(struct CommsPacket) <= MODULAR_MAX_USEFUL_BYTES) {
		maxPktSzAsWeHaveIt = MODULAR_MAX_USEFUL_BYTES;
		reportNoTx = 1;
	}
	else if (txPayloadLen + sizeof(struct CommsPacket) <= dbg->verInfo.maxXferBytes) {
		maxPktSzAsWeHaveIt = dbg->verInfo.maxXferBytes;
		reportNoTx = 2;
		if (dbg->handleWinNumber2)
			hidTxHandle = dbg->handleWinNumber2;
	}
	else
		return 0;
	
	bufSzTx = maxPktSzAsWeHaveIt		+ WINPACKET_OVERHEAD /* in case we need winpadding */ + 1 /* for report id*/;
	bufSzRx = dbg->verInfo.maxXferBytes	+ WINPACKET_OVERHEAD /* in case we need winpadding */ + 1 /* for report id*/;
	bufRx = alloca(bufSzRx);
	bufTx = alloca(bufSzTx);
	pktTxReal = (struct CommsPacket*)(bufTx + 1);
	
	//do not leak anything
	memset(bufTx, 0, bufSzTx);
	memset(bufRx, 0, bufSzRx);
	
	//this is the report number. 0th report always uses MODULAR_MAX_PACKET_SZ packet sz, 1st always dbg->verInfo.maxXferBytes, so decide now
	bufTx[0] = reportNoTx;

	if (USE_WIN_PADDING) {
		struct CommsWinPacket *winPkt = (struct CommsWinPacket*)pktTxReal;
		uint32_t maxPacketSz = maxPktSzAsWeHaveIt + WINPACKET_OVERHEAD;
		
		winPkt->cmd = CMD_WIN_PADDED;
		winPkt->winHdr.actualPayloadLen = txPayloadLen;
		winPkt->actualPacket = *pktTx;
		memcpy(winPkt->actualPacket.payload, pktTx->payload, txPayloadLen);
		txPayloadLen = maxPacketSz - sizeof(struct CommsPacket);		//always max sz
		winPkt->crc = commsPacketCalcCrc(pktTxReal, txPayloadLen);
	}
	else {
		*pktTxReal = *pktTx;
		memcpy(pktTxReal->payload, pktTx->payload, txPayloadLen);
		pktTxReal->crc = commsPacketCalcCrc(pktTxReal, txPayloadLen);
		
		if (dbg->needPadding && !((txPayloadLen + sizeof(struct CommsPacket)) % 8)) {
			pktTxReal->cmd |= CMD_ORR_PADDED;
			txPayloadLen++;
		}
	}
	
	while(1) {
		
		if (!nRetries--)
			return PACKET_RX_FAIL;
		
		if (hid_write(hidTxHandle, bufTx, 1 + txPayloadLen + sizeof(struct CommsPacket)) < 0)
			return PACKET_RX_FAIL;
		
		if (!pktRx)
			return 0;
		
		if (dbg->handleWinNumber2) {	//we need to read on both somehow... no api for it (win does support it though)
			uint64_t start = getTicks();
			
			while(getTicks() - start < 500) {
				
				rxLen = hid_read_timeout(dbg->handle, bufRx, bufSzRx, 3);		//3 is a compromise on speed and cpu busy-waiting
				if (rxLen > 0)
					break;
				rxLen = hid_read_timeout(dbg->handleWinNumber2, bufRx, bufSzRx, 3);
				if (rxLen > 0)
					break;
			}
		}
		else
			rxLen = hid_read_timeout(dbg->handle, bufRx, bufSzRx, 500);	//read efficiently
		
		while (rxLen > 0) {
			
			struct CommsPacket *pktRxRaw = (struct CommsPacket*)(bufRx + 1);
			
			if (rxLen < sizeof(struct CommsPacket) + 1 /* report id */)
				continue;
			
			rxLen--;	//report id;
			rxPayloadLen = rxLen - sizeof(struct CommsPacket);
			
			if (commsPacketCalcCrc(pktRxRaw, rxPayloadLen) != pktRxRaw->crc)
				break;
			
			//if win padded, decode right away
			if (pktRxRaw->cmd == CMD_WIN_PADDED) {
				
				struct CommsWinPacket *winPkt = (struct CommsWinPacket*)pktRxRaw;
				
				rxPayloadLen = winPkt->winHdr.actualPayloadLen;
				*pktRxRaw = winPkt->actualPacket;
				memmove(pktRxRaw->payload, winPkt->actualPacket.payload, rxPayloadLen);
			}
			
			if (pktRxRaw->cmd != (pktTx->cmd &~ CMD_ORR_PADDED))
				continue;

			//copy over to real RXed data
			*pktRx = *pktRxRaw;
			memcpy(pktRx->payload, pktRxRaw->payload, rxPayloadLen);

			return rxPayloadLen;
		}
	}
}

uint32_t debuggerGetMaxXferSz(struct Debugger *dbg)
{
	return dbg->verInfo.maxXferBytes;
}

static bool debuggerPowerCtlCommon(struct Debugger *dbg, struct CommsPacket *pktTx, uint32_t payloadBytes)
{
	unsigned char *outBuf = alloca(debuggerGetMaxXferSz(dbg));
	struct CommsPacket *pktRx = (struct CommsPacket*)outBuf;
	uint32_t payloadSzOut;
	
	pktTx->cmd = SWD_POWER_CTRL;
	payloadSzOut = debuggerDoOneDevCmd(dbg, pktTx, payloadBytes, pktRx);
	if (payloadSzOut == PACKET_RX_FAIL ) {
		fprintf(stderr, "PWR CTL CMD ERR\n");
		return false;
	}
	
	return payloadSzOut == 1 && pktRx->payload[0];
}

bool debuggerPowerCtlSimple(struct Debugger *dbg, bool on)
{
	unsigned char buf[sizeof(struct CommsPacket) + 1];
	struct CommsPacket *pkt = (struct CommsPacket*)buf;
	
	pkt->payload[0] = on ? 1 : 0;
	
	return debuggerPowerCtlCommon(dbg, pkt, 1);
}

bool debuggerResetPinCtl(struct Debugger *dbg, bool high)
{
	unsigned char buf[sizeof(struct CommsPacket) + 1];
	struct CommsPacket *pktTx = (struct CommsPacket*)buf;
	unsigned char *outBuf = alloca(debuggerGetMaxXferSz(dbg));
	struct CommsPacket *pktRx = (struct CommsPacket*)outBuf;
	uint32_t payloadSzOut;
	
	pktTx->cmd = SWD_COMMS_RESET_PIN_CTL;
	pktTx->payload[0] = high ? 1 : 0;
	payloadSzOut = debuggerDoOneDevCmd(dbg, pktTx, 1, pktRx);
	if (payloadSzOut == PACKET_RX_FAIL ) {
		fprintf(stderr, "RST CTL CMD ERR\n");
		return false;
	}
	
	return payloadSzOut == 1 && pktRx->payload[0];
}

static bool debuggerUploadableCodeCmd(struct Debugger *dbg, struct SwdUploadableCodeCtlPacket *tx, struct SwdUploadableCodeCtlPacket* rx)
{
	unsigned char buf[sizeof(struct CommsPacket) + sizeof(struct SwdUploadableCodeCtlPacket)];
	struct CommsPacket *pktTx = (struct CommsPacket*)buf;
	struct SwdUploadableCodeCtlPacket *codeTx = (struct SwdUploadableCodeCtlPacket*)(pktTx->payload);
	unsigned char *outBuf = alloca(debuggerGetMaxXferSz(dbg));
	struct CommsPacket *pktRx = (struct CommsPacket*)outBuf;
	struct SwdUploadableCodeCtlPacket *codeRx = (struct SwdUploadableCodeCtlPacket*)(pktRx->payload);
	uint32_t payloadSzOut;
	
	*codeTx = *tx;
	pktTx->cmd = SWD_COMMS_UPLOAD_CODE_CTL;
	payloadSzOut = debuggerDoOneDevCmd(dbg, pktTx, sizeof(struct SwdUploadableCodeCtlPacket), pktRx);
	if (payloadSzOut != sizeof(struct SwdUploadableCodeCtlPacket)) {
		fprintf(stderr, "CODE CTL CMD ERR\n");
		return false;
	}
	
	if (codeRx->ctlCode == SWD_UPLOAD_CTL_CODE_RESP_FAIL)
		return false;
	else if (codeRx->ctlCode == SWD_UPLOAD_CTL_CODE_RESP_OK) {
		*rx = *codeRx;
		return true;
	}
	else {
		fprintf(stderr, "CODE CTL CMD ERR 2\n");
		return false;
	}
}

bool debuggerUploadableCodeInit(struct Debugger *dbg)
{
	struct SwdUploadableCodeCtlPacket codeTx = {0,}, codeRx = {0,};
	
	codeTx.ctlCode = SWD_UPLOAD_CTL_CODE_INIT;
	return debuggerUploadableCodeCmd(dbg, &codeTx, &codeRx);
}

bool debuggerUploadableCodeRun(struct Debugger *dbg, uint32_t *regsInOutP)
{
	struct SwdUploadableCodeCtlPacket codeTx = {0,}, codeRx = {0,};
	
	codeTx.ctlCode = SWD_UPLOAD_CTL_CODE_RUN;
	memcpy(codeTx.regs.regs, regsInOutP, sizeof(codeTx.regs.regs));
	if (!debuggerUploadableCodeCmd(dbg, &codeTx, &codeRx))
		return false;
	memcpy(regsInOutP, codeRx.regs.regs, sizeof(codeRx.regs.regs));
	return true;
}

bool debuggerUploadableCodeSendOpcode(struct Debugger *dbg, uint8_t opcode, uint32_t param1, uint32_t param2, uint32_t param3, uint32_t *returnValP)
{
	struct SwdUploadableCodeCtlPacket codeTx = {0,}, codeRx = {0,};
	
	codeTx.ctlCode = SWD_UPLOAD_CTL_CODE_ADD_OPCODE;
	codeTx.opcode.opcode = opcode;
	switch (opcode) {								//handle data to send
		case SWD_UPLOAD_OPCODE_CALL_NATIVE:
			codeTx.opcode.imm8 = param1;
			break;
		case SWD_UPLOAD_OPCODE_CALL_GENERATED:
		case SWD_UPLOAD_OPCODE_LABEL_FREE:
		case SWD_UPLOAD_OPCODE_PREDECL_LBL_TO_LBL:
		case SWD_UPLOAD_OPCODE_PREDECL_LBL_FREE:
		case SWD_UPLOAD_OPCODE_BRANCH_UNCONDITIONAL:
			codeTx.opcode.imm32 = param1;
			break;
		case SWD_UPLOAD_OPCODE_RETURN:
		case SWD_UPLOAD_OPCODE_LABEL_GET_CUR:
		case SWD_UPLOAD_OPCODE_PREDECL_LBL_ALLOC:
			break;
		case SWD_UPLOAD_OPCODE_EXIT:
			codeTx.opcode.imm8 = param1;
			break;
		case SWD_UPLOAD_OPCODE_BRANCH_UNSIGNED_GE:
		case SWD_UPLOAD_OPCODE_BRANCH_UNSIGNED_GT:
		case SWD_UPLOAD_OPCODE_BRANCH_UNSIGNED_LE:
		case SWD_UPLOAD_OPCODE_BRANCH_UNSIGNED_LT:
		case SWD_UPLOAD_OPCODE_BRANCH_EQ:
		case SWD_UPLOAD_OPCODE_BRANCH_NE:
			codeTx.opcode.imm32 = param3;
			//fallthrough
		case SWD_UPLOAD_OPCODE_MOV:
		case SWD_UPLOAD_OPCODE_NOT:
		case SWD_UPLOAD_OPCODE_ADD_REG:
		case SWD_UPLOAD_OPCODE_SUB_REG:
		case SWD_UPLOAD_OPCODE_AND:
		case SWD_UPLOAD_OPCODE_ORR:
		case SWD_UPLOAD_OPCODE_XOR:
		case SWD_UPLOAD_OPCODE_LSL_REG:
		case SWD_UPLOAD_OPCODE_LSR_REG:
			codeTx.opcode.dstReg = param1;
			codeTx.opcode.srcReg = param2;
			break;
		case SWD_UPLOAD_OPCODE_BRANCH_EQ_IMM:
		case SWD_UPLOAD_OPCODE_BRANCH_NOT_EQ_IMM:
			codeTx.opcode.imm32 = param3;
			//fallthrough
		case SWD_UPLOAD_OPCODE_ADD_IMM:
		case SWD_UPLOAD_OPCODE_SUB_IMM:
		case SWD_UPLOAD_OPCODE_LSL_IMM:
		case SWD_UPLOAD_OPCODE_LSR_IMM:
			codeTx.opcode.dstReg = param1;
			codeTx.opcode.imm8 = param2;
			break;
		case SWD_UPLOAD_OPCODE_LDR_IMM:
		case SWD_UPLOAD_OPCODE_BRANCH_NEG:
		case SWD_UPLOAD_OPCODE_BRANCH_NOT_NEG:
		case SWD_UPLOAD_OPCODE_BRANCH_ZERO:
		case SWD_UPLOAD_OPCODE_BRANCH_NOT_ZERO:
			codeTx.opcode.dstReg = param1;
			codeTx.opcode.imm32 = param2;
			break;
		case SWD_UPLOAD_OPCODE_PUSH:
		case SWD_UPLOAD_OPCODE_POP:
			codeTx.opcode.dstReg = param1;
			break;
		case SWD_UPLOAD_OPCODE_PREDECL_LBL_FILL:
			codeTx.opcode.imm32 = param1;
			codeTx.opcode.imm32_2 = param2;
			break;
		default:
			fprintf(stderr, "CODE CTL opcode 0x%02x unknown\n", opcode);
			return false;
	}

	if (!debuggerUploadableCodeCmd(dbg, &codeTx, &codeRx))
		return false;
	*returnValP = codeRx.opcode.imm32;
	return true;
}

bool debuggerPowerCtlVariable(struct Debugger *dbg, uint32_t mv)
{
	unsigned char buf[sizeof(struct CommsPacket) + 2];
	struct CommsPacket *pkt = (struct CommsPacket*)buf;
	
	*(uint16_t*)(pkt->payload) = mv;
	
	return !(mv >> 16) && debuggerPowerCtlCommon(dbg, pkt, 2);	//reject voltages we cannot even represent in 16 bits
}

bool debuggerClockCtlSet(struct Debugger *dbg, uint32_t requestedClock, uint32_t *suppliedclockP)
{
	unsigned char *outBuf = alloca(debuggerGetMaxXferSz(dbg));
	struct CommsPacket *pktRx = (struct CommsPacket*)outBuf;
	unsigned char bufTx[sizeof(struct CommsPacket) + 4];
	struct CommsPacket *pktTx = (struct CommsPacket*)bufTx;
	uint32_t payloadSzOut;

	*(uint32_t*)(pktTx->payload) = requestedClock;
	pktTx->cmd = SWD_COMMS_CMD_SET_CLOCK;
	payloadSzOut = debuggerDoOneDevCmd(dbg, pktTx, 4, pktRx);
	if (payloadSzOut != 4) {
		fprintf(stderr, "CLK CTL CMD ERR\n");
		return false;
	}
	
	if (suppliedclockP)
		*suppliedclockP = *(uint32_t*)(pktRx->payload);
	
	return true;
}

static uint32_t debuggerRawSwdBusReadWrite(struct Debugger *dbg, uint8_t cmd, uint8_t ap, uint8_t a23, uint32_t *valP)
{
	uint8_t *buf = alloca(debuggerGetMaxXferSz(dbg));
	struct CommsPacket *pkt = (struct CommsPacket*)buf;
	struct SwdCommsWireBusPacket *rpp = (struct SwdCommsWireBusPacket*)pkt->payload;
	uint32_t ret;
	
	pkt->cmd = cmd;
	rpp->ap = ap;
	rpp->a23 = a23;
	rpp->val = *valP;
	
	ret = debuggerDoOneDevCmd(dbg, pkt, sizeof(struct SwdCommsWireBusPacket), pkt);
	if (ret != sizeof(struct SwdCommsWireBusPacket))
		return PACKET_RX_FAIL;
	
	*valP = rpp->val;
	
	return rpp->returnVal;
}

bool debuggerRawSwdBusReadWriteWithRetry(struct Debugger *dbg, uint8_t cmd, uint8_t ap, uint8_t a23, uint32_t *valP)
{
	uint32_t ret, nRetries = 0, valInitial = *valP;
	
	while(nRetries++ < 32) {
		
		*valP = valInitial;
		ret = debuggerRawSwdBusReadWrite(dbg, cmd, ap, a23, valP);
		
		if (ret == PACKET_RX_FAIL || ret == BUS_SWD_FAULT)
			return false;
		
		if (ret == BUS_SWD_ACK)
			return true;
		
		if (ret == BUS_SWD_WAIT)
			continue;
		
		if (ret == BUS_SWD_EMPTY) {
			fprintf(stderr, "ERROR: bus appears unconnected\n");
			return false;
		}
		
		//impossible
		fprintf(stderr, "ERROR: impossible return val from debugger: 0x%02X\n", ret);
		return false;
	}
	
	return false;
}

bool debuggerRawSwdBusRead(struct Debugger *dbg, uint8_t ap, uint8_t a23, uint32_t *valP)
{
	return debuggerRawSwdBusReadWriteWithRetry(dbg, SWD_COMMS_SWD_WIRE_BUS_R, ap, a23, valP);
}

bool debuggerRawSwdBusWrite(struct Debugger *dbg, uint8_t ap, uint8_t a23, uint32_t val)
{
	return debuggerRawSwdBusReadWriteWithRetry(dbg, SWD_COMMS_SWD_WIRE_BUS_W, ap, a23, &val);
}
