#ifdef WIN32
#include <windows.h>
#endif
#include <sys/stat.h>
#include <sys/time.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <wchar.h>
#include <errno.h>
#include <stdio.h>
#define _LARGE_PACKETS_
#include "gdbServer.h"
#include "scriptfiles.h"
#include "hidapi.h"
#include "cpu.h"


#define MY_VID		0x4744
#define MY_PID		0x5043

static bool mNeedPadding = true;	//better safe then sorry by default
static bool mHavePowerCtrl = false;


#define SPECIAL_STEP_PRE_CPU_INIT							0	//called before we try attach
#define SPECIAL_STEP_POST_CPU_INIT							1	//called after we tried attach
#define SPECIAL_STEP_GET_CPU_QUIRKS							2	//called to get info about quirks

//for SPECIAL_STEP_POST_CPU_INIT
#define SPECIAL_OPMASK_POSTINIT_RETRY_INIT					0x00000001	//retry init now

//for SPECIAL_STEP_GET_CPU_QUIRKS
#define SPECIAL_OPMASK_GET_QUIRKS_RESET_DISCONNECTS			0x00000001	//we cnanot reset this cpu and expect togo on

typedef void (*SpecialFuncF)(uint32_t step, void **self/*modifiable*/, uint32_t *opmaskP, uint32_t addr, uint32_t len);


static struct SwdCommsVerInfoRespPacketV3 verInfo = {
		.maxXferBytes = MODULAR_MAX_PACKET_SZ,
		.flags = 0,			//other values defaults so older ver packets work...
		.hwType = SWD_COMMS_HW_TYP_UNKNOWN,
	};

//this assumes it runs on a little-endian CPU that supports unaligned accesses. Too bad, I am lazy

//return payload len of RXed packet or PACKET_RX_FAIL on error
static uint32_t doOneSwdDevCmd(hid_device *handle, const struct CommsPacket *pktTx, uint32_t txPayloadLen, struct CommsPacket *pktRx)
{
	unsigned char bufTx[verInfo.maxXferBytes + 1];
	struct CommsPacket *pktTxReal = (struct CommsPacket*)(bufTx + 1);
	int nRetries = 8, rxPayloadLen, rxLen;
	
	*pktTxReal = *pktTx;
	memcpy(pktTxReal->payload, pktTx->payload, txPayloadLen);
	pktTxReal->crc = commsPacketCalcCrc(pktTxReal, txPayloadLen);
	bufTx[0] = 0;
	
	if (mNeedPadding && !((txPayloadLen + sizeof(struct CommsPacket)) % 8)) {
		pktTxReal->cmd |= CMD_ORR_PADDED;
		txPayloadLen++;
	}
	
	while(1) {
		
		if (!nRetries--)
			return PACKET_RX_FAIL;
		
	//	fprintf(stderr, "sending pkt %d\n", pktTxReal->cmd);
		
		if (hid_write(handle, bufTx, 1 + txPayloadLen + sizeof(struct CommsPacket)) < 0)
			return PACKET_RX_FAIL;
		
		if (!pktRx)
			return 0;
		
		while ((rxLen = hid_read_timeout(handle, (char*)pktRx, verInfo.maxXferBytes, 500)) > 0) {
			
	//		fprintf(stderr, "getting pkt %d in resp to pkt %d with total len %u\n", pktRx->cmd, pktTxReal->cmd, rxLen);
			
			if (pktRx->cmd != (pktTxReal->cmd &~ CMD_ORR_PADDED))
				continue;
			
			if (rxLen < sizeof(struct CommsPacket))
				continue;
			
			rxPayloadLen = rxLen - sizeof(struct CommsPacket);
			
			if (commsPacketCalcCrc(pktRx, rxPayloadLen) != pktRx->crc)
				break;
			
	//		fprintf(stderr, "crc pass\n");
			return rxPayloadLen;
		}
	}
}

static hid_device* openHidDev(const char *snum)
{
	wchar_t snumWbuf[128], *snumW = snumWbuf;
	struct hid_device_info *devs, *dev;
	bool found = false;
	hid_device* ret;
	int nDevs = 0;
	
	//convert given snum to wide if given
	if (snum)
		snumWbuf[mbstowcs(snumW, snum, sizeof(snumWbuf) / sizeof(*snumWbuf) - 1)] = 0;
	else
		snumW = NULL;
	
	devs = hid_enumerate (MY_VID, MY_PID);
 	for (dev = devs; dev; dev = dev->next) {
 		
 		nDevs++;
 		
 		//exact match found
 		if (dev->serial_number && snumW && !wcscmp(dev->serial_number, snumW))
 			found = true;
 	}
 	
 	//if no snum given and only one device exists, assume it is the one
 	if (nDevs == 1 && !snumW)
 		found = true;
 	
 	if (!found) {
 		
 		bool showList = true;
 		
 		if (snumW)
 			fprintf(stderr, "Requested device '%s' not found.", snum);
 		else if (nDevs)
 			fprintf(stderr, "More than one device found.");
 		else {
 			fprintf(stderr, "No devices found.");
 			showList = false;
 		}
 		
 		if (showList) {
 		
	 		fprintf(stderr, " Available %u device(s):\n", nDevs);
	 		
	 		for (dev = devs; dev; dev = dev->next) {
	 		
	 			fprintf(stderr, "\t'%S'\n", dev->serial_number ? dev->serial_number : L"UNKNOWN SERIAL NUMBER");
		 	}
		 }
		 
		 fprintf(stderr, "\n");
 	}
 	
	hid_free_enumeration(devs);
	
	if (!found)
		return NULL;
	
	ret = hid_open(MY_VID, MY_PID, snumW);
	
	if (!ret)
		fprintf(stderr, "Failed to find device\n");
	
	
	return ret;
}

static void showregs(void)
{
	uint32_t regs[CPU_NUM_REGS_PER_SET];
	int i;
	
	if (!cpuRegsGet(SWD_COMMS_REG_SET_BASE, regs))
		return;
	
	for (i = 0; i < CPU_NUM_REGS_PER_SET; i++)
		fprintf(stderr, "R%02u = 0x%08X\n", i, regs[i]);
}


//At least linux will queue up bufers from device even while we're not expecting them. This will drain them.
static void drainOsBuf(hid_device *handle)
{
	struct CommsPacket pkt;
	
	while (hid_read_timeout(handle, (char*)&pkt, sizeof(struct CommsPacket), 100) > 0);
}

static bool setWatchpoint(uint32_t idx, uint32_t addr, uint32_t size, uint32_t type)
{
	uint32_t vals[3] = {addr, size, type};
	
	return cpuMemWrite(0xE0001020 + idx * 0x10, 3, vals, true);
}

static bool runScript(uint32_t base, uint32_t ofst, uint32_t paramR0, uint32_t paramR1, uint32_t paramR2, uint32_t paramR3)
{
	uint32_t regs[CPU_NUM_REGS_PER_SET], checkval, addr = base + ofst, tBit = (1ul << 24);
	
	
	if(!cpuMemRead(addr, 1, &checkval)) {
		fprintf(stderr, "RUN: RM err\n");
		return false;
	}
	
	if (!checkval) {
	//	fprintf(stderr, "script offset 0x%08X empty\n", ofst);
		return true;
	}
	
	cpuStep();		//do not ask...
	cpuStop();		//still do not ask (if we were in LOCKUP, step can act as continue on some chips)
	
	if(!cpuRegsGet(SWD_COMMS_REG_SET_BASE, regs)) {
		fprintf(stderr, "RUN: RG err\n");
		return false;
	}
	
	regs[0] = paramR0;
	regs[1] = paramR1;
	regs[2] = paramR2;
	regs[3] = paramR3;
	regs[15] = addr | 1;
	
	if(!cpuRegsSet(SWD_COMMS_REG_SET_BASE, regs)) {
		fprintf(stderr, "RUN: RS err\n");
		return false;
	}
	
	
	if(!cpuRegsGet(SWD_COMMS_REG_SET_CTRL, regs)) {
		fprintf(stderr, "RUN: RG2 err\n");
		return false;
	}
	
	if (!(regs[CORTEX_REG_XPSR % NUM_REGS] & tBit)) {	//t bit must be set else chip will be unhappy
	
		regs[CORTEX_REG_XPSR % NUM_REGS] |= tBit;
		
		if(!cpuRegsSet(SWD_COMMS_REG_SET_CTRL, regs)) {
			fprintf(stderr, "RUN: RS2 err\n");
			return false;
		}
	}
	
	while(1) {
		uint8_t sta;
		
		if (!cpuGo()) {
			fprintf(stderr, "RUN: GO err\n");
			return false;
		}
		
		while (CORTEX_W_FAIL == (sta = cpuIsStoppedAndWhy()));
		
		
		if (sta & CORTEX_W_DWPT) {
			if(!cpuRegsGet(SWD_COMMS_REG_SET_BASE, regs)) {
				fprintf(stderr, "RUN: RG.s err\n");
				return false;
			}
			
			if ((regs[15] &~ 7) == ((base + 0x0C) &~ 7)) {
				
	//			fprintf(stderr, "syscall %d\n", regs[0]);
				sta &=~ CORTEX_W_DWPT;
				
				switch (regs[0]) {
					case 0:	//set bpt/wpt
						setWatchpoint(1, regs[1], regs[2], regs[3]);
						break;
					case 1://clear bpt
						setWatchpoint(1, 0, 0, 0);
						break;
					default:
						fprintf(stderr, "unknown syscall\n");
						break;
				}
				regs[15] = regs[14];
				
				if(!cpuRegsSet(SWD_COMMS_REG_SET_BASE, regs)) {
					fprintf(stderr, "RUN: RS.s err\n");
					return false;
				}
				continue;
			}
		}
		break;
	}
	
	return true;
}

static uint64_t getTicks(void)
{
#ifdef WIN32
	return GetTickCount64();
#else
	struct timeval tv;
	if(gettimeofday(&tv, NULL))
		return 0;
	return (unsigned long)((tv.tv_sec * 1000ul) + (tv.tv_usec / 1000ul));
#endif
}

static bool verifyRemoteVer(hid_device *handle)
{
	uint8_t buf[verInfo.maxXferBytes], rspSz;
	struct CommsPacket *pkt = (struct CommsPacket*)buf;
	wchar_t snum[129] = {0,};
	static const char *hwNames[] = {
		[SWD_COMMS_HW_TYP_AVR_PROTO] = "AVR simple CortexProg",
		[SWD_COMMS_HW_TYP_EFM] = "ARM CortexProg",
		[SWD_COMMS_HW_TYP_UNKNOWN] = "Unknown CortexProg type",
	};
	const char *hwName;
	
	pkt->cmd = SWD_COMMS_CMD_VER_INFO;
	rspSz = doOneSwdDevCmd(handle, pkt, 0, pkt);
	
	if (rspSz == sizeof(struct SwdCommsVerInfoRespPacketV1) || rspSz == sizeof(struct SwdCommsVerInfoRespPacketV2) || rspSz == sizeof(struct SwdCommsVerInfoRespPacketV3)) {
		
		//because they overlay nicely, this is doable
		memcpy(&verInfo, pkt->payload, rspSz);
	}
	else {
		fprintf(stderr, "VER GET INFO ERR\n");
		return false;
	}
	
	if (verInfo.hwType >= sizeof(hwNames) / sizeof(*hwNames) || !hwNames[verInfo.hwType])
		hwName = "Unrecognized CortesProg type";
	else
		hwName = hwNames[verInfo.hwType];
	
	if (hid_get_serial_number_string(handle, snum, sizeof(snum) / sizeof(*snum) - 1))
		snum[0] = 0;

	fprintf(stderr, "SWD HW: '%s' ver. %u, FW: %u.%u.%u.%u, flags 0x%08X, maxXfer: %u, serial '%S'\n",
		hwName, verInfo.hwVer,
		(uint8_t)(verInfo.swdAppVer >> 24), (uint8_t)(verInfo.swdAppVer >> 16), (uint8_t)(verInfo.swdAppVer >> 8), (uint8_t)(verInfo.swdAppVer >> 0),
		verInfo.flags, verInfo.maxXferBytes, snum);
	
	mNeedPadding = !!(verInfo.flags & USB_FLAGS_NEED_PACKET_PADDING);
	mHavePowerCtrl = !!(verInfo.flags & PWR_FLAG_PWR_CTRL_ON_OFF);
	
	return true;
}


static bool setCpsrTbitAndDisableInterrupts(void)
{
	uint32_t regs[CPU_NUM_REGS_PER_SET];
	
	if (!cpuRegsGet(SWD_COMMS_REG_SET_CTRL, regs))  {
		fprintf(stderr, "CHIP REG GET FAIL\n");
		return false;
	}
	regs[0] |= 1ul << 24;	//set t bit
	regs[3] |= 1;			//disable interrupts
	if (!cpuRegsSet(SWD_COMMS_REG_SET_CTRL, regs))  {
		fprintf(stderr, "CHIP REG SET FAIL\n");
		return false;
	}
	
	return true;
}

static bool swdWireBusWrite(uint8_t ap, uint8_t a23, uint32_t val)
{
	uint8_t v;
	
	do {
		v = swdLlWireBusWrite(ap, a23, val);
	} while (v == 2);	//while reply is WAIT, retry
	
	return v == 1;		//was it an ACK? :)
}

static bool swdWireBusRead(uint8_t ap, uint8_t a23, uint32_t *valP)
{
	uint8_t v;
	
	do {
		v = swdLlWireBusRead(ap, a23, valP);
	} while (v == 2);	//while reply is WAIT, retry
	
	return v == 1;		//was it an ACK? :)
}

