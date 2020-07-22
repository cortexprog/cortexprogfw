#include "gdbServer.h"
#include "cpu.h"
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/select.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <stdint.h>
#include <netinet/in.h>
#include <string.h>
#include <stdio.h>
#include <arpa/inet.h>




//watchpoint info
static bool mHaveReadDataWpts, mHaveWriteDataWpts, mHaveRwDataWpts, mWptsAvail[16];
static uint32_t mMaxWpt, mWptAddrs[16], mWptMaxLogSz /* max sz ia 1 << this */;
static uint8_t mWptTypes[16];

//breakpoint info (we could use the 2 bpts per reg approach FBPv1 allows, but we do not because it is a huge pita)
static uint32_t mMaxBpt, mBptRemap, mBptV2;
static bool mBptsAvail[128];
static uint32_t mBptAddrs[128];

//reg caches
static bool mRegsValid;
static uint32_t mRegs[NUM_REGS*2];	//SWD_COMMS_REG_SET_BASE, SWD_COMMS_REG_SET_CTRL

//misc things
static bool mWasRunning;




#define WPT_TYPE_PC_ADDR		4
#define WPT_TYPE_DATA_READ		5
#define WPT_TYPE_DATA_WRITE		6
#define WPT_TYPE_DATA_RW		7



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

static bool checkForWptTypeSupport(uint32_t type, bool *resultP)
{
	uint32_t t = type;
	
	if (!cpuMemWrite(ADDR_DWT_FUNC0, 1, &t, true))
		return false;
	if (!cpuMemRead(ADDR_DWT_FUNC0, 1, &t))
		return false;
	if ((t & 0x0f) == type)
		*resultP = true;
	return true;
}

static bool gatherTargetLimitations(void)
{
	bool v7 = cpuIsV7();
	uint32_t t;
	
	
	//read number of watchpoints
	if (!cpuMemRead(ADDR_DWT_CTRL, 1, &t))
		return false;
	mMaxWpt = t >> 28;
	
	//check on what watchpoitns can do
	if (mMaxWpt) {
		
		if (!checkForWptTypeSupport(WPT_TYPE_DATA_READ, &mHaveReadDataWpts))
			return false;
		if (!checkForWptTypeSupport(WPT_TYPE_DATA_WRITE, &mHaveWriteDataWpts))
			return false;
		if (!checkForWptTypeSupport(WPT_TYPE_DATA_RW, &mHaveRwDataWpts))
			return false;
		
		t = 0x1F;
		if (!cpuMemWrite(ADDR_DWT_MASK0, 1, &t, true)) {
			fprintf(stderr, "Failed to write WPT mask\n");
			return false;
		}
		if (!cpuMemRead(ADDR_DWT_MASK0, 1, &t))
			return false;
		mWptMaxLogSz = t & 0x1F;
		
		
		fprintf(stderr, "Found support for %u watchpoints: %s%s%s%s. Max size: %ub\n", mMaxWpt, 
				mHaveReadDataWpts ? " R" : "",
				mHaveWriteDataWpts ? " W" : "",
				mHaveRwDataWpts ? " RW" : "",
				(mHaveReadDataWpts || mHaveWriteDataWpts || mHaveRwDataWpts) ? "" : "<UNKNOWN TYPES>",
				1 << mWptMaxLogSz);
		
		for (t = 0; t < mMaxWpt; t++){
			uint32_t zero = 0;
			
			if (!cpuMemWrite(ADDR_DWT_FUNC0 + t * OFST_DWT_NEXT, 1, &zero, true)) {
				fprintf(stderr, "Failed to disable watchpoint %u\n", t);
				return false;
			}
			
			mWptsAvail[t] = true;
		}
	}

	//read number of breakpoints
	if (!cpuMemRead(ADDR_FBP_CTRL, 1, &t))
		return false;
	mMaxBpt = (t >> 4) & 0x0f;
	if (v7)	//v7 has more
		mMaxBpt += (t >> 8) & 0x70;
	
	if (mMaxBpt) {
		
		if (v7) {
			uint32_t tt;
			
			if (!cpuMemRead(ADDR_FBP_REMAP, 1, &tt))
				return false;
			mBptRemap = !!((tt >> 29) & 1);
		}
		
		switch(t >> 28) {
			case 0:	//FBP v1
				break;
			case 1:	//FBP v2
				mBptV2 = true;
				break;
			default:
				fprintf(stderr, "FBP version unknown: %u\n", (t >> 28));
				break;
		}
		
		t |= 3;
		if (!cpuMemWrite(ADDR_FBP_CTRL, 1, &t, true)) {
			fprintf(stderr, "Failed to enable breakpoints\n");
			return false;
		}
		
		fprintf(stderr, "Found FBPv%u with support for %u breakpoints%s\n", mBptV2 ? 2 : 1, mMaxBpt, mBptRemap ? " and REMAP support" : "");
		
		for (t = 0; t < mMaxBpt; t++) {
			
			uint32_t zero = 0;
			
			if (!cpuMemWrite(ADDR_FBP_COMP0 + t * OFST_FBP_NEXT, 1, &zero, true)) {
				fprintf(stderr, "Failed to disable breakpoint %u\n", t);
				return false;
			}
			
			mBptsAvail[t] = true;
		}
	}
}


