#ifndef _DEBUGGER_H_
#define _DEBUGGER_H_

#define _LARGE_PACKETS_
#include "types.h"
#include "../swdCommsPacket.h"

#define PACKET_RX_FAIL	0xFFFFFFFFUL

struct Debugger;

#define DEBUGGER_SNUM_GUARANTEED_ILLEGAL		((void*)1)			//guaranteed to never succeed being opened as a valid debugger

struct Debugger *debuggerOpen(const char *snum, bool verbose);		//snum can be null
void debuggerClose(struct Debugger *dbg);

bool debuggerInit(struct Debugger *dbg, bool verbose);				//basically reads debugger version and verifies we can talk to it

//very raw access to the debugger - only of use for fwupdate; return num bytes of payload or PACKET_RX_FAIL
uint32_t debuggerDoOneDevCmd(struct Debugger *dbg, const struct CommsPacket *pktTx, uint32_t txPayloadLen, struct CommsPacket *pktRx);

uint32_t debuggerGetMaxXferSz(struct Debugger *dbg);				//gets max payload sz for THIS debugger

bool debuggerCanSupportOnOffPowerCtl(const struct Debugger *dbg);		//see if old-style power contorl is supported. only useful after debuggerOpen() ha sbeen called
bool debuggerCanSupportVariablePowerCtl(const struct Debugger *dbg);	//see if new-style power contorl is supported. only useful after debuggerOpen() ha sbeen called
bool debuggerCanSupportVariableClockSpeed(const struct Debugger *dbg);	//see if clock speed contorl is possible in this debugger
bool debuggerIsSlowHw(const struct Debugger *dbg);						//see if this hardware is slow enough to warrant longer timeouts
bool debuggerHasResetPin(const struct Debugger *dbg);					//see if debugger has a dedicated reset pin
bool debuggerCanSupportUploadableCode(const struct Debugger *dbg);		//see if debugger can support uploadable code

bool debuggerPowerCtlSimple(struct Debugger *dbg, bool on);
bool debuggerPowerCtlVariable(struct Debugger *dbg, uint32_t mv);
bool debuggerClockCtlSet(struct Debugger *dbg, uint32_t requestedClock, uint32_t *suppliedclockP);

bool debuggerResetPinCtl(struct Debugger *dbg, bool high);
bool debuggerUploadableCodeInit(struct Debugger *dbg);
bool debuggerUploadableCodeRun(struct Debugger *dbg, uint32_t *regsInOutP);
bool debuggerUploadableCodeSendOpcode(struct Debugger *dbg, uint8_t opcode, uint32_t param1, uint32_t param2, uint32_t param3, uint32_t *returnValP);	//ctlCode is SWD_UPLOAD_CTL_CODE_*, others are params as seen in swdCommsPacket.h in order they are seen always

//both of these will retry a few times before giving up
bool debuggerRawSwdBusRead(struct Debugger *dbg, uint8_t ap, uint8_t a23, uint32_t *valP);
bool debuggerRawSwdBusWrite(struct Debugger *dbg, uint8_t ap, uint8_t a23, uint32_t val);

#endif