#define SWD_ADDR_SELECT_W		2
#define SWD_ADDR_RDBUFF_R		3

#define SWD_REG_BANK_MASK		0xF0
#define SWD_REG_SEL_MASK		0x0C
#define SWD_REG_SEL_SHIFT		2


#define AP_ADDR_IDR				0xFC


#define ADDR_DHCSR				0xE000EDF0
#define ADDR_DCRSR				0xE000EDF4
#define ADDR_DCRDR				0xE000EDF8
#define ADDR_DEMCR				0xE000EDFC

#define ADDR_DWT_PCSR			0xE000101C

#define DHCSR_BITS_KEY			0xa05f0000
#define DHCSR_BITS_WRITEABLE	0x0000ffff
#define DHCSR_BIT_LOCKUP		0x00080000
#define DHCSR_BIT_STEP			0x00000004
#define DHCSR_BIT_HALT			0x00000002
#define DHCSR_BIT_DEBUGEN		0x00000001

#define NORDIC_CTRL_AP_ADDR_RESET			0x00
#define NORDIC_CTRL_AP_ADDR_ERASEALL		0x04
#define NORDIC_CTRL_AP_ADDR_ERASEALLSTATUS	0x08
#define NORDIC_CTRL_AP_ADDR_APPROTECTSTATUS	0x0C
#define NORDIC_CTRL_AP_IDR_VAL				0x02880000ul


#define FREESCALE_MDM_AP_IDR_ADDR_STATUS	0x00
#define FREESCALE_MDM_AP_IDR_ADDR_CONTROL	0x04
#define FREESCALE_MDM_AP_IDR_VAL			0x001c0020ul

#define EFM32HG_AAP_CMD						0xF0E00000
#define EFM32HG_AAP_CMDKEY					0xF0E00004
#define EFM32HG_AAP_STATUS					0xF0E00008
#define EFM32HG_AAP_IDR						0xF0E000FC

#define EFM32HG_AAP_IDR_VAL					0x16E60001
#define EFM32HG_AAP_CMDKEY_VAL				0xCFACC118
#define EFM32HG_AAP_CMD_VAL_ERASE			1
#define EFM32HG_AAP_CMD_VAL_RESET			2
#define EFM32HG_AAP_STATUS_ERASING			1

#define STM31F1_DBGMCU_IDCODE_ADDR			0xE0042000
#define STM31F1_DBGMCU_IDCODE_MASK			0x00000FD1
#define STM31F1_DBGMCU_IDCODE_VAL			0x00000410
#define STM32F1_FLASH_KEYR_ADDR				0x40022004
#define STM32F1_FLASH_OPTKEYR_ADDR			0x40022008
#define STM32F1_FLASH_SR_ADDR				0x4002200C
#define STM32F1_FLASH_CR_ADDR				0x40022010
#define STM32F1_FLASH_SR_VAL_BUSY			0x00000001
#define STM32F1_FLASH_CR_VAL_OPTWRE			0x00000200
#define STM32F1_FLASH_CR_VAL_STRT			0x00000040
#define STM32F1_FLASH_CR_VAL_OPTER			0x00000020
#define STM32F1_FLASH_CR_VAL_OPTPG			0x00000010
#define STM32F1_FLASH_OPTKEYR_VAL1			0x45670123
#define STM32F1_FLASH_OPTKEYR_VAL2			0xCDEF89AB
#define STM32F1_FLASH_RDPRT_KEY_VAL			0x000000A5
#define STM32F1_OTBYTES_RDP_ADDR			0x1FFFF800
#define STM32F1_STAGING_AREA				0x20000000	//we'll overwrite 4 bytes there
#define STM32F1_STAGING_VALUE				0xe7fe8001	//"STRH r1, [r0] \n 1: B 1b"

#define CORTEX_ADDR_AIRCR					0xE000ED0C
#define CORTEX_AIRCR_VAL_RESET				0x05FA0004

#define PSOC4_ADDR_TST_MODE					0x40030014
#define PSOC4_TST_MODE_VAL_ON				0x80000000
#define PSOC4_TST_MODE_VAL_OFF				0x00000000

#define PSOC41xx_42xx_ADDR_CPUSS_BASE		0x40000000
#define PSOC_OTHERS_ADDR_CPUSS_BASE			0x40100000

#define PSOC4xxx_ADDROFST_CPUSS_SYSREQ		0x00000004
#define PSOC4xxx_SYSREQ_BIT_SYSCALL_REQ		0x80000000
#define PSOC4xxx_SYSREQ_BIT_ROM_ACCESS_EN	0x20000000
#define PSOC4xxx_SYSREQ_BIT_PRIVILEDGED		0x10000000
#define PSOC4xxx_SYSREQ_BIT_ROM_ACCESS		0x08000000
#define PSOC4xxx_ADDROFST_CPUSS_SYSARG		0x00000008

#define PSOC4xxx_CMD_VAL_WRITE_PROTECTION	0x0000000D
#define PSOC4xxx_CMD_ARG_WRITE_PROTECTION	0x0001E0B6
#define PSOC4xxx_CMD_RAM_WRITE_PROTECTION	false

#define PSOC4xxx_CMD_VAL_ERASE_ALL			0x0000000A
#define PSOC4xxx_CMD_ARG_ERASE_ALL			0x0000DDB6
#define PSOC4xxx_CMD_RAM_ERASE_ALL			true

#define PSOC4xxx_CMD_VAL_SILICON_ID			0x00000000
#define PSOC4xxx_CMD_ARG_SILICON_ID			0x0000D3B6
#define PSOC4xxx_CMD_RAM_SILICON_ID			false

#define PSOC4xxx_CMD_VAL_CLOCK_UP			0x00000015
#define PSOC4xxx_CMD_ARG_CLOCK_UP			0x0000E8B6
#define PSOC4xxx_CMD_RAM_CLOCK_UP			false


static bool swdSelectApAndRegBank(uint32_t apNum, uint32_t reg)		//make sure proper setup fro accesing a given reg on a givne AP is made
{
	static uint32_t selectedAp = 0xffffffff;
	uint32_t desiredVal = (apNum << 24) | (reg & SWD_REG_BANK_MASK);
	
	if (desiredVal == selectedAp)
		return true;
	
	if (!swdWireBusWrite(0, SWD_ADDR_SELECT_W, desiredVal))
		return false;
	
	selectedAp = desiredVal;
	return true;
}

static bool swdApRegRead(uint32_t ap, uint32_t reg, uint32_t *valP)
{
	uint32_t regSel = (reg & SWD_REG_SEL_MASK) >> SWD_REG_SEL_SHIFT;
	
	//setup for AP selection and bank selection
	if (!swdSelectApAndRegBank(ap, reg))
		return false;
	
	//request read of a given red
	if (!swdWireBusRead(1, regSel, valP))
		return false;
	
	//let it cycle through the AP & get the result
	if (!swdWireBusRead(0, SWD_ADDR_RDBUFF_R, valP))
		return false;
	
	//we're done
	return true;
}

static bool swdApRegWrite(uint32_t ap, uint32_t reg, uint32_t val)
{
	uint32_t regSel = (reg & SWD_REG_SEL_MASK) >> SWD_REG_SEL_SHIFT;
	
	//setup for AP selection and bank selection
	if (!swdSelectApAndRegBank(ap, reg))
		return false;
	
	//request write of a given red
	if (!swdWireBusWrite(1, regSel, val))
		return false;
	
	//let it cycle through the AP & get the result
	if (!swdWireBusRead(0, SWD_ADDR_RDBUFF_R, &val))
		return false;
	
	//we're done
	return true;
}

static int findApByIdMask(uint32_t mask, uint32_t expected)	//return index or negative on error
{
	uint32_t i, val;
	
	for (i = 0; i < 256; i++) {
		
		if (!swdApRegRead(i, AP_ADDR_IDR, &val)) {
			fprintf(stderr, "Failed to read AP[%3u].IDR\n", i);
			return -1;
		}
		
		if (!val)
			break;
		
		if ((val & mask) == expected)	//found it
			return i;
	}
	
	return -1;
}

static void sfNrfUnlock(uint32_t step, void **self, uint32_t *opmaskP, uint32_t addr, uint32_t len)
{
	uint64_t ticks;
	uint32_t val;
	int i;
	
	switch(step) {
		case SPECIAL_STEP_PRE_CPU_INIT:
			//*opmaskP |= SPECIAL_OPMASK_PREINIT_ALLOW_CPU_INIT_FAIL;		//allow CPU init to fail - this is expected as AHB will not be found
			break;
		
		case SPECIAL_STEP_POST_CPU_INIT:
		
			*self = NULL;				//do not retry this forever
			i = findApByIdMask(0xffffffff, NORDIC_CTRL_AP_IDR_VAL);
			if (i >= 0) {
				
				//CTRL-AP for nRF52 found
				
				fprintf(stderr, "CTRL-AP found at index %d\n", i);
				
				ticks = getTicks();
				
				if (!swdApRegWrite(i, NORDIC_CTRL_AP_ADDR_ERASEALL, 1)) {
					fprintf(stderr, "Failed to write NORDIC_CTRL_AP[%3u].ERASEALL\n", i);
					return;
				}

				fprintf(stderr, "ERASE started...\n");
				
				//wait
				do{
					
					//request read of ERASEALLSTATUS
					if (!swdApRegRead(i, NORDIC_CTRL_AP_ADDR_ERASEALLSTATUS, &val)) {
						fprintf(stderr, "Failed to read NORDIC_CTRL_AP[%3u].NORDIC_CTRL_AP_ADDR_ERASEALLSTATUS\n", i);
						return;
					}
					
				}while(val & 1);
				
				ticks = getTicks() - ticks;
				
				fprintf(stderr, "ERASE done... (took %llu ms)\n", (unsigned long long)ticks);
				
				if (!swdApRegWrite(i, NORDIC_CTRL_AP_ADDR_RESET, 1)) {
					fprintf(stderr, "Failed to write 1 to NORDIC_CTRL_AP[%3u].RESET\n", i);
					return;
				}
				
				if (!swdApRegWrite(i, NORDIC_CTRL_AP_ADDR_RESET, 0)) {
					fprintf(stderr, "Failed to write 0 to NORDIC_CTRL_AP[%3u].RESET\n", i);
					return;
				}
				
				*opmaskP |= SPECIAL_OPMASK_POSTINIT_RETRY_INIT;
			}
	}
}

static void sfFreescaleUnlock(uint32_t step, void **self, uint32_t *opmaskP, uint32_t addr, uint32_t len)
{
	uint64_t ticks;
	uint32_t val;
	int i;
	
	switch(step) {
		case SPECIAL_STEP_PRE_CPU_INIT:
			//*opmaskP |= SPECIAL_OPMASK_PREINIT_ALLOW_CPU_INIT_FAIL;		//allow CPU init to fail - this is expected as AHB will not be found
			break;
		
		case SPECIAL_STEP_POST_CPU_INIT:
		
			*self = NULL;				//do not retry this forever
			i = findApByIdMask(0xffffffff, FREESCALE_MDM_AP_IDR_VAL);
			if (i >= 0) {
				
				//MDM-AP for freescale chip found
				
				fprintf(stderr, "MDM-AP found at index %d\n", i);
				
				//request read of ERASEALLSTATUS
				if (!swdApRegRead(i, FREESCALE_MDM_AP_IDR_ADDR_STATUS, &val)) {
					fprintf(stderr, "Failed to read FREESCALE_MDM_AP_IDR_ADDR_STATUS\n");
					return;
				}
				
				fprintf(stderr, " MDM_AP_STATUS = 0x%08x\n", val);
				
				//request read of ERASEALLSTATUS
				if (!swdApRegRead(i, FREESCALE_MDM_AP_IDR_ADDR_CONTROL, &val)) {
					fprintf(stderr, "Failed to read FREESCALE_MDM_AP_IDR_ADDR_CONTROL\n");
					return;
				}
				
				fprintf(stderr, " FREESCALE_MDM_AP_IDR_ADDR_CONTROL = 0x%08x\n", val);
				
				fprintf(stderr, " erasing...\n");
				ticks = getTicks();
				
				//put chip in reset
				if (!swdApRegWrite(i, FREESCALE_MDM_AP_IDR_ADDR_CONTROL, 0x08)) {
					fprintf(stderr, "Failed to write MDM_AP_CONTROL 2\n");
					return;
				}
				
				//keep chip in reset and erase flash
				if (!swdApRegWrite(i, FREESCALE_MDM_AP_IDR_ADDR_CONTROL, 0x09)) {
					fprintf(stderr, "Failed to write MDM_AP_CONTROL 2\n");
					return;
				}
				
				//release reset
				if (!swdApRegWrite(i, FREESCALE_MDM_AP_IDR_ADDR_CONTROL, 0x01)) {
					fprintf(stderr, "Failed to write MDM_AP_CONTROL 3\n");
					return;
				}
				
				
				//wait
				do {
					
					if (!swdApRegRead(i, FREESCALE_MDM_AP_IDR_ADDR_CONTROL, &val)) {
						fprintf(stderr, "Failed to read MDM_AP_CONTROL\n");
						return;
					}
					
				} while (val & 1);
				
				ticks = getTicks() - ticks;
				fprintf(stderr, " ERASE done... (took %llu ms)\n", (unsigned long long)ticks);
				
				
				if (!swdApRegRead(i, FREESCALE_MDM_AP_IDR_ADDR_STATUS, &val)) {
					fprintf(stderr, "Failed to read FREESCALE_MDM_AP_IDR_ADDR_STATUS\n");
					return;
				}
				
				fprintf(stderr, " MDM_AP_STATUS = 0x%08x\n", val);
				
				//release chip reset
				if (!swdApRegWrite(i, FREESCALE_MDM_AP_IDR_ADDR_CONTROL, 0x00)) {
					fprintf(stderr, "Failed to write MDM_AP_CONTROL 3\n");
					return;
				}
				
				fprintf(stderr, " MDM_AP_CONTROL = 0x%08x\n", val);
				
				*opmaskP |= SPECIAL_OPMASK_POSTINIT_RETRY_INIT;
			}
	}
}