static void gdbRegsCache(void)
{
	if (mRegsValid)
		return;
	
	(void)/*XXX*/cpuRegsGet(SWD_COMMS_REG_SET_BASE, mRegs + 0 * NUM_REGS);
	(void)/*XXX*/cpuRegsGet(SWD_COMMS_REG_SET_CTRL, mRegs + 1 * NUM_REGS);
	
	mRegsValid = true;
}

static void gdbStop(void)
{
	(void)/*XXX*/cpuStop();
	
	gdbRegsCache();
}

static bool cpuBkptDel(uint32_t addr, uint8_t type, uint8_t sz)
{
	uint32_t i, t = 0;
	
	//verify size is 2 or 4
	if (sz != 2 && sz != 4)
		return false;
	
	//make addr even
	addr &=~ 1;
	
	for (i = 0; i < mMaxBpt; i++) {
		if (mBptsAvail[i])
			continue;
		if (mBptAddrs[i] != addr)
			continue;
		
		//for all versions of the FBP, zero means "disabled"
		if (!cpuMemWrite(ADDR_FBP_COMP0 + OFST_FBP_NEXT * i, 1, &t, true))
			return false;
		
		mBptsAvail[i] = true;
		return true;
	}
	
	return false;
}

static bool cpuBkptAdd(uint32_t addr, uint8_t type, uint8_t sz)
{
	uint32_t i, t;
	
	//verify size is 2 or 4
	if (sz != 2 && sz != 4)
		return false;
	
	//make addr even
	addr &=~ 1;
	
	//now we figure out what to write
	if (mBptV2)
		t = addr | 1;
	else {
		//if we have an FBPv1, verify addr is valid
		if (addr >>29)
			return false;
		t = (addr &~ 2) | ((addr & 2) ? (2 << 30) : (1 << 30)) | 1;
	}
	
	//find a slot & take it
	for (i = 0; i < mMaxBpt; i++) {
		
		if (!mBptsAvail[i])
			continue;
		
		if (!cpuMemWrite(ADDR_FBP_COMP0 + OFST_FBP_NEXT * i, 1, &t, true))
			return false;
		
		mBptAddrs[i] = addr;
		mBptsAvail[i] = false;
		
		return true;
	}
	
	return false;
}

static bool cpuWptDel(uint32_t addr, uint8_t type, uint8_t sz)
{
	uint32_t i, t = 0;
	
	for (i = 0; i < mMaxWpt; i++) {
		if (mWptsAvail[i])
			continue;
		if (mWptAddrs[i] != addr)
			continue;
		if (mWptTypes[i] != type)
			continue;
		
		if (!cpuMemWrite(ADDR_DWT_FUNC0 + OFST_DWT_NEXT * i, 1, &t, true))
			return false;
		
		mWptsAvail[i] = true;
		return true;
	}
	
	return false;
}

