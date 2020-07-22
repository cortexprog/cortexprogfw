#ifndef _CPU_H_
#define _CPU_H_

#ifdef WIN32
#include <windows.h>
#endif
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <stdio.h>

#include "hidapi.h"
#include "../swdCommsPacket.h"
#include "../cortex.h"

#define CPU_OPTIMAL_MEM_XFER_WORDS		SWD_COMMS_MAX_XFER_WORDS
#define CPU_NUM_REGS_PER_SET			NUM_REGS


#ifdef _LARGE_PACKETS_
#define PACKET_RX_FAIL	0xffffffff
#else
#define PACKET_RX_FAIL	0xff
#endif

//pktRx == NULL if we do not want a reply, return PACKET_RX_FAIL for fail
typedef uint32_t (*DoOneSwdDevCmdF)(hid_device *handle, const struct CommsPacket *pktTx, uint32_t txPayloadLen, struct CommsPacket *pktRx);

bool cpuInit(hid_device *dev, DoOneSwdDevCmdF cmdF, uint32_t maxXferBytes);
bool cpuIdentify(uint32_t *loadSzP, uint32_t *loadAddrP, uint32_t *stageAddrP, char **nameP, char** scptFileName, FILE **scptFileHandle);	//nameP must be freed by caller

bool cpuMemReadEx(uint32_t base, uint32_t numWords, uint32_t *dst, bool silent/* do not write any errors to stdout*/);
bool cpuMemRead(uint32_t base, uint32_t numWords, uint32_t *dst);
bool cpuMemWrite(uint32_t base, uint32_t numWords, const uint32_t *src, bool withAck);

bool cpuRegsGet(uint8_t regSet, uint32_t *dst);
bool cpuRegsSet(uint8_t regSet, const uint32_t *src);

uint8_t cpuStop(void);
bool cpuReset(void);
bool cpuGo(void);
uint8_t cpuStep(void);
uint8_t cpuIsStoppedAndWhy(void);

bool cpuIsV7(void);
bool cpuHasFpu(void);

uint32_t cpuGetOptimalReadNumWords(void);
uint32_t cpuGetOptimalWriteNumWords(void);


//very very very low level
uint8_t swdLlWireBusRead(uint8_t ap, uint8_t a23, uint32_t *valP);
uint8_t swdLlWireBusWrite(uint8_t ap, uint8_t a23, uint32_t val);


//higher level
uint32_t cpuTraceLogRead(uint32_t addr, uint8_t *buf);	//buf better be >= COMMS_PAYLOAD_MAX_SIZE in len


//sort of averarching
bool cpuPwrSet(bool on);

#endif