static void sfEfm32hgUnlock(uint32_t step, void **self, uint32_t *opmaskP, uint32_t addr, uint32_t len)
{
	uint64_t ticks;
	uint32_t val;
	
	switch(step) {
		case SPECIAL_STEP_PRE_CPU_INIT:
			//*opmaskP |= SPECIAL_OPMASK_PREINIT_ALLOW_CPU_INIT_FAIL;		//allow CPU init to fail - this is expected as AHB will not be found
			break;
		
		case SPECIAL_STEP_POST_CPU_INIT:
		
			*self = NULL;				//do not retry this forever
			if (!cpuMemRead(EFM32HG_AAP_IDR, 1, &val)) {
				fprintf(stderr, "Failed to read EFM32HG_AAP_IDR\n");
				return;
			}
			if (val != EFM32HG_AAP_IDR_VAL) {
				fprintf(stderr, "EFM32HG_AAP_IDR val mismatch (0x%08X != 0x%08X)\n", val, EFM32HG_AAP_IDR_VAL);
				return;
			}
			fprintf(stderr, "EFM32HG AAP found\n");
			val = EFM32HG_AAP_CMDKEY_VAL;
			if (!cpuMemWrite(EFM32HG_AAP_CMDKEY, 1, &val, true)) {
				fprintf(stderr, "Failed to write EFM32HG_AAP_CMDKEY 1\n");
				return;
			}
			fprintf(stderr, " erasing...\n");
			ticks = getTicks();
			val = EFM32HG_AAP_CMD_VAL_ERASE;
			if (!cpuMemWrite(EFM32HG_AAP_CMD, 1, &val, true)) {
				fprintf(stderr, "Failed to write EFM32HG_AAP_CMD 1\n");
				return;
			}
			val = 0;
			if (!cpuMemWrite(EFM32HG_AAP_CMDKEY, 1, &val, true)) {
				fprintf(stderr, "Failed to write EFM32HG_AAP_CMDKEY 2\n");
				return;
			}
			
			//wait
			do {
				
				if (!cpuMemRead(EFM32HG_AAP_STATUS, 1, &val)) {
					fprintf(stderr, "Failed to read EFM32HG_AAP_STATUS\n");
					return;
				}
			} while (val & EFM32HG_AAP_STATUS_ERASING);
			
			ticks = getTicks() - ticks;
			fprintf(stderr, " ERASE done... (took %llu ms)\n", (unsigned long long)ticks);
			
			fprintf(stderr, " resetting...\n");
			val = EFM32HG_AAP_CMDKEY_VAL;
			if (!cpuMemWrite(EFM32HG_AAP_CMDKEY, 1, &val, true)) {
				fprintf(stderr, "Failed to write EFM32HG_AAP_CMDKEY 3\n");
				return;
			}
			val = EFM32HG_AAP_CMD_VAL_RESET;
			if (!cpuMemWrite(EFM32HG_AAP_CMD, 1, &val, true)) {
				fprintf(stderr, "Failed to write EFM32HG_AAP_CMD 2\n");
				return;
			}
			val = 0;
			if (!cpuMemWrite(EFM32HG_AAP_CMDKEY, 1, &val, true)) {
				fprintf(stderr, "Failed to write EFM32HG_AAP_CMDKEY 4\n");
				return;
			}
			
			*opmaskP |= SPECIAL_OPMASK_POSTINIT_RETRY_INIT;
			break;
	}
}

static void stm32f1Unlock(uint32_t step, void **self, uint32_t *opmaskP, uint32_t addr, uint32_t len)
{
	uint64_t ticks;
	uint32_t val, regs[CPU_NUM_REGS_PER_SET];
	
	switch(step) {
		case SPECIAL_STEP_PRE_CPU_INIT:
			//do not allow CPU init to fail - it should work out fine!
			break;
		
		case SPECIAL_STEP_POST_CPU_INIT:
		
			*self = NULL;				//do not retry this forever
			
			//verify this is the device we expect
			fprintf(stderr, "Read IDCODE...");
			if (!cpuMemRead(STM31F1_DBGMCU_IDCODE_ADDR, 1, &val)) {
				fprintf(stderr, "Failed to read STM31F1_DBGMCU_IDCODE_ADDR\n");
				return;
			}
			fprintf(stderr, "0x%08x", val);
			if ((val & STM31F1_DBGMCU_IDCODE_MASK) != STM31F1_DBGMCU_IDCODE_VAL) {
				fprintf(stderr, "...Not expected ID - refusing to go on\n");
				return;
			}
			fprintf(stderr, "\n");

			//unlock flash writing
			fprintf(stderr, "Unlocking flash...");
			val = STM32F1_FLASH_OPTKEYR_VAL1;
			if (!cpuMemWrite(STM32F1_FLASH_KEYR_ADDR, 1, &val, true)) {
				fprintf(stderr, "Failed to write STM32F1_FLASH_KEYR_ADDR key 1\n");
				return;
			}
			val = STM32F1_FLASH_OPTKEYR_VAL2;
			if (!cpuMemWrite(STM32F1_FLASH_KEYR_ADDR, 1, &val, true)) {
				fprintf(stderr, "Failed to write STM32F1_FLASH_KEYR_ADDR key 2\n");
				return;
			}
			fprintf(stderr, "DONE\n");
			
			//unlock option byte writing
			fprintf(stderr, "Unlocking option bytes...");
			val = STM32F1_FLASH_OPTKEYR_VAL1;
			if (!cpuMemWrite(STM32F1_FLASH_OPTKEYR_ADDR, 1, &val, true)) {
				fprintf(stderr, "Failed to write STM32F1_FLASH_OPTKEYR_ADDR key 1\n");
				return;
			}
			val = STM32F1_FLASH_OPTKEYR_VAL2;
			if (!cpuMemWrite(STM32F1_FLASH_OPTKEYR_ADDR, 1, &val, true)) {
				fprintf(stderr, "Failed to write STM32F1_FLASH_OPTKEYR_ADDR key 2\n");
				return;
			}
			if (!cpuMemRead(STM32F1_FLASH_CR_ADDR, 1, &val)) {
				fprintf(stderr, "Failed to read STM32F1_FLASH_CR_ADDR\n");
				return;
			}
			if (!(val & STM32F1_FLASH_CR_VAL_OPTWRE)) {
				fprintf(stderr, "FLASH_CR OPTWRE bit not set - we cannot go on\n");
				return;
			}
			fprintf(stderr, "DONE\n");
			
			//erase option bytes
			fprintf(stderr, "Erasing option bytes...");
			val = STM32F1_FLASH_CR_VAL_OPTER | STM32F1_FLASH_CR_VAL_OPTWRE;
			if (!cpuMemWrite(STM32F1_FLASH_CR_ADDR, 1, &val, true)) {
				fprintf(stderr, "Failed to write STM32F1_FLASH_CR_ADDR.1\n");
				return;
			}
			ticks = getTicks();
			val = STM32F1_FLASH_CR_VAL_OPTER | STM32F1_FLASH_CR_VAL_STRT | STM32F1_FLASH_CR_VAL_OPTWRE;
			if (!cpuMemWrite(STM32F1_FLASH_CR_ADDR, 1, &val, true)) {
				fprintf(stderr, "Failed to write STM32F1_FLASH_CR_ADDR.2\n");
				return;
			}
			do {
				
				if (!cpuMemRead(STM32F1_FLASH_SR_ADDR, 1, &val)) {
					fprintf(stderr, "Failed to read STM32F1_FLASH_SR_ADDR\n");
					return;
				}
			} while (val & STM32F1_FLASH_SR_VAL_BUSY);
			ticks = getTicks() - ticks;
			fprintf(stderr, "DONE after %llu ms\n", (unsigned long long)ticks);
			
			//upload and set up erase code
			fprintf(stderr, "Erasing flash...");
			if (CORTEX_W_FAIL == cpuStop()) {
				fprintf(stderr, "Failed to stop CPU\n");
				return;
			}
			regs[ 0] = STM32F1_OTBYTES_RDP_ADDR;
			regs[ 1] = STM32F1_FLASH_RDPRT_KEY_VAL;
			regs[15] = STM32F1_STAGING_AREA | 1;
			if (!cpuRegsSet(SWD_COMMS_REG_SET_BASE, regs)) {
				fprintf(stderr, "Failed to set CPU regs\n");
				return;
			}

			val = STM32F1_STAGING_VALUE;	//"STRH r1, [r0] \n 1: B 1b"
			if (!cpuMemWrite(STM32F1_STAGING_AREA, 1, &val, true)) {
				fprintf(stderr, "Failed to write to staging area\n");
				return;
			}
			
			//make sure we can execute
			if (!setCpsrTbitAndDisableInterrupts())
				return;
			
			val = STM32F1_FLASH_CR_VAL_OPTPG | STM32F1_FLASH_CR_VAL_OPTWRE;
			if (!cpuMemWrite(STM32F1_FLASH_CR_ADDR, 1, &val, true)) {
				fprintf(stderr, "Failed to write STM32F1_FLASH_CR_ADDR.3\n");
				return;
			}
			
			ticks = getTicks();
			if (!cpuGo()) {
				fprintf(stderr, "Failed to launch CPU\n");
				return;
			}
			do {
				if (!cpuMemRead(STM32F1_FLASH_SR_ADDR, 1, &val)) {
					fprintf(stderr, "Failed to read STM32F1_FLASH_SR_ADDR\n");
					return;
				}
			} while (val & STM32F1_FLASH_SR_VAL_BUSY);
			ticks = getTicks() - ticks;
			fprintf(stderr, "DONE after %llu ms\n", (unsigned long long)ticks);

			cpuReset();
			*opmaskP |= SPECIAL_OPMASK_POSTINIT_RETRY_INIT;
			break;
	}
}

static uint32_t psoc4identify(uint8_t *chipFamilyID)	//return cpuss base, optionally chip family id as well
{
	uint32_t val;
	
	//see which cpu we have (SROM has a cpuid we can read but SROM may be inaccesible). We'll use the romtable
	if (!cpuMemRead(0xF0000FE0, 1, &val)) {
		fprintf(stderr, "Failed to read ROM PID REG\n");
		return 0;
	}
	if (val >> 8) {
		fprintf(stderr, "ROM PID REG invalid\n");
		return 0;
	}
	
	if (chipFamilyID)
		*chipFamilyID = val;
	
	
	//from: http://www.cypress.com/file/324226/download
	switch (val) {
		case 0x93:		//PSoC4100/PSoC4200
			fprintf(stderr, "PSoC4100/4200 detected\n");
			return PSOC41xx_42xx_ADDR_CPUSS_BASE;
		case 0x9E:		//CYBLE-022001-00, CY8C4247LQI 
			fprintf(stderr, "CYBL10x6x (PSoC4000-BLE?) detected\n");
			return PSOC_OTHERS_ADDR_CPUSS_BASE;
		case 0x9A:		//PsoC4000
			fprintf(stderr, "PSoC4000 detected\n");
			return PSOC_OTHERS_ADDR_CPUSS_BASE;
		case 0xA1:		//PSoC4100M/PSoC4200M
			fprintf(stderr, "PSoC4100M/4200M detected\n");
			return PSOC_OTHERS_ADDR_CPUSS_BASE;
		case 0xA9:		//PSoC4000S
			fprintf(stderr, "PSoC4000S detected\n");
			return PSOC_OTHERS_ADDR_CPUSS_BASE;
		default:
			fprintf(stderr, "Unknown PSoC 0x%02X detected. Assuming CPUSS 0x%08X\n", val, PSOC_OTHERS_ADDR_CPUSS_BASE);
			return PSOC_OTHERS_ADDR_CPUSS_BASE;
	}
	/*
		other known vals:
		AE, A3, AA - PSoC_BLE of some type
		
		69 - PSoC5
		A0 - 4200L
		A7 - 4200D ? 
		AB - 4100S
		AC - analog coprocessor (aka PSoC4400)
	*/
}

static void psoc4init(uint32_t step, void **self, uint32_t *opmaskP, uint32_t addr, uint32_t len)	//all of this only works on 4100/4200. All others we're SOL on
{
	uint32_t val;
	uint64_t ticks;
	
	switch(step) {
		case SPECIAL_STEP_PRE_CPU_INIT:
			break;
			
		case SPECIAL_STEP_POST_CPU_INIT:
		
			//wait till we can read PSOC4_ADDR_TST_MODE
			ticks = getTicks();
			do {
				if (cpuMemReadEx(PSOC4_ADDR_TST_MODE, 1, &val, true)) {
					break;
				}
				
				if (getTicks() - ticks > 10000) {
					fprintf(stderr, "TST wait timeout\n");
					return;
				}
				
			} while (1);
			ticks = getTicks() - ticks;
			break;
		
		case SPECIAL_STEP_GET_CPU_QUIRKS:
		
			*opmaskP |= SPECIAL_OPMASK_GET_QUIRKS_RESET_DISCONNECTS;
			break;
	}
}