static bool cpuWptAdd(uint32_t addr, uint8_t type, uint8_t sz)
{
	uint32_t szLog, params[3], i;
	
	//size must be nonzero power of 2
	if (!sz)
		return false;
	if (sz & (sz - 1))
		return false;
	
	//calculate log base 2
	for (szLog = 0; szLog < 32 && szLog != sz; szLog++);
	
	//verify we can do it
	if (szLog > mWptMaxLogSz)
		return false;
	
	//find a slot
	for (i = 0; i < mMaxWpt; i++) {
		
		if (!mWptsAvail[i])
			continue;
		
		params[0] = addr;	//COMP
		params[1] = szLog;	//MASK
		params[2] = type;	//FUNC
		
		
		if (!cpuMemWrite(ADDR_DWT_COMP0 + OFST_DWT_NEXT * i, 3, params, true))
			return false;
		
		mWptAddrs[i] = addr;
		mWptTypes[i] = type;
		mWptsAvail[i] = false;
		
		return true;
	}
	
	return false;
}

static uint32_t htoiEx(const char** cP, int maxChars)
{
	uint32_t i = 0;
	const char* in = *cP;
	char c;
	
	while(maxChars-- && (c = *in) != 0){
		
		if(c >= '0' && c <= '9') i = (i * 16) + (c - '0');
		else if(c >= 'a' && c <= 'f') i = (i * 16) + (c + 10 - 'a');
		else if(c >= 'A' && c <= 'F') i = (i * 16) + (c + 10 - 'A');
		else break;
		in++;
	}
	
	*cP = in;
	
	return i;
}

static uint32_t htoi(const char** cP)
{
	return htoiEx(cP, 8);
}


static uint32_t swap32(uint32_t x)
{
	return ((x >> 24) & 0xff) | ((x >> 8) & 0xff00) | ((x & 0xff00) << 8) | ((x & 0xff) << 24);	
}


static bool gdb_memAccessPeriph(uint32_t addr, uint8_t* buf, int write)
{
	uint32_t tmp;

	if (!cpuMemRead(addr &~ 3, 1, &tmp))
		return false;

	fprintf(stderr, "tmp 1[0x%08x] = 0x%08x\n", addr, tmp);

	if (!write) {
		*buf = tmp >> ((addr & 3) * 8);
		return true;
	}

	tmp = (tmp &~ (0xfful << ((addr & 3) * 8))) | (((uint32_t)*buf) << ((addr & 3) * 8));

	fprintf(stderr, "tmp 2[0x%08x] = 0x%08x\n", addr, tmp);

	return cpuMemWrite(addr &~ 3, 1, &tmp, true);
}

static bool gdb_memAccess(uint32_t addr, uint8_t* buf, int write)
{
	#define MEM_CACHE_NUM_WORDS_SHIFT	5		//mem cache is (2 ^this_val) words
	
	static uint32_t cachedAddr = 0xffffffff, cachedVal[1 << MEM_CACHE_NUM_WORDS_SHIFT];
	uint32_t blockAddr = addr &~ ((4 << MEM_CACHE_NUM_WORDS_SHIFT) - 1);
	uint32_t wordIdx = (addr >> 2) & ((1 << MEM_CACHE_NUM_WORDS_SHIFT) - 1);
	
	//special case all periph addrs (>= 0x40000000)
	if (addr >= 0x40000000)
		return gdb_memAccessPeriph(addr, buf, write);


	if (blockAddr != cachedAddr) {
		uint32_t val[1 << MEM_CACHE_NUM_WORDS_SHIFT];
		
		fprintf(stderr, "issuing read @ 0x%08X for %u words\n", blockAddr, 1 << MEM_CACHE_NUM_WORDS_SHIFT);
		
		if (!cpuMemRead(blockAddr, 1 << MEM_CACHE_NUM_WORDS_SHIFT, val))
			return false;
		
		memcpy(cachedVal, val, sizeof(cachedVal));
		cachedAddr = blockAddr;
	}
	
	if (write)
	{
		uint32_t v = *buf, m = 0xff, val;
		v <<= (addr & 3) * 8;
		m <<= (addr & 3) * 8;
		
		val = (cachedVal[wordIdx] &~ m) | v;
		if (!cpuMemWrite(cachedAddr + wordIdx * 4, 1, &val, true))
			return false;
		
		cachedVal[wordIdx] = val;
	}
	else {
		*buf = cachedVal[wordIdx] >> ((addr & 3) * 8);
	}

	return true;
}

static void addRegVal32ToStr(char *str, int idx)
{
	uint32_t orend = (idx == 15) ? 1 : 0;	//keep pc odd
	
	sprintf(str + strlen(str), "%08x", swap32(mRegs[idx] | orend));
}

static bool addRegToStr(char* str, int reg)
{
	gdbRegsCache();
	
	if (reg < 0x10)		//R0..R15
		addRegVal32ToStr(str, reg);
	
	else if (reg < 0x18)	//double-length FP regs
		strcat(str, "000000000000000000000000");
	
	else if (reg == 0x18)	//fpcs
		addRegVal32ToStr(str, 20);
	
	else if (reg == 0x19)	//cpsr
		addRegVal32ToStr(str, 16);
	
	else
		return false;
	
	return true;
}

static int acceptConn(int port)
{
	struct sockaddr_in sa = {AF_INET, htons(port)};
	socklen_t sl = sizeof(sa);
	int sock, ret;
	
	inet_aton("127.0.0.1", (struct in_addr*)&sa.sin_addr.s_addr);
	
	sock = socket(PF_INET, SOCK_STREAM, 0);
	if(sock == -1){
		fprintf(stderr, "gdb socket creation fails: %d\n", errno);
		return -1;
	}
	
	ret = bind(sock, (struct sockaddr*)&sa, sizeof(sa));
	if(ret){
		fprintf(stderr, "gdb socket bind fails: %d\n", errno);
		close(sock);
		return -1;
	}
	
	ret = listen(sock, 1);
	if(ret){
		fprintf(stderr, "gdb socket listen fails: %d\n", errno);
		close(sock);
		return -1;
	}
	
	ret = accept(sock, (struct sockaddr*)&sa, &sl);
	if(ret == -1){
		fprintf(stderr, "gdb socket accept fails: %d\n", errno);
		close(sock);
		return -1;
	}
	close(sock);
	return ret;
}

static void sendpacket(int sock, char* packet, bool withAck)
{
	unsigned int c;
	int i;
			
	c = 0;
	for(i = 0; i < strlen(packet); i++) c += packet[i];
	memmove(packet + (withAck ? 2 : 1), packet, strlen(packet) + 1);
	if(withAck){
		packet[0] = '+';
		packet[1] = '$';
	}
	else{
		packet[0] = '$';
	}
	sprintf(packet + strlen(packet), "#%02x", c & 0xFF);
	
	fprintf(stderr, "PACKET OUT: '%s'\n", packet);
	send(sock, packet, strlen(packet), 0);	
}

static bool writeReg(int gdbRegNo, uint32_t val)
{
	gdbRegsCache();
	
	fprintf(stderr, "writing 0x%08X to reg %u\n", val, gdbRegNo);
	
	if (gdbRegNo < 0x10) {		//r0..r15
		mRegs[gdbRegNo] = val;
		(void)/*XXX*/cpuRegsSet(SWD_COMMS_REG_SET_BASE, mRegs + 0 * NUM_REGS);
	}
	else if (gdbRegNo == 0x18 || gdbRegNo == 0x19) {	//cpsr or fpsr
		mRegs[(gdbRegNo == 0x18) ? 20 : 16] = val;
		(void)/*XXX*/cpuRegsSet(SWD_COMMS_REG_SET_CTRL, mRegs + 1 * NUM_REGS);
	}
	else
		return false;
	
	return true;
}