static void showsr(void)
{
       uint32_t val, regs[16];
       
       
       if (!cpuRegsGet(SWD_COMMS_REG_SET_BASE, regs)) {
               fprintf(stderr, "Failed to get regs 2\n");
               return;
       }
       
       for (val = 0; val < 16; val++)
               fprintf(stderr, "R%02d = 0x%08x\n", val, regs[val]);
               
               
       if (!cpuRegsGet(SWD_COMMS_REG_SET_CTRL, regs)) {
               fprintf(stderr, "Failed to get regs 2\n");
               return;
       }
       
       fprintf(stderr, "CPSR = 0x%08x\n", regs[0]);
       
       if (!cpuMemRead(PSOC_OTHERS_ADDR_CPUSS_BASE + PSOC4xxx_ADDROFST_CPUSS_SYSREQ, 1, &val)) {
               fprintf(stderr, "failed to read CPUSS_SYSREQ 3\n");
               return;
       }
       fprintf(stderr, "sr 0x%08x\n\n", val);
}

static bool psoc4unlockStep(uint32_t cpussBase, uint32_t cmd, uint32_t param, bool paramInReg, const char *stepName, uint64_t timeout, uint32_t *retCodeP)
{
	uint32_t val, ramAddrToUse = 0x20000000, stackPtrToUse = 0x20000800, regs[16];
	uint64_t ticks;
	bool ret;
	
	if (!cpuRegsGet(SWD_COMMS_REG_SET_BASE, regs)) {
		fprintf(stderr, "Failed to get regs arg for unlock step '%s'\n", stepName);
		return false;
	}
	regs[13] = stackPtrToUse;
	if (!cpuRegsSet(SWD_COMMS_REG_SET_BASE, regs)) {
		fprintf(stderr, "Failed to set regs arg for unlock step '%s'\n", stepName);
		return false;
	}
	
	if (!paramInReg) {
		if (!cpuMemWrite(ramAddrToUse, 1, &param, true)) {
			fprintf(stderr, "Failed to write arg to memory for unlock step '%s'\n", stepName);
			return false;
		}
		param = ramAddrToUse;
	}
	
	if (!cpuMemWrite(cpussBase + PSOC4xxx_ADDROFST_CPUSS_SYSARG, 1, &param, true)) {
		fprintf(stderr, "Failed to write SYSARG for unlock step '%s'\n", stepName);
		return false;
	}
	val = PSOC4xxx_SYSREQ_BIT_SYSCALL_REQ + cmd;	//arg to unprotect
	if (!cpuMemWrite(cpussBase + PSOC4xxx_ADDROFST_CPUSS_SYSREQ, 1, &val, true)) {
		fprintf(stderr, "Failed to write cmd for unlock step '%s'\n", stepName);
		return false;
	}
	
	ticks = getTicks();
	if (!cpuGo()) {
		fprintf(stderr, "Failed to launch CPU arg for unlock step '%s'\n", stepName);
		return false;
	}
	do {
		if (!cpuMemRead(cpussBase + PSOC4xxx_ADDROFST_CPUSS_SYSREQ, 1, &val)) {
			fprintf(stderr, "Failed to read CPUSS_SYSREQ arg for unlock step '%s'\n", stepName);
			return false;
		}
		
		if (getTicks() - ticks > timeout)
			return false;
		
	} while (val & (PSOC4xxx_SYSREQ_BIT_SYSCALL_REQ | PSOC4xxx_SYSREQ_BIT_PRIVILEDGED));
	ticks = getTicks() - ticks;
	
	if (!cpuMemRead(cpussBase + PSOC4xxx_ADDROFST_CPUSS_SYSARG, 1, &val)) {
		fprintf(stderr, "Failed to read CPUSS_SYSARG arg for unlock step '%s'\n", stepName);
		return false;
	}
	ret = (val >> 28) == 0x0A;
	if (retCodeP)
		*retCodeP = val;
	
	fprintf(stderr, "Unlock step '%s' DONE after %llu ms. Result: %s (0x%08X)\n", stepName, (unsigned long long)ticks, ret ? "SUCCESS" : "FAILURE", val);

	return ret;
}

static void psoc4unlock(uint32_t step, void **self, uint32_t *opmaskP, uint32_t addr, uint32_t len)
{
	uint32_t val, cpussBase;
	uint8_t family;
	
	//we must do the usual psoc4 init always
	psoc4init(step, self, opmaskP, addr, len);

	//only if it succeeds all the way can we go on
	if (step != SPECIAL_STEP_GET_CPU_QUIRKS || !(*opmaskP & SPECIAL_OPMASK_GET_QUIRKS_RESET_DISCONNECTS))	//at this spot we'll take over :)
		return;
	
	//never again will we try
	*self = NULL;
	
	//stop cpu
	if (cpuStop() == CORTEX_W_FAIL) {
		fprintf(stderr, "CHIP STOP FAIL\n");
	//	return;
	}
	
	if (!setCpsrTbitAndDisableInterrupts()) {
		fprintf(stderr, "T bit setting failed\n");
//		return;
	}
	
	cpussBase = psoc4identify(&family);	//return cpuss base, optionally chip family id as well
	if (cpussBase) {
		fprintf(stderr, "PSoC identified by ROM as having CPUSS base 0x%08X and family 0x%02X\n", cpussBase, family);
	}
	else if (psoc4unlockStep(PSOC_OTHERS_ADDR_CPUSS_BASE, PSOC4xxx_CMD_VAL_SILICON_ID, PSOC4xxx_CMD_ARG_SILICON_ID, !PSOC4xxx_CMD_RAM_SILICON_ID, "identify_4000", 10000, &val)) {
		
		if((val & 0xffff) == 0xa040)
			fprintf(stderr, "PSoC4000 detected\n");
		else if((val & 0xf000) == 0x1000)
			fprintf(stderr, "PSoC4100M/4200M detected\n");
		else
			fprintf(stderr, "Unknown PSoC 0x%08X detected with CPUSS @ 0x%08X. Will do our best\n", val, PSOC_OTHERS_ADDR_CPUSS_BASE);
		
		cpussBase = PSOC_OTHERS_ADDR_CPUSS_BASE;
	}
	else if (psoc4unlockStep(PSOC41xx_42xx_ADDR_CPUSS_BASE, PSOC4xxx_CMD_VAL_SILICON_ID, PSOC4xxx_CMD_ARG_SILICON_ID, !PSOC4xxx_CMD_RAM_SILICON_ID, "identify_4100", 10000, &val)) {
	
		if((val & 0xffff) == 0x416)
			fprintf(stderr, "PSoC4100 detected\n");
		else
			fprintf(stderr, "Unknown PSoC 0x%08X detected with CPUSS @ 0x%08X. Will do our best\n", val, PSOC41xx_42xx_ADDR_CPUSS_BASE);
		
		cpussBase = PSOC41xx_42xx_ADDR_CPUSS_BASE;
	}
	else {
		fprintf(stderr, "Chip did not respond to CPUID request on either of the known CPUSS addresses. Giving up!\n");
		return;
	}
	
	//this is safe to ignore errors on
	(void)psoc4unlockStep(cpussBase, PSOC4xxx_CMD_VAL_CLOCK_UP, PSOC4xxx_CMD_ARG_CLOCK_UP, !PSOC4xxx_CMD_RAM_CLOCK_UP, "clock up", -1ll, NULL);

	//we do not bother loading latches as this will unlock everything anyways
	if (psoc4unlockStep(cpussBase, PSOC4xxx_CMD_VAL_WRITE_PROTECTION, PSOC4xxx_CMD_ARG_WRITE_PROTECTION, !PSOC4xxx_CMD_RAM_WRITE_PROTECTION, "unlock", -1ll, NULL) &&
		psoc4unlockStep(cpussBase, PSOC4xxx_CMD_VAL_ERASE_ALL, PSOC4xxx_CMD_ARG_ERASE_ALL, !PSOC4xxx_CMD_RAM_ERASE_ALL, "erase", -1ll, NULL))
		cpuReset();
}

static void psoc4romDumpWord(uint32_t step, void **self, uint32_t *opmaskP, uint32_t addr, uint32_t len)
{
	const uint32_t ram_code_location = 0x20000080ul, ram_ofst_trigger = 0x00, ram_ofst_handler = 0x02, ram_loc_stack_top = 0x20000700;	//needs at least 8 words
	uint32_t cpussBase, exploitBase;
	uint32_t regs[16], val, i;
	uint64_t ticks;
	uint8_t family;
	
	/*	==== upload ====
	
		setup:
			str r0, [r1]
		
		handler:
			ldr r6, =cpussBase
			ldr r5, =PSOC4xxx_SYSREQ_BIT_SYSCALL_REQ + PSOC4xxx_SYSREQ_BIT_PRIVILEDGED
			ldr r0, [r6, #4]
			bic r0, r5
			str r0, [r6, #4]
		3:
			bkpt
			b 3b
	*/
	
	if ((addr & 3) || (len != 4)) {
		fprintf(stderr, "This requires a word-aligned address and length of 4\n");
		fprintf(stderr, "This requires a word-aligned address\n");
		return;
	}
	
	//we must do the usual psoc4 init always
	psoc4init(step, self, opmaskP, addr, len);

	//only if it succeeds all the way can we go on
	if (step != SPECIAL_STEP_GET_CPU_QUIRKS || !(*opmaskP & SPECIAL_OPMASK_GET_QUIRKS_RESET_DISCONNECTS))	//at this spot we'll take over :)
		return;
	
	
	//never again will we try
	*self = NULL;
	
	//stop cpu
	if (cpuStop() == CORTEX_W_FAIL) {
		fprintf(stderr, "CHIP STOP FAIL\n");
		return;
	}
	
	if (!setCpsrTbitAndDisableInterrupts()) {
		fprintf(stderr, "T bit setting failed\n");
		return;
	}
	
	
	cpussBase = psoc4identify(&family);	//return cpuss base, optionally chip family id as well
	if (!cpussBase)
		return;
	
	switch (family) {
		case 0x93:		//PSoC4100/PSoC4200
			exploitBase = 0x100001bc;
			break;
		case 0x9E:		//CYBL10x6x (PSoC4000-BLE?)
			exploitBase = 0x100001ae;
			break;
		case 0x9A:		//PsoC4000
			exploitBase = 0x10000184;
			break;
		case 0xA1:		//PSoC4100M/PSoC4200M
			exploitBase = 0x100001b4;
			break;
		case 0xA9:		//PSoC4000S
			exploitBase = 0x100001a0;
			break;
		default:
			fprintf(stderr, "Exploit unlikely\n");
			return;
	}


	uint32_t upload[] = {
		0x4E036008, 0x68704D03, 0x607043A8, 0xE7FDBE00,
		cpussBase, PSOC4xxx_SYSREQ_BIT_SYSCALL_REQ + PSOC4xxx_SYSREQ_BIT_PRIVILEDGED,
	};
	
	//upload code
	if (!cpuMemWrite(ram_code_location, sizeof(upload) / sizeof(*upload), upload, true)) {
		fprintf(stderr, "Failed to upload code\n");
		return;
	}
	
	//prepare DEMCR
	if (!cpuMemRead(ADDR_DEMCR, 1, &val)) {
		fprintf(stderr, "Failed to read DEMCR\n");
		return;
	}
	val |= 1 << 24;	//DWT on
	if (!cpuMemWrite(ADDR_DEMCR, 1, &val, true)) {
		fprintf(stderr, "Failed to write DEMCR\n");
		return;
	}
	
	//prepare DHCSR
	if (!cpuMemRead(ADDR_DHCSR, 1, &val)) {
		fprintf(stderr, "Failed to read DHCSR 1\n");
		return;
	}
	val = DHCSR_BITS_KEY | (val & DHCSR_BITS_WRITEABLE) | DHCSR_BIT_DEBUGEN;	//enable debugging
	if (!cpuMemWrite(ADDR_DHCSR, 1, &val, true)) {
		fprintf(stderr, "Failed to write DHCSR 1\n");
		return;
	}
	
	//prepare to run it (stack for exception return)
	if (!cpuRegsGet(SWD_COMMS_REG_SET_BASE, regs)) {
		fprintf(stderr, "Failed to get regs 1\n");
		return;
	}
	regs[0] = PSOC4xxx_SYSREQ_BIT_SYSCALL_REQ;	//cpuid call
	regs[1] = cpussBase + PSOC4xxx_ADDROFST_CPUSS_SYSREQ;
	regs[13] = ram_loc_stack_top;
	regs[15] = ram_code_location + ram_ofst_trigger + 1;
	if (!cpuRegsSet(SWD_COMMS_REG_SET_BASE, regs)) {
		fprintf(stderr, "Failed to set regs 1\n");
		return;
	}
	
	for (i = 0; i < 2; i++) {
		if (CORTEX_W_FAIL == cpuStep()) {
			fprintf(stderr, "step %u of exploit part 1 fail\n", i);
			return;
		}
	}

	if (!cpuRegsGet(SWD_COMMS_REG_SET_BASE, regs)) {
		fprintf(stderr, "Failed to get regs 2\n");
		return;
	}
	if ((regs[15] & 0xf0000000) != 0x10000000) {
		fprintf(stderr, "after step pc was 0x%08X\n", regs[15]);
		return;
	}
	
	fprintf(stderr, "execution in rom achieved\n");
	
	regs[ 0] = ram_loc_stack_top;
	regs[ 1] = ram_loc_stack_top;
	regs[ 2] = cpussBase;
	regs[ 3] = 0x00000000ul;
	regs[ 4] = PSOC4xxx_SYSREQ_BIT_ROM_ACCESS_EN + PSOC4xxx_SYSREQ_BIT_PRIVILEDGED;
	regs[13] = ram_loc_stack_top;
	regs[15] = exploitBase | 1;	//first exploit in the chain
	
	if (!cpuRegsSet(SWD_COMMS_REG_SET_BASE, regs)) {
		fprintf(stderr, "Failed to set regs 2\n");
		return;
	}
		
	//sysarg regs gets our desired address
	val = addr - 0x0c;
	if (!cpuMemWrite(cpussBase + PSOC4xxx_ADDROFST_CPUSS_SYSARG, 1, &val, true)) {
		fprintf(stderr, "Failed to write exploit CPUSS_SYSARG\n");
		return;
	}
	
	val = ram_code_location + ram_ofst_handler + 1;	//ret addr (where we'll jump when done)
	if (!cpuMemWrite(ram_loc_stack_top + 4, 1, &val, true)) {
		fprintf(stderr, "Failed to write exploit [SP + 4]\n");
		return;
	}
	
	
	fprintf(stderr, "EXPLOIT #2: setup\n");
	
	//go
	if (!cpuMemRead(ADDR_DHCSR, 1, &val)) {
		fprintf(stderr, "failed to read DHCSR 3\n");
		return;
	}
	
	val = DHCSR_BITS_KEY | (val & DHCSR_BITS_WRITEABLE &~ DHCSR_BIT_STEP &~ DHCSR_BIT_HALT) | DHCSR_BIT_DEBUGEN;
	if (!cpuMemWrite(ADDR_DHCSR, 1, &val, true)) {
		fprintf(stderr, "Failed to write DHCSR 3\n");
		return;
	}

	fprintf(stderr, "EXPLOIT #2: going\n");

	
	ticks = getTicks();
	while(1) {
		
		if (getTicks() - ticks > 1000) {
			fprintf(stderr, "exploit #2 (ROP) timeout\n");
			return;
		}

		if (!cpuMemRead(ADDR_DHCSR, 1, &val)) {
			fprintf(stderr, "failed to read DHCSR 3\n");
			continue;
		}
		if ((val & 3) == 3) {
			fprintf(stderr, "EXPLOIT #2: halted\n");
			break;
		}
	}
	
	if (!cpuRegsGet(SWD_COMMS_REG_SET_BASE, regs)) {
		fprintf(stderr, "Failed to get regs 3\n");
		return;
	}
	
	fprintf(stderr, "[0x%08x] =  0x%08x\n", addr, regs[4]);
	
	(void)cpuReset();
}