static int interpPacket(int sock, const char* in, char* out)	//return 0 if we failed to interp a command, 1 is all ok, -1 to send no reply and run
{
	unsigned char c;
	unsigned addr, len;
	unsigned char* ptr;
	int i;
	int ret = 1;
	
	fprintf(stderr, "PACKET  IN: '%s'\n", in);
	
	if(strcmp(in, "qSupported") == 0){
		
		strcpy(out, "PacketSize=99");	
	}
	else if(strcmp(in, "vCont?") == 0){
		
		out[0] = 0;
	}
	else if(strcmp(in, "s") == 0){		//single step
		
		mRegsValid = false;
		(void)/*XXX*/cpuStep();
		strcpy(out,"S05");
		return 1;
	}
	else if(strcmp(in, "k") == 0){		//kill (we restart)
		
		mRegsValid = false;
		(void)/*XXX*/cpuReset();
		strcpy(out,"S05");
		return 1;
	}
	else if(strcmp(in, "c") == 0 || in[0] == 'C'){		//continue [with signal, which we ignore]
		
		mRegsValid = false;
		(void)/*XXX*/cpuGo();
		mWasRunning = true;
		return -1;
	}
	else if(in[0] == 'Z' || in[0] == 'z'){
		
		char op = in[0];
		char type = in[1];
		bool (*f)(uint32_t addr, uint8_t type, uint8_t sz) = NULL;
		
		in += 3;
		addr = htoi(&in);
		if(*in++ != ',') goto fail;	//no comma?
		len = htoi(&in);
		if(*in) goto fail;		//string not over?
		
		if (type == '0' || type == '1'){	//bkpt
			
			f = (op == 'Z') ? cpuBkptAdd : cpuBkptDel;
			type = 0;
		}
		else if (type == '2' || type == '3' || type == '4'){	//wpt
			
			f = (op == 'Z') ? cpuWptAdd : cpuWptDel;
			switch (type) {
				case '2':	//write
					type = WPT_TYPE_DATA_WRITE;
					break;
				case '3':	//read
					type = WPT_TYPE_DATA_READ;
					break;
				case '4'://rw
					type = WPT_TYPE_DATA_RW;
					break;
				default:
					f = NULL;
					break;
			}
		}
		else goto fail;

		strcpy(out, f && f(addr, type, len) ? "OK" : "e00");
	}
	else if(in[0] == 'H' && (in[1] == 'c' || in[1] == 'g')){
		strcpy(out, "OK");	
	}
	else if(in[0] == 'q'){
		
		if(in[1] == 'C'){
			
			strcpy(out, "");	
		}
		else if(strcmp(in + 1, "Offsets") == 0){
			
			strcpy(out, "Text=0;Data=0;Bss=0");
		}
		else goto fail;
	}
	else if(in[0] == 'p'){	//read register
		
		in++;
		i = htoi(&in);
		if(*in) goto fail;	//string not over?
		
		out[0] = 0;
		if(!addRegToStr(out, i)) goto fail;
	}
	else if(strcmp(in, "g") == 0){	//read all registers
		
		out[0] = 0;
		for(i = 0; i < 0x1a; i++) if(!addRegToStr(out, i)) goto fail;
	}
	else if(in[0] == 'G'){	//write all registers
		
		bool success = true;
		
		for (i = 0; i < 16; i++)
			success = success && writeReg(i, htoi(&in));
		if (strlen(in) > 8 * (0x18 - 0x10)) {
			in += 8 * (0x18 - 0x10);
			
			for (i = 0x18; i <= 0x19; i++)
				success = success && writeReg(i, htoi(&in));
		}
		
		strcpy(out, success ? "OK" : "E 00");
	}
	else if(in[0] == 'P'){	//write register
		
		in++;
		i = htoi(&in);
		if(*in++ != '=') goto fail;	//string not over?
		addr = swap32(htoi(&in));
		
		strcpy(out, writeReg(i, addr) ? "OK" : "E 00");
	}
	else if(in[0] == 'm'){	//read memory
		
		in++;
		addr = htoi(&in);
		if(*in++ != ',') goto fail;
		len = htoi(&in);
		if(*in) goto fail;
		out[0] = 0;
		while(len--){
			
			if(!gdb_memAccess(addr++, &c, false))
				break;
			sprintf(out + strlen(out), "%02x", c);	
		}
	}
	else if(in[0] == 'M'){	//write memory
		
		in++;
		addr = htoi(&in);
		if(*in++ != ',') goto fail;
		len = htoi(&in);
		if(*in++ != ':') goto fail;
		while(len--){
			
			c = htoiEx(&in, 2);
			
			fprintf(stderr, "writing 0x%02X to 0x%08x\n", c, addr);
			
			if(!gdb_memAccess(addr++, &c, true)) goto fail;
		}
		if(*in) goto fail;
		strcpy(out,"OK");
	}
	else if(strcmp(in, "?") == 0){
		
		strcpy(out,"S05");
	}
	else goto fail;
	
send_pkt:
	return ret;
	
fail:
	out[0] = 0;
	ret = 0;
	goto send_pkt;
}