static void psoc4miniromDumpWord(uint32_t step, void **self, uint32_t *opmaskP, uint32_t addr, uint32_t len)
{
	const uint32_t ram_code_location = 0x20000080ul, ram_code = 0x5088, ram_loc_stack_top = 0x20000700, sycallNo = 0x1234, maxSteps = 32, maxOfst = 0x7C;
	uint32_t cpussBase, targetReg, targetPc, targetNextPc, regs[16], val, i, j;
	int8_t offsets[8];

	//upload: str r0, [r1, r2]
	
	//we must do the usual psoc4 init always
	psoc4init(step, self, opmaskP, addr, len);

	//only if it succeeds all the way can we go on
	if (step != SPECIAL_STEP_GET_CPU_QUIRKS || !(*opmaskP & SPECIAL_OPMASK_GET_QUIRKS_RESET_DISCONNECTS))	//at this spot we'll take over :)
		return;
	
	if ((addr & 3) || (len & 3)) {
		fprintf(stderr, "This requires a word-aligned address and length\n");
		return;
	}
	
	//never again will we try
	*self = NULL;
	
	//stop cpu
	if (cpuStop() == CORTEX_W_FAIL) {
		fprintf(stderr, "CHIP STOP FAIL\n");
		return;
	}
	
	if (!setCpsrTbitAndDisableInterrupts()) {
		fprintf(stderr, "T bit setting failed\n");
		return;
	}
	
	cpussBase = psoc4identify(NULL);
	if (!cpussBase)
		return;

	//upload code
	if (!cpuMemWrite(ram_code_location, 1, &ram_code, true)) {
		fprintf(stderr, "Failed to upload code\n");
		return;
	}
	
	//prepare DEMCR
	if (!cpuMemRead(ADDR_DEMCR, 1, &val)) {
		fprintf(stderr, "Failed to read DEMCR\n");
		return;
	}
	val |= 1 << 24;	//DWT on
	if (!cpuMemWrite(ADDR_DEMCR, 1, &val, true)) {
		fprintf(stderr, "Failed to write DEMCR\n");
		return;
	}
	
	//prepare DHCSR
	if (!cpuMemRead(ADDR_DHCSR, 1, &val)) {
		fprintf(stderr, "Failed to read DHCSR 1\n");
		return;
	}
	val = DHCSR_BITS_KEY | (val & DHCSR_BITS_WRITEABLE) | DHCSR_BIT_DEBUGEN;	//enable debugging
	if (!cpuMemWrite(ADDR_DHCSR, 1, &val, true)) {
		fprintf(stderr, "Failed to write DHCSR 1\n");
		return;
	}
	
	//prepare to run it (stack for exception return)
	if (!cpuRegsGet(SWD_COMMS_REG_SET_BASE, regs)) {
		fprintf(stderr, "Failed to get regs 1\n");
		return;
	}
	for (i = 0; i < 16; i++)
		regs[i] = 0x5a5a0000 + (i + 1);	//placeholders that are easy to see
	
	val = cpussBase + PSOC4xxx_ADDROFST_CPUSS_SYSREQ;		//address (we'll split it 2 ways to avoid detecting our own address as a base)
	
	regs[0] = PSOC4xxx_SYSREQ_BIT_SYSCALL_REQ | sycallNo;	//some invalid syscall that we can easily identify the number of
	regs[1] = (val / 2) | 1;	//just ot make sure it is not even aligned
	regs[2] = val - regs[1];
	regs[13] = ram_loc_stack_top;
	regs[15] = ram_code_location + 1;
	if (!cpuRegsSet(SWD_COMMS_REG_SET_BASE, regs)) {
		fprintf(stderr, "Failed to set regs 1\n");
		return;
	}
	
	for (i = 0; i < maxSteps; i++) {
		
		targetPc = regs[15];
		if (CORTEX_W_FAIL == cpuStep()) {
			fprintf(stderr, "step %u of exploit part 1 fail\n", i);
			return;
		}
		
		fprintf(stderr, "Step %u ... ", i);
		if (!cpuRegsGet(SWD_COMMS_REG_SET_BASE, regs)) {
			fprintf(stderr, "Failed to get regs 1\n");
			return;
		}
		targetNextPc = regs[15];
		if ((regs[15] & 0xfff00000) != 0x10000000) {
			fprintf(stderr, "PC 0x%08X not in SROM\n", regs[15]);
			continue;
		}
		
		//loads can only happen ot low regs
		for (targetReg = 0; targetReg < 8 && regs[targetReg] != PSOC4xxx_SYSREQ_BIT_SYSCALL_REQ + PSOC4xxx_SYSREQ_BIT_ROM_ACCESS_EN + sycallNo; targetReg++);
		
		if (targetReg == 8) {
			fprintf(stderr, "Syscall Num not found in any register\n");
			continue;
		}
		
		fprintf(stderr, "Syscall Num found in R%d\n", targetReg);
		
		for (j = 0; j < 8; j++) {	//low regs only
		
			int32_t ofst;
			
			offsets[j] = -1;
			
			if (j == targetReg)
				continue;
			
			ofst = cpussBase + PSOC4xxx_ADDROFST_CPUSS_SYSREQ - regs[j];
			
			if (ofst < 0 || ofst > maxOfst)
				continue;
			
			offsets[j] = ofst;
			fprintf(stderr, "  Potential base reg: R%u (0x%08X, ofst 0x%02X)\n", j, regs[j], ofst);
		}
		break;
	}
	
	if (i == maxSteps) {
		fprintf(stderr, "No luck after reaching maximum steps. Giving up\n");
		return;
	}
	
	for (i = 0; i < 8 && offsets[i] < 0; i++);
	if (i == 8) {
		fprintf(stderr, "No offsets found. Giving up\n");
		return;
	}
	
	fprintf(stderr, "  Will exploit suspected load at 0x%08X with targets: ", targetPc);
	for (i = 0; i < 8; i++) {
		if (offsets[i] < 0)
			continue;
			
		if (offsets[i])
			fprintf(stderr, " [R%u, #0x%02X]", i, offsets[i]);
		else
			fprintf(stderr, " [R%u]", i);
	}
	fprintf(stderr, "\n");
	
	if (isatty(fileno(stdout))) {
		fprintf(stderr, "Refusing to write binary data to console. Please redirect stdout to a file\n");
		return;
	}
	
	for (i = addr; i < addr + len; i += 4) {
		
		fprintf(stderr, "\r  Dumping ... 0x%08X", i);
		for (j = 0; j < 8; j++) {
			
			if (offsets[j] < 0)
				continue;
			
			regs[j] = i - offsets[j];
		}
		regs[15] = targetPc + 1;
		if (!cpuRegsSet(SWD_COMMS_REG_SET_BASE, regs)) {
			fprintf(stderr, " ... Failed to set regs 2\n");
			return;
		}
		if (CORTEX_W_FAIL == cpuStep()) {
			fprintf(stderr, " ... Failed to step\n");
			return;
		}
		if (!cpuRegsGet(SWD_COMMS_REG_SET_BASE, regs)) {
			fprintf(stderr, " ... Failed to get regs 2\n");
			return;
		}
		if (regs[15] != targetNextPc) {
			fprintf(stderr, " ... PC of 0x%08X not as expected (0x%08X)\n", regs[15], targetNextPc);
			return;
		}
		
		val = regs[targetReg];
		
		for (j = 0; j < 4; j++, val >>= 8)
			putc(val & 0xff, stdout);
	}
	fprintf(stderr, "\nSUCCESS\n");
	

	return;
}

//following from 4200M
//for multi-macro chips the threshold is stored in [param, 8], [param, 9], etc.... instead of [param, 2]
//also data is written to offset 0x18 and read from both macros no matter what

static bool psoc4dataRecoverySyscall(uint32_t cpussBase, uint32_t ram_code_location, uint32_t ram_cmd_location, uint32_t ram_loc_stack_top, uint32_t cmd, uint32_t arg0, uint32_t arg1, uint32_t arg2)
{
	uint32_t regs[16], val, sta;
	
	regs[13] = ram_loc_stack_top;
	regs[14] = ram_code_location + 1;
	regs[15] = ram_code_location + 1;
	if (!cpuRegsSet(SWD_COMMS_REG_SET_BASE, regs)) {
		fprintf(stderr, "Failed to set regs\n");
		return false;
	}
	
	val = 0x0000d3b6 + (cmd << 8) + arg0;		
	if (cmd != 0x15 && cmd != 0x0b && cmd != 0x0B && cmd != 0x00) {	//param(s) in ram
					
		if (!cpuMemWrite(ram_cmd_location + 0, 1, &val, true)) {
			fprintf(stderr, "Failed to write cmd[0]\n");
			return false;
		}
		
		val = arg1;
		if (!cpuMemWrite(ram_cmd_location + 4, 1, &val, true)) {
			fprintf(stderr, "Failed to write cmd[1]\n");
			return false;
		}
		
		val = arg2;
		if (!cpuMemWrite(ram_cmd_location + 8, 1, &val, true)) {
			fprintf(stderr, "Failed to write cmd[2]\n");
			return false;
		}
		
		val = ram_cmd_location;
	}
	if (!cpuMemWrite(cpussBase + PSOC4xxx_ADDROFST_CPUSS_SYSARG, 1, &val, true)) {
		fprintf(stderr, "Failed to write sysarg\n");
		return false;
	}
	
	val = PSOC4xxx_SYSREQ_BIT_SYSCALL_REQ | cmd;
	if (!cpuMemWrite(cpussBase + PSOC4xxx_ADDROFST_CPUSS_SYSREQ, 1, &val, true)) {
		fprintf(stderr, "Failed to write sysreq\n");
		return false;
	}

	cpuGo();
	
	while (CORTEX_W_FAIL == (sta = cpuIsStoppedAndWhy()));

	if (!cpuMemRead(cpussBase + PSOC4xxx_ADDROFST_CPUSS_SYSARG, 1, &val)) {
		fprintf(stderr, "Failed to read sysarg\n");
		return false;
	}
	
	if ((val >> 28) != 0x0A) {
		fprintf(stderr, "Failed to complete syscall. err = 0x%08X\n", val);
		return false;
	}
	
	return true;
}


//ideas:
//	bimodalization
//	checksums ARE readable befor eprotect
//	have entire process in this func (unprotect and do this) so we can collect checksums and check them



//following from 4200M"
//  for multi-macro chips the threshold is stored in [param, 8], [param, 9], etc.... instead of [param, 2]
//  also data is written to offset 0x0c and read from both macros no matter what


//idea: characterize each bit for what values we get from it after it being a zero or a one before a chip erase
//then decide on what the bits were (using initial analog data_ using thie characterization
//this may produce better results. we'll be able to test some of this using collected data now
//but not all (we're not collcting data on zeroes)

#define METHOD_1


static void psoc4dataRecovery(uint32_t step, void **self, uint32_t *opmaskP, uint32_t addr, uint32_t len)
{
	const uint32_t ram_code_location = 0x20000040ul, ram_cmd_location = 0x20000080ul, ram_code[] = {0xbebebebe}, ram_loc_stack_top = 0x20000700;
	const uint32_t numSamples = 4096, nTurns = 2, rowsz = 128;
	const uint32_t analogRangeMin = 0x45, analogRangeMax = 0x72;
	//const uint32_t data_read_ofst = 0x0c /*code would have me belive this should be 0x18*/;	//0x08 for PSoC4100
	const uint32_t data_read_ofst = 0x08;	// for PSoC4100
	uint32_t cpussBase, regs[16], val, i, j, t, q, sta, readvals[rowsz / 4], tnd, testNum;
	int32_t *flipPoint[nTurns];
	uint32_t checksums[(len + (rowsz - 1)) / rowsz];
	uint64_t ticks;

	//upload: str r0, [r1, r2]; bkpt
	
	//we must do the usual psoc4 init always
	psoc4init(step, self, opmaskP, addr, len);

	//only if it succeeds all the way can we go on
	if (step != SPECIAL_STEP_GET_CPU_QUIRKS || !(*opmaskP & SPECIAL_OPMASK_GET_QUIRKS_RESET_DISCONNECTS))	//at this spot we'll take over :)
		return;
	
	if ((addr & 3) || (len & 3) || !len) {
		fprintf(stderr, "This requires a word-aligned address and length\n");
		return;
	}
	
	//never again will we try
	*self = NULL;
	for (i = 0; i < nTurns; i++) {
		flipPoint[i] = calloc(len, 8 * sizeof(uint32_t));
		if (!flipPoint[i]) {
			fprintf(stderr, "alloc %u fail\n", i);
			return;
		}
	}
	
	//stop cpu
	if (cpuStop() == CORTEX_W_FAIL) {
		fprintf(stderr, "CHIP STOP FAIL\n");
		return;
	}
	
	if (!setCpsrTbitAndDisableInterrupts()) {
		fprintf(stderr, "T bit setting failed\n");
		return;
	}
	
	cpussBase = psoc4identify(NULL);
	if (!cpussBase)
		return;

	fprintf(stderr, "assuming CPUSS @ 0x%08X\n", cpussBase);

	//upload code
	if (!cpuMemWrite(ram_code_location, sizeof(ram_code) / sizeof(uint32_t), ram_code, true)) {
		fprintf(stderr, "Failed to upload code\n");
		return;
	}
	
	//prepare DEMCR
	if (!cpuMemRead(ADDR_DEMCR, 1, &val)) {
		fprintf(stderr, "Failed to read DEMCR\n");
		return;
	}
	val |= 1 << 24;	//DWT on
	if (!cpuMemWrite(ADDR_DEMCR, 1, &val, true)) {
		fprintf(stderr, "Failed to write DEMCR\n");
		return;
	}
	
	//prepare DHCSR
	if (!cpuMemRead(ADDR_DHCSR, 1, &val)) {
		fprintf(stderr, "Failed to read DHCSR 1\n");
		return;
	}
	val = DHCSR_BITS_KEY | (val & DHCSR_BITS_WRITEABLE) | DHCSR_BIT_DEBUGEN;	//enable debugging
	if (!cpuMemWrite(ADDR_DHCSR, 1, &val, true)) {
		fprintf(stderr, "Failed to write DHCSR 1\n");
		return;
	}
	
	//goto just_skipit;
	
	fprintf(stderr, "clock up (ignore all errors)...\n");
	(void)psoc4dataRecoverySyscall(cpussBase, ram_code_location, ram_cmd_location, ram_loc_stack_top, 0x15, 0, 0, 0);
	fprintf(stderr,"DONE\n");
	
	
	
	
	
	for (i = addr; i < addr + len; i += sizeof(readvals)) {
		fprintf(stderr, "\rcollecting checksums ... 0x%08X ... ", i);
		if (!psoc4dataRecoverySyscall(cpussBase, ram_code_location, ram_cmd_location, ram_loc_stack_top, 0x0b, (i / sizeof(readvals)) << 16, 0, 0))
			return;
		
		if (!cpuMemRead(cpussBase + PSOC4xxx_ADDROFST_CPUSS_SYSARG, 1, &val)) {
			fprintf(stderr, "Failed to read sysarg\n");
		}
		
		val &= 0x00ffffff;
		checksums[(i - addr) / sizeof(readvals)] = val;
		fprintf(stderr, "0x%06X", val);
	}
	fprintf(stderr,"\n");
	
	fprintf(stderr, "Erasing chip...");
	if (!psoc4dataRecoverySyscall(cpussBase, ram_code_location, ram_cmd_location, ram_loc_stack_top, 0x0A, 0, 0, 0))
		return;
	fprintf(stderr,"DONE\n");
	
	
	tnd = 0;
	
	
	ticks = getTicks();
	for (testNum = 0; testNum < nTurns; testNum++) {
		
		
		for (j = 0; j < numSamples; j++) {
			
			for (i = addr; i < addr + len; i += sizeof(readvals)) {
				
				
				
				for (t = analogRangeMin; t <= analogRangeMax; t++) {
					
					uint64_t elapsed = getTicks() - ticks;
					uint64_t ppm;
					
					
					tnd++;
					ppm = 1000000ull * sizeof(readvals) * tnd / numSamples / len / (analogRangeMax - analogRangeMin + 1) / nTurns;
					if (!ppm)
						ppm = 1;
					
					fprintf(stderr, "\rS %3u/%3u  A 0x%08X t 0x%02X    %6lluppm  (%llu seconds past, %llu seconds total)    ",
						j, numSamples, i, t, (unsigned long long)ppm,
						(unsigned long long)elapsed / 1000, (unsigned long long)elapsed * 1000 / ppm);
					
					//flag is 0, threshold is 0x00ff0000 byte
					if (!psoc4dataRecoverySyscall(cpussBase, ram_code_location, ram_cmd_location, ram_loc_stack_top, 0x0c, (t << 16), i, t | (t << 8) /* for 4200M */))
						return;
					
					if (!cpuMemRead(ram_cmd_location + data_read_ofst, sizeof(readvals) / sizeof(uint32_t), readvals)) {
						fprintf(stderr, "Failed to read values\n");
						return;
					}
					
					
	//				for (q = 0; q < sizeof(readvals) / 4; q++)
	//					fprintf(stderr, "V_m0x%02X[0x%02x] = 0x%08x   \n", t, q * 4, readvals[q]);
					
					for (q = 0; q < sizeof(readvals) * 8; q++) {
						
						uint32_t mask = 1 << (q % 32);
						uint32_t arrIdx = q / 32;
						
						if (readvals[arrIdx] & mask)
							flipPoint[testNum][q + i - addr]++;
					}
				}
			}
		}
		
		for (i = 0; i < len * 8; i++)
			fprintf(stderr, "flippt[%u][0x%02X] = 0x%06X\n", testNum, i, flipPoint[testNum][i]);
		
		if (testNum == nTurns - 1)
			break;
		
		for (q = 0; q < 1; q++) {
		
		#ifdef METHOD_1
			val = 0;
		#else
			val = (testNum == 0) ? 0xffffffff : 0;	//read 0 is data, read 1 is what ones look like erased, read 2 is what zeroes look like erased
		#endif
			for (j = 0; j < sizeof(readvals) / sizeof(uint32_t); j++)
				readvals[j] = val;
		
			
			for (i = addr; i < addr + len; i += sizeof(readvals)) {
				
				fprintf(stderr, "writing 0x%08Xs @ 0x%08X\n", val, i);
				
				if (!cpuMemWrite(ram_cmd_location + 8, sizeof(readvals) / sizeof(uint32_t), readvals, true)) {
					fprintf(stderr, "Failed to write values\n");
					return;
				}
				
				if (!psoc4dataRecoverySyscall(cpussBase, ram_code_location, ram_cmd_location, ram_loc_stack_top, 0x04, 0, sizeof(readvals) - 1, val/*as else we'll ovefwrite our first val*/))
					return;
				
				if (!psoc4dataRecoverySyscall(cpussBase, ram_code_location, ram_cmd_location, ram_loc_stack_top, 0x06, ((i - addr) / sizeof(readvals)) << 16, 0, 0))
					return;
			}
			
			if (!psoc4dataRecoverySyscall(cpussBase, ram_code_location, ram_cmd_location, ram_loc_stack_top, 0x0A, 0, 0, 0))
				return;
		}
	}

just_skipit:
	//checksums[0] = 0x001d29;	//sss

	
	#ifndef METHOD_1
	
		for (i = 0; i < nTurns; i++)
			fprintf(stderr, "i.s[%u][0] = 0x%08X\n", i, flipPoint[i][0]);
	
		for (i = 3; i < nTurns; i += 2) {
			
			for (j = 0; j < sizeof(readvals) * 8; j++)
				flipPoint[1][j] += flipPoint[i][j];
		}
		
		for (i = 4; i < nTurns; i += 2) {
			
			for (j = 0; j < sizeof(readvals) * 8; j++)
				flipPoint[2][j] += flipPoint[i][j];
		}
		
		for (j = 0; j < sizeof(readvals) * 8; j++) {
			flipPoint[2][j] *= (nTurns - 0) / 2;
			flipPoint[1][j] *= (nTurns - 1) / 2;
			flipPoint[0][j] *= (nTurns - 0) / 2;
			flipPoint[0][j] *= (nTurns - 1) / 2;
		}
	
		for (i = 0; i < nTurns; i++)
			fprintf(stderr, "f.s[%u][0] = 0x%08X\n", i, flipPoint[i][0]);
	
		for (j = 0; j < sizeof(readvals) * 8; j++) {
			
			fprintf(stderr, "Guessing [%04xh] 0x%08X on the range 0x%08X - 0x%08X ... ", j, flipPoint[0][j], flipPoint[2][j], flipPoint[1][j]);
			
			if (flipPoint[2][j] == flipPoint[1][j]) {
				fprintf(stderr, "RANGE NONEXISTENT\n");
			}
			else if (flipPoint[2][j] > flipPoint[1][j]) {
				
				fprintf(stderr, "RR ...");	//reversed range
				
				if (flipPoint[0][j] > flipPoint[2][j]) {
					fprintf(stderr, "ol 0\n");
					flipPoint[0][j] = -1;
				}
				else if (flipPoint[0][j] < flipPoint[1][j]) {
					fprintf(stderr, "ol 1\n");
					flipPoint[0][j] = 1;
				}
				else {
					uint32_t range = flipPoint[2][j] - flipPoint[1][j];
					uint32_t distFrom0 = flipPoint[2][j] - flipPoint[0][j];
					uint32_t distFrom1 = flipPoint[0][j] - flipPoint[1][j];
					
					if (distFrom0 == distFrom1) {
						fprintf(stderr, "50/50\n");
						flipPoint[0][j] = 0;
					}
					else if (distFrom0 < distFrom1) {
						fprintf(stderr, "guess 0 %u%%\n", distFrom1 * 100 / range);
						flipPoint[0][j] = -1;
					}
					else {
						fprintf(stderr, "guess 1 %u%%\n", distFrom0 * 100 / range);
						flipPoint[0][j] = 1;
					}
				}
			}
			else {	//normal range
				
				fprintf(stderr, " .....");
			
				if (flipPoint[0][j] < flipPoint[2][j]) {
					fprintf(stderr, "ol 0\n");
					flipPoint[0][j] = -1;
				}
				else if (flipPoint[0][j] > flipPoint[1][j]) {
					fprintf(stderr, "ol 1\n");
					flipPoint[0][j] = 1;
				}
				else {
					uint32_t range = flipPoint[1][j] - flipPoint[2][j];
					uint32_t distFrom0 = flipPoint[0][j] - flipPoint[2][j];
					uint32_t distFrom1 = flipPoint[1][j] - flipPoint[0][j];
					
					if (distFrom0 == distFrom1) {
						fprintf(stderr, "50/50\n");
						flipPoint[0][j] = 0;
					}
					else if (distFrom0 < distFrom1) {
						fprintf(stderr, "guess 0 %u%%\n", distFrom1 * 100 / range);
						flipPoint[0][j] = -1;
					}
					else {
						fprintf(stderr, "guess 1 %u%%\n", distFrom0 * 100 / range);
						flipPoint[0][j] = 1;
					}
				}
			}
		}
		
		for (q = 0; q <= 1; q++) {
			uint8_t trybuf[sizeof(readvals)];
			uint32_t checksum = 0;
			fprintf(stderr, "guessing with 50/50s being %u...", q);
			
			memset(trybuf, 0, sizeof(trybuf));
			
			for (j = 0; j < sizeof(readvals) * 8; j++) {
						
				uint32_t dstIdx = j / 8;
				uint32_t dstMask = 1 << (j % 8);
				uint32_t val;
				
				if ((flipPoint[0][j] < 0) || (!flipPoint[0][j] && !q))
					dstMask = 0;
				
				trybuf[dstIdx] |= dstMask;
			}
		
			for (j = 0; j < sizeof(trybuf); j++)
				checksum += trybuf[j];
			
			fprintf(stderr, "checksum 0x%06X, wanted 0x%05X\n", checksum, checksums[0]);
		}
		
			
		
	
	#else	//old analysis
	
		for (i = 0; i < sizeof(readvals) * 8; i++) {
			flipPoint[0][i] *= nTurns - 1;
			for (j = 1; j < nTurns; j++)
				flipPoint[0][i] -= flipPoint[j][i];
			
			fprintf(stderr, "flipPoint[0][0x%02X] = 0x%08X;\n", i, flipPoint[0][i]);
		}
		
		
		
		for (i = addr; i < addr + len; i += sizeof(readvals)) {
			
			uint32_t expectedCsum, calcedCsum;
			uint8_t trybuf[sizeof(readvals)];
			int32_t min, max, guess;
			int32_t bestVal;
			int32_t bestDiff = 0x0f000000;	//impossibly big diff
		
			min = 0x7fffffff;
			max = -min;
			
			expectedCsum = checksums[(i - addr) / sizeof(readvals)];
			
			for (j = 0; j < sizeof(readvals) * 8; j++) {
				if (flipPoint[0][j] > max)
					max = flipPoint[0][j];
				if (flipPoint[0][j] < min)
					min = flipPoint[0][j];
			}
			
			max++;
			min--;
			
			fprintf(stderr, "Trying to decode 0x%08X (checksum 0x%06X) over the range [%d,%d]\n", i, expectedCsum, min, max);
			
			while(1) {
				for (guess = min; guess < max + 1; guess++) {
					int32_t diff;
					
					memset(trybuf, 0, sizeof(trybuf));
					
					for (j = 0; j < sizeof(readvals) * 8; j++) {
						
						uint32_t dstIdx = j / 8;
						uint32_t dstMask = 1 << (j % 8);
						uint32_t val;
						
						if (flipPoint[0][j] >= guess)
							trybuf[dstIdx] |= dstMask;
					}
					
					calcedCsum = 0;
					for (j = 0; j < sizeof(trybuf); j++)
						calcedCsum += trybuf[j];
					
					fprintf(stderr, "  %5i -> 0x%06x (wanted 0x%06x)\n", guess, calcedCsum, expectedCsum);
					
					if (calcedCsum == expectedCsum)
						break;
					
					diff = calcedCsum - expectedCsum;
					if (diff < 0)
						diff = -diff;
					if (diff < bestDiff) {
						bestDiff = diff;
						bestVal = guess;
					}
				}
				
				if (calcedCsum == expectedCsum) {
					fprintf(stderr, "likely success\n");
					break;
				}
				else if (min == max)
					break;
				else {
					fprintf(stderr, "no exact match, will use closest (@ %d)\n", bestVal);
					min = max = bestVal;
				}
			}
			
			memcpy(flipPoint[0], trybuf, sizeof(trybuf));
			for (j = 0; j < sizeof(readvals); j++)
			{
				putchar(~trybuf[j]);
			}
			
			for (j = 0; j < sizeof(readvals) / sizeof(uint32_t); j++) {
				fprintf(stderr, "reconstructed[0x%02X] = 0x%08X\n", j, flipPoint[0][j]);
			}
		}
	
	
	#endif
	
out:
	fprintf(stderr, "\nSUCCESS\n");
	

	return;
}