void gdbServer(int port)
{
	int sock;
	
	fprintf(stderr, "GDB server listening on port %d\n", port);
	
	sock = acceptConn(port);
	if (!sock)
		return;
	
	gatherTargetLimitations();
	
	while(1){

		char packet[4096];
		uint32_t v32;
		int ret;
		
		do {
			struct timeval tv = {.tv_sec = mWasRunning ? 0 : 100,};
			fd_set set;
			
			FD_ZERO(&set);
			FD_SET(sock, &set);
			ret = select(sock + 1, &set, NULL, NULL, &tv);
			
		}while(ret == -1 && errno == EINTR);
		
		if (mWasRunning && (v32 = cpuIsStoppedAndWhy()) != CORTEX_W_FAIL) {	//we were unning and now stopped
			
			fprintf(stderr, "stop %u\n", v32);
			strcpy(packet, "S05");
			sendpacket(sock, packet, false);
			mWasRunning = false;
		}
		
		if (ret == 1) {
			
			char c;
			char* p;
			int i, len = 0, esc = 0, end = 0;
			
			ret = recv(sock, &c, 1, 0);
			if(ret != 1){
				fprintf(stderr, "failed to receive byte (1)\n");
				close(sock);
				return;
			}
			
			if(c == 3){
				strcpy(packet,"S11");
				sendpacket(sock, packet, false);
				gdbStop();
			}
			else if(c == '+'){
				//some ack for some reason?
			}
			else if(c != '$'){
				printf("unknown packet header '%c'\n", c);
			}
			else{
				do{
					if(esc){
						c = c ^ 0x20;
						esc = 0;
					}
					else if(c == 0x7d){
						esc = 1;
					}
					
					if(!esc){	//we cannot be here if we're being escaped
						
						packet[len++] = c;
						if(end == 0 && c == '#') end = 2;
						else if(end){
							
							end--;
							if(!end) break;
						}
						
						ret = recv(sock, &c, 1, 0);
						if(ret != 1) fprintf(stderr, "failed to receive byte (2)\n");
					}
				}while(1);
				packet[len] = 0;
				
				memmove(packet, packet + 1, len);
				len -= 4;
				packet[len] = 0;
				ret = interpPacket(sock, p = strdup(packet), packet);
				if(ret == 0)
					fprintf(stderr, "how do i respond to packet <<%s>>\n", p);
				if(ret == -1){	//ack it anyways
					char c = '+';
					send(sock, &c, 1, 0);
					//XXX: running = 1;
				}
				else sendpacket(sock, packet, true);
				
				free(p);
			}
		}
	}
}