struct {
	const char *name, *info;
	SpecialFuncF funcPtr;
} mSpecialFuncs[] = {
	{.name = "nrfunlock", .info = "Unlock an nRf5x chip", .funcPtr = sfNrfUnlock,},
	{.name = "freescaleunlock", .info = "Unlock a frescale chip", .funcPtr = sfFreescaleUnlock,},
	{.name = "efm32hgunlock", .info = "Unlock an EFM32HG chip", .funcPtr = sfEfm32hgUnlock,},
	{.name = "stm32f1xxunlock", .info = "Unlock an SFM32F1xx chip", .funcPtr = stm32f1Unlock,},
	{.name = "psoc4xxx_init", .info = "Init a PSoC4xxx chip", .funcPtr = psoc4init,},
	{.name = "psoc4xxx_unlock", .info = "Unlock a PSoC4xxx chip", .funcPtr = psoc4unlock,},
	{.name = "psoc4xx_read_word_as_priviledged", .info = "Dump a word from a PSoC4xxx chip in PRIV mode", .funcPtr = psoc4romDumpWord,},
	{.name = "psoc4xx_read_minirom", .info = "Dump a word from a PSoC4xxx minirom", .funcPtr = psoc4miniromDumpWord,},
	{.name = "psoc4xx_data_recovery", .info = "Recover PSoc$ protected data", .funcPtr = psoc4dataRecovery,},
};

static void specialFuncsList(void)
{
	uint32_t i;
	
	for (i = 0; i < sizeof(mSpecialFuncs) / sizeof(*mSpecialFuncs); i++)
		fprintf(stderr, "'%s' -> %s\n", mSpecialFuncs[i].name, mSpecialFuncs[i].info);
}

static SpecialFuncF specialFuncsFind(const char *nm)
{
	uint32_t i;
	
	for (i = 0; i < sizeof(mSpecialFuncs) / sizeof(*mSpecialFuncs); i++)
		if (!strcmp(mSpecialFuncs[i].name, nm))
			return mSpecialFuncs[i].funcPtr;
	
	return NULL;
}

int main(int argc, char* argv[])
{
	uint32_t scriptSz, addrOverride = 0xffffffff, lenOverride = 0, scriptBase = 0, flashBase, flashSize = 0, flashBlock, flashStageAddr, traceAdr = 0, gdbPort = 0, opmask;
	bool haveAddrOverride = false, haveLenOverride = false, haveCustomCmd = false, haveBlockErase = false, sendDataWithAcks = true, tmpBool = false, pwrOnBefore = false;
	bool showUsage = false, haveWrite = false, haveMassErase = false, doEraseAll = false, doErase = false, gdbDoReset = false, pwrOffAfter = false;
	char *writeFrom = NULL, *readTo = NULL, *uploadFrom = NULL, *scptFileNm = NULL, *wantedDebuggerSnum = NULL;
	char *cpuNm = NULL, *selfName = argv[0];
	SpecialFuncF wantedSpecialFunc = NULL;
	uint64_t ticksStart, ticksPrev;
	struct CommsPacket pkt;
	hid_device *handle;
	FILE *scpt = NULL;
	int i, ret = 0;
	struct stat st;


	opterr = 0;
	while (1) {
		char opt = getopt(argc, argv, "s:w:r:u:A:L:eET:_G:g:S::P::s:");
		unsigned long tmpLongVal;

		if (opt == -1)
			break;
		
		switch(opt) {
			case '?':
			default:
				goto showusage;
			
			case '_':
				sendDataWithAcks = false;
				break;
			
			case 'G':
				gdbDoReset = true;
			case 'g':
				if (gdbPort)
					goto showusage;
				
				i = atoi(optarg);
				if (i <= 0 || i >= 65536)
					goto showusage;
				gdbPort = i;
				break;
			
			case 'P':
					if (!optarg)
						goto showusage;
					else if (!strcmp(optarg, "during"))
						pwrOnBefore = pwrOffAfter = true;
					else if (!strcmp(optarg, "on"))
						pwrOnBefore = true;
					else if (!strcmp(optarg, "off"))
						pwrOffAfter = true;
					else
						goto showusage;
					break;

			case 'S':
				if (!optarg || !(wantedSpecialFunc = specialFuncsFind(optarg))) {
					fprintf(stderr, "SPECIAL OPERATIONS SUPPORTED: \n");
					specialFuncsList();
					goto out_opts;
				}
				break;
			
			case 'r':
				readTo = strdup(optarg);
				break;
			
			case 'w':
				if (access(optarg, R_OK) == -1)
					goto showusage;
				writeFrom = strdup(optarg);
				break;
			
			case 'u':
				if (access(optarg, R_OK) == -1)
					goto showusage;
				uploadFrom = strdup(optarg);
				break;
			
			case 'e':
				doErase = true;
				break;
			
			case 'E':
				doEraseAll = true;
				break;
			
			case 'A':
			case 'L':
				if (1 != sscanf(optarg, "%li", &tmpLongVal))
					goto showusage;
				if ((uint32_t)tmpLongVal != tmpLongVal)
					goto showusage;
				if (tmpLongVal & 3) {
					fprintf(stderr, "%s must be a multiple of 4\n", (opt == 'A') ? "address" : "length");
					goto showusage;
				}
				if (opt == 'A') {
					addrOverride = tmpLongVal;
					haveAddrOverride = true;
				}
				else {
					lenOverride = tmpLongVal;
					haveLenOverride = true;
				}
				break;
			case 'T':
				if (1 != sscanf(optarg, "%li", &tmpLongVal))
					goto showusage;
				if (tmpLongVal & 3) {
					fprintf(stderr, "trace addr must be a multiple of 4\n");
					goto showusage;
				}
				if (!tmpLongVal) {
					fprintf(stderr, "trace addr must be nonzero\n");
					goto showusage;
				}
				traceAdr = tmpLongVal;
				break;
			
			case 's':
				wantedDebuggerSnum = strdup(optarg);
				break;
		}

		continue;

	showusage:
		showUsage = true;
		goto out_opts;
	}

	if (haveAddrOverride && !haveLenOverride)
		goto showusage;

	if (doErase && doEraseAll)
		goto showusage;

	ret = hid_init();
	if (ret) {
		fprintf(stderr, "Failed to load hid lib\n");
		goto out_hidlib;
	}

	//Find the device
	handle = openHidDev(wantedDebuggerSnum);
	if (!handle) {
		ret = ENODEV;
		goto out_hidopen;
	}

	drainOsBuf(handle);
	if (!verifyRemoteVer(handle)) {
		fprintf(stderr, "VER GET INFO ERR\n");
		goto out_misc;
	}
	
	if (!mHavePowerCtrl && (pwrOnBefore || pwrOffAfter)) {
		pwrOffAfter = pwrOnBefore = false;
		fprintf(stderr, "This debugger does not have power control. Power requests cannot be satisfied\n");
		goto out_misc;
	}
	
	if (!cpuInit(handle, doOneSwdDevCmd, verInfo.maxXferBytes)) {
		fprintf(stderr, "CPU MODULE INIT ERR\n");
		goto out_misc;
	}
	
	
	if (pwrOnBefore) {
		if (!cpuPwrSet(true))
			fprintf(stderr, "Failed to set power on state for target. Proceeding anyways...\n");
	}
	
	do {

		opmask = 0;
		if (wantedSpecialFunc)
			wantedSpecialFunc(SPECIAL_STEP_PRE_CPU_INIT, (void**)&wantedSpecialFunc, &opmask, addrOverride, lenOverride);
		
		free(cpuNm);
		cpuNm = NULL;
		if (!cpuIdentify(&scriptSz, &scriptBase, &flashStageAddr, &cpuNm, &scptFileNm, &scpt))	//does actual attach
			scpt = NULL;
	
		opmask = 0;
		if (wantedSpecialFunc)
			wantedSpecialFunc(SPECIAL_STEP_POST_CPU_INIT, (void**)&wantedSpecialFunc, &opmask, addrOverride, lenOverride);
	
	} while (opmask & SPECIAL_OPMASK_POSTINIT_RETRY_INIT);
	
	//make user feel comfy
	if(scpt) {
		fprintf(stderr, "Using scriptfile '%s'\n", scptFileNm);
		free(scptFileNm);
	}
	
	//do not bother with the script upload if it is not needed
	if (scpt && !(doEraseAll || doErase || writeFrom || ((readTo || writeFrom) && (!haveAddrOverride || !haveLenOverride)))) {
		fclose(scpt);
		scpt = NULL;
	}

	opmask = 0;
	if (wantedSpecialFunc)
			wantedSpecialFunc(SPECIAL_STEP_GET_CPU_QUIRKS, (void**)&wantedSpecialFunc, &opmask, addrOverride, lenOverride);

	if (scpt) {
		uint32_t i, now, addr = scriptBase, nfo[4], tmpSz = scriptSz;

		//reset cpu
		if (!(opmask & SPECIAL_OPMASK_GET_QUIRKS_RESET_DISCONNECTS) && !cpuReset()) {
			fprintf(stderr, "CHIP RESET FAIL\n");
			goto out_misc;
		}
		
		if (cpuStop() == CORTEX_W_FAIL) {
			fprintf(stderr, "CHIP RESET & STOP FAIL\n");
			goto out_misc;
		}
		
		fprintf(stderr, " script base = 0x%08X\n", scriptBase);
		(void)fseek(scpt, 0, SEEK_SET);

		while (tmpSz) {
			uint32_t buf[1024] = {0,}, now = tmpSz > sizeof(buf) ? sizeof(buf) : tmpSz, numWords = (now + sizeof(uint32_t) - 1) / sizeof(uint32_t);
			
			size_t done = fread(buf, 1, now, scpt);
			if (done != now) {
				fprintf(stderr, "MEM ERR\n");
				goto out_misc;
			}

			if (!cpuMemWrite(addr, numWords, buf, true)) {
				fprintf(stderr, "SCRIPT UPLOAD: ERROR writing %u words @ 0x%08X\n", numWords, addr);
				goto out_misc;
			}

			tmpSz -= now;
			addr += now;
		}
		
		fprintf(stderr, "%u bytes of script uploaded\n", scriptSz);
		
		if (!setCpsrTbitAndDisableInterrupts())
			goto out_misc;
		
		//run scrit init tasks
		setWatchpoint(0, scriptBase + 0x0C, 2, CORTEX_WP_PC);	//for syscalls to debugger
	
		fprintf(stderr, "running stage 1 init...\n");
		if (!runScript(scriptBase, 0x10, 0, 0, 0, 0)) {
			fprintf(stderr, "STAGE 1 init failed\n");
			goto out_misc;
		}
		fprintf(stderr, "running stage 2 init...\n");
		if (!runScript(scriptBase, 0x14, 0, 0, 0, 0)) {
			fprintf(stderr, "STAGE 1 init failed\n");
			goto out_misc;
		}
		fprintf(stderr, "running stage 3 init...\n");
		if (!runScript(scriptBase, 0x18, 0, 0, 0, 0)) {
			fprintf(stderr, "STAGE 1 init failed\n");
			goto out_misc;
		}
	
		if (!cpuMemRead(scriptBase + 0x00, 3, nfo)) {
			fprintf(stderr, "NFO 1 read fail\n");
			goto out_misc;
		}
		
		flashSize = nfo[0];
		flashBlock = nfo[1];
		flashBase = nfo[2];
		
		if (!flashSize || flashBlock < 4 || (flashBlock & (flashBlock - 1)) || flashSize % flashBlock || (flashBase & 3)) {
			fprintf(stderr, "Flash info in NFO 1 not believable: 0x%08X 0x%08X 0x%08X 0x%08X\n", nfo[0], nfo[1], nfo[2], nfo[3]);
			goto out_misc;
		}
		
		fprintf(stderr, "Chip claims %u bytes of flash in %ux %u-byte blocks at 0x%08X. (We'll stage at 0x%08X)\n",
			flashSize, flashSize / flashBlock, flashBlock, flashBase, flashStageAddr);
		
		if (!cpuMemRead(scriptBase + 0x1C, 4, nfo)) {
			fprintf(stderr, "NFO 2 read fail\n");
			goto out_misc;
		}
		
		haveMassErase = !!nfo[0];
		haveBlockErase = !!nfo[1];
		haveWrite = !!nfo[2];
		haveCustomCmd = !!nfo[3];
		
		fprintf(stderr, "Chip supports:%s%s%s%s\n", haveMassErase ? " MassErase" : "",
			haveBlockErase ? " BlockErase" : "", haveWrite ? " BlockWrite" : "", haveCustomCmd ? " CustomCmd" : "");
	}
	
	if (readTo) {
		
		uint32_t addr = flashBase, len = flashSize, lenOrig;
		FILE *out = fopen(readTo, "wb");
		
		if (!out) {
			fprintf(stderr, "Cannot create read output file '%s'\n", readTo);
			goto out_misc;
		}
		
		if (haveAddrOverride)
			addr = addrOverride;
		if (haveLenOverride)
			len = lenOverride;
		
		lenOrig = len = (len + sizeof(uint32_t) - 1) / sizeof(uint32_t);	//convert to words
		
		ticksStart = getTicks();
		
		while (len) {
			uint32_t xferSz = cpuGetOptimalReadNumWords(), buf[xferSz], now = len > xferSz ? xferSz : len;
			
			ticksPrev = getTicks();
			
			if (!cpuMemRead(addr, now, buf)) {
				fprintf(stderr, "READ error\n");
				fclose(out);
				goto out_misc;
			}
			fwrite(buf, now * sizeof(uint32_t), 1, out);
			addr += now * sizeof(uint32_t);
			len -=now;
			
			ticksPrev = getTicks() - ticksPrev;
			if (!ticksPrev)
				ticksPrev = 1;
			fprintf(stderr, "Reading @ 0x%08X (%3u%%) @ %u bytes/sec   \r",
				addr, 100 * (lenOrig - len) / lenOrig, (uint32_t)((uint64_t)now * sizeof(uint32_t) * 1000 / ticksPrev));
		}
		
		ticksStart = getTicks() - ticksStart;
		if (!ticksStart)
			ticksStart = 1;
		fprintf(stderr, "Read complete %u bytes in %u ms -> %u b/s                                         \n",
				(uint32_t)(lenOrig * sizeof(uint32_t)), (uint32_t)ticksStart, (uint32_t)((uint64_t)lenOrig * sizeof(uint32_t) * 1000 / ticksStart));
		fclose(out);
	}
	
	if (uploadFrom) {
		
		uint32_t addr = flashBase, len = flashSize, lenOrig;
		FILE *in = fopen(uploadFrom, "rb");
		
		if (!in) {
			fprintf(stderr, "Cannot open upload input file '%s'\n", uploadFrom);
			goto out_misc;
		}
		
		if (haveAddrOverride)
			addr = addrOverride;
		if (haveLenOverride)
			len = lenOverride;

		(void)/*i like to live dangerously*/stat(uploadFrom, &st);
		if (len > st.st_size)
			len = st.st_size;

		lenOrig = len = (len + sizeof(uint32_t) - 1) / sizeof(uint32_t);	//convert to words
		
		ticksStart = getTicks();
		
		while (len) {
			uint32_t xferSz = cpuGetOptimalWriteNumWords(), buf[xferSz], now = len > xferSz ? xferSz : len;
			
			ticksPrev = getTicks();
			
			now = fread(buf, 1, now * sizeof(uint32_t), in) + 3;
			now /= sizeof(uint32_t);
			
			if (!cpuMemWrite(addr, now, buf, sendDataWithAcks)) {
				fprintf(stderr, "UPLD write error\n");
				fclose(in);
				goto out_misc;
			}
			addr += now * sizeof(uint32_t);
			len -=now;
			
			ticksPrev = getTicks() - ticksPrev;
			if (!ticksPrev)
				ticksPrev = 1;
			fprintf(stderr, "Uploading @ 0x%08X (%3u%%) @ %u bytes/sec   \r",
				addr, 100 * (lenOrig - len) / lenOrig, (uint32_t)((uint64_t)now * sizeof(uint32_t) * 1000 / ticksPrev));
		}
		
		ticksStart = getTicks() - ticksStart;
		if (!ticksStart)
			ticksStart = 1;
		fprintf(stderr, "Upload complete %u bytes in %u ms -> %u b/s                                         \n",
				(uint32_t)(lenOrig * sizeof(uint32_t)), (uint32_t)ticksStart, (uint32_t)((uint64_t)lenOrig * sizeof(uint32_t) * 1000 / ticksStart));
		fclose(in);
	}
	
	if (doEraseAll) {
		
		if (!scpt || !haveMassErase) {
			
			fprintf(stderr, "Cannot do mass erase without script that supports it. Try '-e'\n");
			goto out_misc;
		}
		
		if (!runScript(scriptBase, 0x1C, 0, 0, 0, 0)) {
			
			fprintf(stderr, "Mass erase script failed\n");
			goto out_misc;
		}
	}

	if (doErase) {
	
		uint32_t addr = flashBase, len = flashSize, pos;
		
		if (haveAddrOverride)
			addr = addrOverride;
		if (haveLenOverride)
			len = lenOverride;
		
		if (!scpt || !haveBlockErase) {
			fprintf(stderr, "Cannot block-erase without script that supports that. Try '-E'\n");
			goto out_misc;
		}
		
		if ((addr & (flashBlock - 1)) || (len & (flashBlock - 1))) {
			fprintf(stderr, "Cannot erase non-block aligned area\n");
			goto out_misc;
		}
		
		for (pos = addr; pos != addr + len; pos += flashBlock) {
			fprintf(stderr, "Erasing @ 0x%08X     \r", pos);
			if (!runScript(scriptBase, 0x20, pos, 0, 0, 0)) {
				
				fprintf(stderr, "Block erase script failed\n");
				goto out_misc;
			}
		}
		fprintf(stderr, "\n");
	}

	if (writeFrom) {
		
		uint32_t pos, addr = flashBase, len = flashSize, lenOrig;
		FILE *in = fopen(writeFrom, "rb");
			
		if (!in) {
			fprintf(stderr, "Cannot open write input file '%s'\n", writeFrom);
			goto out_misc;
		}

		if (haveAddrOverride)
			addr = addrOverride;
		if (haveLenOverride)
			len = lenOverride;
		
		(void)/*i like to live dangerously*/stat(writeFrom, &st);
		if (len > st.st_size)
			len = st.st_size;

		//round len up
		len = (len + flashBlock - 1) / flashBlock * flashBlock;
		
		if (!scpt || !haveWrite ) {
			fprintf(stderr, "Cannot write without script that supports write\n");
			goto out_misc;
		}
		
		if (addr & (flashBlock - 1)) {
			fprintf(stderr, "Cannot write non-block aligned area\n");
			goto out_misc;
		}
		
		ticksStart = getTicks();
		
		for (pos = addr; pos != addr + len; pos += flashBlock) {
			
			uint32_t xferSz = cpuGetOptimalWriteNumWords(), step = flashBlock > xferSz * sizeof(uint32_t) ? xferSz * sizeof(uint32_t) : flashBlock;
			
			for (i = 0; i < flashBlock; i += step) {
				uint32_t buf[xferSz], now = flashBlock - i, dummy;
				
				if (now > step)
					now = step;
				
				memset(buf, 0, sizeof(buf));
				ticksPrev = getTicks();
				
				dummy = fread(buf, 1, now, in);
				(void)dummy;
				
				if (!cpuMemWrite(flashStageAddr + i, now / sizeof(uint32_t), buf, sendDataWithAcks)) {
					fprintf(stderr, "WR error\n");
					fclose(in);
					goto out_misc;
				}
				ticksPrev = getTicks() - ticksPrev;
				if (!ticksPrev)
					ticksPrev = 1;
				fprintf(stderr, "Writing @ 0x%08X (%3u%%) @ %u bytes/sec   \r",
					pos + i, 100 * (pos + i - addr) / len, (uint32_t)((uint64_t)step * 1000 / ticksPrev));
			}

			if (!runScript(scriptBase, 0x24, pos, 0, 0, 0)) {
				
				fprintf(stderr, "Block write script failed\n");
				goto out_misc;
			}
			
		}
		ticksStart = getTicks() - ticksStart;
		if (!ticksStart)
			ticksStart = 1;
		fprintf(stderr, "Write complete %u bytes in %u ms -> %u b/s                                         \n",
				len, (uint32_t)ticksStart, (uint32_t)((uint64_t)len * 1000 / ticksStart));
		fclose(in);
	}

	if (traceAdr) {
		
		uint32_t word;
		
		if (opmask & SPECIAL_OPMASK_GET_QUIRKS_RESET_DISCONNECTS) {
			FILE *f;
			
			(void)cpuReset();
			if (!cpuIdentify(NULL, NULL, NULL, NULL, NULL, NULL)) {
				fprintf(stderr, "CANNOT REACQUIRE CPU\n");
				goto out_misc;
			}
		}
		else if (!cpuReset() || !cpuGo()) {
			fprintf(stderr, "CANNOT START CPU\n");
			goto out_misc;
		}
		
		fprintf(stderr, "TRACE LOG from 0x%08x:\n", traceAdr);
		
		while(1) {
			uint8_t buf[verInfo.maxXferBytes];
			uint32_t i, len;
			
			len = cpuTraceLogRead(traceAdr, buf);
			for (i = 0; i < len; i++)
				putchar(buf[i]);
			fflush(stdout);
		}
	}

	if (gdbPort)
	{
		if (gdbDoReset && !cpuReset()) {
			fprintf(stderr, "CANNOT RESET CPU\n");
			goto out_misc;
		}
		
		gdbServer(gdbPort);
	}
	
	if (writeFrom && !traceAdr && !gdbPort) {
		fprintf(stderr, "resetting after upload...\n");
		
		if (opmask & SPECIAL_OPMASK_GET_QUIRKS_RESET_DISCONNECTS)
			(void)cpuReset();
		else if (!cpuReset() || !cpuGo()) {
			fprintf(stderr, "CHIP RESET & GO FAIL\n");
			goto out_misc;
		}
	}
	
	fprintf(stderr, "All done!\n");
	ret = 0;

out_misc:

	if (pwrOffAfter) {
		if (!cpuPwrSet(false))
			fprintf(stderr, "Failed to set power state to 0 for target\n");
	}

	if (scpt)
		fclose(scpt);

out_hidopen:
	(void)hid_exit();

out_hidlib:
	;
	
out_opts:
	if (showUsage)
		fprintf(stderr, "USAGE: %s\n"
				"\t[-r <READ_INTO_THIS_FILE>]\n"
				"\t[-u <UPLOAD_THIS_FILE> /* to ram */]\n"
				"\t[-w <WRITE_THIS_FILE> /* to flash, does NOT imply pre-erase, use -E or -e */]  \n"
				"\t[-e /* erase area */ ]\n"
				"\t[-E /* erase all */ ]\n"
				"\t[-L <OVERWRITE_LENGTH> [-A <OVERWRITE_BASE_ADDRESS>]]\n"
				"\t[-T <ADDRESS> /* live log trace using given address for buffer */]\n"
				"\t[-G <PORT> /* start GDB server on a given port (reset chip) */]\n"
				"\t[-g <PORT> /* same as -G, but no chip reset */]\n"
				"\t[-_ /* upload with no ACKs for speed */]\n"
				"\t[-S<SPECIAL CMD> /* like 'nrfunlock'. Pass nothign to get list */]\n"
				"\t[-P <on|off|during> /* turn power on, off or only on during command */]\n"
				"\t[-s <DEBUGGER_SERIAL_NUMBER>]\n"
				, selfName);

	free(wantedDebuggerSnum);
	free(uploadFrom);
	free(writeFrom);
	free(readTo);
	return ret;
}
