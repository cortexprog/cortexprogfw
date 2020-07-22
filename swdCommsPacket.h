#ifndef _SWD_COMMS_PACKET_H_
#define _SWD_COMMS_PACKET_H_


#include "../ModulaR/commsPacket.h"


#define NUM_REGS						16

#define SWD_COMMS_CMD_VER_INFO		0x00	// () -> SwdCommsVerInfoRespPacketV*
#define SWD_COMMS_CMD_ATTACH		0x01	// () -> SwdCommsAttachRespPacket
#define SWD_COMMS_CMD_MEM_READ		0x02	// SwdCommsMemPacket -> SwdCommsMemPacket
#define SWD_COMMS_CMD_MEM_WRITE		0x03	// SwdCommsMemPacket -> SwdCommsMemPacket
#define SWD_COMMS_CMD_REGS_READ		0x04	// () -> SwdCommsRegsPacket
#define SWD_COMMS_CMD_REGS_WRITE	0x05	// SwdCommsRegsPacket -> ()
#define SWD_COMMS_CMD_GO			0x06	// () -> (u8 success)
#define SWD_COMMS_CMD_RESET			0x07	// () -> (u8 success)
#define SWD_COMMS_CMD_STOP			0x08	// () -> (u8 CORTEX_W_* mask or CORTEX_W_FAIL)
#define SWD_COMMS_CMD_IS_STOPPED	0x09	// () -> (u8 CORTEX_W_* mask or CORTEX_W_FAIL)
#define SWD_COMMS_CMD_SINGLE_STEP	0x0A	// () -> (u8 CORTEX_W_* mask or CORTEX_W_FAIL)
#define SWD_COMMS_CMD_SELECT_CPU	0x0B	// (u16 cpuid) -> (u8 success)

#define SWD_COMMS_SWD_WIRE_BUS_R	0x3E	// SwdCommsWireBusPacket -> SwdCommsWireBusPacket
#define SWD_COMMS_SWD_WIRE_BUS_W	0x3F	// SwdCommsWireBusPacket -> SwdCommsWireBusPacket
#define SWD_TRACE_LOG_READ			0x40	// (u32) -> (u8[])
#define SWD_POWER_CTRL				0x41	// simple control: (u8 on) -> (u8 success)  === OR === variable-voltage control: (u16 millivolts) -> (u8 success)
#define SWD_COMMS_CMD_SET_CLOCK		0x42	// (u32 clock) -> (u32 clock)
#define SWD_COMMS_UPLOAD_CODE_CTL	0x43	// SwdUploadableCodeCtlPacket -> SwdUploadableCodeCtlPacket
#define SWD_COMMS_RESET_PIN_CTL		0x44	// (u8 high) -> u8 success

#pragma pack(push,1)
struct SwdCommsWireBusPacket {
	uint8_t returnVal;		//SWD return val, ignored on input
	uint8_t ap;
	uint8_t a23;
	uint32_t val;
};
#pragma pack(pop)

//EACH ONE OF THESE MUST!!! include the previous's bytes up front verbatim
#pragma pack(push,1)
struct SwdCommsVerInfoRespPacketV1 {
	uint32_t swdAppVer;
};
#pragma pack(pop)

#pragma pack(push,1)
struct SwdCommsVerInfoRespPacketV2 {
	uint32_t swdAppVer;
	uint32_t flags;
	uint32_t maxXferBytes;		//SWD_COMMS_MAX_XFER_BYTES
};
#pragma pack(pop)

#pragma pack(push,1)
struct SwdCommsVerInfoRespPacketV3 {
	uint32_t swdAppVer;
	uint32_t flags;
	uint32_t maxXferBytes;		//SWD_COMMS_MAX_XFER_BYTES
	uint8_t hwType;				//SWD_COMMS_HW_TYP_*
	uint8_t hwVer;				//SWD_COMMS_HW_*_VER_*
};
#pragma pack(pop)

#pragma pack(push,1)
struct SwdCommsVerInfoRespPacketV4 {
	uint32_t swdAppVer;
	uint32_t flags;
	uint32_t maxXferBytes;		//SWD_COMMS_MAX_XFER_BYTES
	uint8_t hwType;				//SWD_COMMS_HW_TYP_*
	uint8_t hwVer;				//SWD_COMMS_HW_*_VER_*
	uint16_t millivoltsMin;		//for power suply ability (zeroes if not supported) only usable if flags has PWR_FLAG_PWR_CTRL_SETTABLE or PWR_FLAG_PWR_CTRL_ON_OFF bit set!
	uint16_t millivoltsMax;		//in case of PWR_FLAG_PWR_CTRL_ON_OFF, should be same as above
	uint16_t milliampsMax;		//max supply current
};
#pragma pack(pop)

#pragma pack(push,1)
struct SwdCommsVerInfoRespPacketV5 {
	uint32_t swdAppVer;
	uint32_t flags;
	uint32_t maxXferBytes;		//SWD_COMMS_MAX_XFER_BYTES
	uint8_t hwType;				//SWD_COMMS_HW_TYP_*
	uint8_t hwVer;				//SWD_COMMS_HW_*_VER_*
	uint16_t millivoltsMin;		//for power suply ability (zeroes if not supported) only usable if flags has PWR_FLAG_PWR_CTRL_SETTABLE or PWR_FLAG_PWR_CTRL_ON_OFF bit set!
	uint16_t millivoltsMax;		//in case of PWR_FLAG_PWR_CTRL_ON_OFF, should be same as above
	uint16_t milliampsMax;		//max supply current
	uint32_t maxClockRate;		//in Hz
};
#pragma pack(pop)


#define USB_FLAGS_FTR_OUT_SUPPORTED		0x00000001UL
#define USB_FLAGS_NEED_PACKET_PADDING	0x00000002UL
#define PWR_FLAG_PWR_CTRL_ON_OFF		0x00000004UL		//old-style power control (on or off 3.3V supply, 50mA max)
#define UART_FLAG_UART_EXISTS			0x00000008UL
#define PWR_FLAG_PWR_CTRL_SETTABLE		0x00000010UL		//new-style power control (300mA, settable voltage)
#define SWD_FLAG_CLOCK_SPEED_SETTABLE	0x00000020UL		//if set, maxClockRate in SwdCommsVerInfoRespPacket is usable
#define SWD_FLAG_MULTICORE_SUPPORT		0x00000040UL		//if set, multicore commands supported
#define SWD_FLAG_SLOW_DEBUGGER			0x00000080UL		//increases timeouts to account for slow debuggers
#define SWD_FLAG_UPLOADABLE_CODE		0x00000100UL		//supports uploadable code
#define SWD_FLAG_RESET_PIN				0x00000200UL		//sw-controllable reset pin exists

#define SWD_COMMS_HW_TYP_UNKNOWN		0xFF
#define SWD_COMMS_HW_TYP_AVR_PROTO		0x00
#define SWD_COMMS_HW_TYP_EFM			0x01

#define SWD_COMMS_HW_EFM_VER_0			0x00	//red boards, bitbanged io
#define SWD_COMMS_HW_EFM_VER_1			0x01	//black boards
#define SWD_COMMS_HW_EFM_VER_2			0x02	//long red boards, UART, switchable io, level shifters
#define SWD_COMMS_HW_EFM_VER_4			0x03	//variable-voltage supplies


#define SWD_FLAG_HAS_FPU		0x01

#define SWD_COMMS_MAX_CORES		4				//must fit into our min max packet sz

#define ERR_FLAG_TYPE_MASK		0xF0
#define ERR_FLAG_TYPE_SWD		0x00
#define ERR_FLAG_TYPE_MEMAP		0x10
#define ERR_FLAG_TYPE_CORTEX	0x20


#pragma pack(push,1)
struct SwdCommsAttachRespPacketV1 {	//if no SWD_FLAG_MULTICORE_SUPPORT
	uint16_t cortexType;            //if zero, we have error, else we have flags
	union {
		uint8_t flags;
		uint8_t error;
	};
	uint32_t romTableBase;
	uint32_t targetid;      		//DAPv2 only, else 0
};
#pragma pack(pop)

#pragma pack(push,1)
struct SwdCommsAttachRespPacketV2 {	//if SWD_FLAG_MULTICORE_SUPPORT
	uint8_t error;					//zero if none
	struct SwdCommsAttachRespCoreInfo {
		uint8_t flags;
		uint16_t identifier;		//opaque to PC app
		uint16_t cortexType;		//if zero -> no cpu here
		uint32_t romTableBase;
	} cores[];	//up to SWD_COMMS_MAX_CORES
};
#pragma pack(pop)

#define SWD_MEM_NUM_WORDS_ERROR				0xFFFF
#define SWD_COMMS_MAX_XFER_WORDS_NO_ACK		0xFFFE	/* write SWD_COMMS_MAX_XFER_WORDS and send no ack */
#pragma pack(push,1)
struct SwdCommsMemPacket {
	uint32_t addr;
	uint16_t numWords;	//up to SWD_COMMS_MAX_XFER_WORDS or SWD_MEM_NUM_WORDS_ERROR on error
	uint32_t words[];	//numWords elements for write req or read resp, else nonexistent
};
#pragma pack(pop)

#define SWD_COMMS_REG_SET_BASE	0x00	//R0..R15
#define SWD_COMMS_REG_SET_CTRL	0x01	//XPSR, MSP, PSP, CFBP, FPCSR
#define SWD_COMMS_REG_SET_FP0	0x02	//S0..S15	//this and the next must be contiguous
#define SWD_COMMS_REG_SET_FP1	0x03	//S16..S31	//this and the prev must be contiguous
#define SWD_COMMS_REG_SET_ERROR	0xFF

#define SWD_REGS_NUM_Rx(x)		(NUM_REGS * SWD_COMMS_REG_SET_BASE + x)
#define SWD_REGS_NUM_XPSR		(NUM_REGS * SWD_COMMS_REG_SET_CTRL + 0)
#define SWD_REGS_NUM_MSP		(NUM_REGS * SWD_COMMS_REG_SET_CTRL + 1)
#define SWD_REGS_NUM_PSP		(NUM_REGS * SWD_COMMS_REG_SET_CTRL + 2)
#define SWD_REGS_NUM_CFBP		(NUM_REGS * SWD_COMMS_REG_SET_CTRL + 3)
#define SWD_REGS_NUM_FPCSR		(NUM_REGS * SWD_COMMS_REG_SET_CTRL + 4)
#define SWD_REGS_NUM_Sx(x)		(NUM_REGS * SWD_COMMS_REG_SET_FP0 + (x))

#pragma pack(push,1)
struct SwdCommsRegsPacket {
	uint8_t regSet;		//or SWD_COMMS_REG_SET_ERROR
	uint32_t regs[];	//NUM_REGS or missing
};
#pragma pack(pop)

//code generation. any failure means no furthe continuation is allowed

#define SWD_UPLOAD_CTL_CODE_INIT				0
#define SWD_UPLOAD_CTL_CODE_ADD_OPCODE			1
#define SWD_UPLOAD_CTL_CODE_RUN					2
#define SWD_UPLOAD_CTL_CODE_RESP_OK				0xFE
#define SWD_UPLOAD_CTL_CODE_RESP_FAIL			0xFF

#define SWD_UPLOAD_NATIVE_FUNC_RESET_CTL		0x00		// (u32 high) -> (bool success)
#define SWD_UPLOAD_NATIVE_FUNC_SWD_WRITE		0x01		// (u1 ap, u2 a23, u32 val) -> (bool success)
#define SWD_UPLOAD_NATIVE_FUNC_SWD_READ			0x02		// (u1 ap, u2 a23) -> (bool success, u32 val)
#define SWD_UPLOAD_NATIVE_FUNC_SWD_WRITE_BITS	0x03		// (u32 bits, u32 nbits) -> () //only 8..16 bits allowed
#define SWD_UPLOAD_NATIVE_FUNC_SUPPLY_GET_V		0x04		// () -> (i32 millivolts, or negative one if func is unable to run)
#define SWD_UPLOAD_NATIVE_FUNC_SUPPLY_SET_V		0x05		// (u32 millivolts) -> (bool set)


//OPCODES: function calls
#define SWD_UPLOAD_OPCODE_CALL_NATIVE			0x00		//what func to call is in imm8, options are SWD_UPLOAD_NATIVE_FUNC_*
#define SWD_UPLOAD_OPCODE_CALL_GENERATED		0x01		//call a function we've generated (a CodegenLabel) in imm32
#define SWD_UPLOAD_OPCODE_RETURN				0x02		//return from a generated func
#define SWD_UPLOAD_OPCODE_EXIT					0x03		//exit from uploaded script with return code imm8  (any nonzero ret code is an error)

//OCODES: data processing
#define SWD_UPLOAD_OPCODE_MOV					0x20		// dstReg = srcReg
#define SWD_UPLOAD_OPCODE_NOT					0x21		// dstReg = ~srcReg
#define SWD_UPLOAD_OPCODE_ADD_REG				0x22		// dstReg += srcReg
#define SWD_UPLOAD_OPCODE_SUB_REG				0x23		// dstReg -= srcReg
#define SWD_UPLOAD_OPCODE_ADD_IMM				0x24		// dstReg += imm8
#define SWD_UPLOAD_OPCODE_SUB_IMM				0x25		// dstReg -= imm8
#define SWD_UPLOAD_OPCODE_AND					0x26		// dstReg &= srcReg
#define SWD_UPLOAD_OPCODE_ORR					0x27		// dstReg |= srcReg
#define SWD_UPLOAD_OPCODE_XOR					0x28		// dstReg ^= srcReg
#define SWD_UPLOAD_OPCODE_LSL_REG				0x29		// dstReg <<= srcReg
#define SWD_UPLOAD_OPCODE_LSR_REG				0x2a		// dstReg >>= srcReg
#define SWD_UPLOAD_OPCODE_LSL_IMM				0x2b		// dstReg <<= imm5
#define SWD_UPLOAD_OPCODE_LSR_IMM				0x2c		// dstReg >>= imm5
#define SWD_UPLOAD_OPCODE_LDR_IMM				0x2d		// dstReg = imm32

//STACK ops
#define SWD_UPLOAD_OPCODE_PUSH					0x60		// push dstReg
#define SWD_UPLOAD_OPCODE_POP					0x61		// pop  dstReg

//LABEL: normal
#define SWD_UPLOAD_OPCODE_LABEL_GET_CUR			0x80		// cur label handle returned in imm32
#define SWD_UPLOAD_OPCODE_LABEL_FREE			0x81		// free the label handle (in imm32) returned by SWD_UPLOAD_OPCODE_LABEL_GET_CUR

//LABEL: predeclared
#define SWD_UPLOAD_OPCODE_PREDECL_LBL_ALLOC		0x90		// declare a predeclared label (you can jump to it now and set its target later). returned in imm32
#define SWD_UPLOAD_OPCODE_PREDECL_LBL_TO_LBL	0x91		// convert a predeclared label to a normal label (to be used as a branch or call target) imm32 -> imm32
#define SWD_UPLOAD_OPCODE_PREDECL_LBL_FILL		0x92		// label is passed in imm32, target to point it to (a label) is passed in imm32_2
#define SWD_UPLOAD_OPCODE_PREDECL_LBL_FREE		0x93		// free the predeclared label handle (in imm32) returned by SWD_UPLOAD_OPCODE_PREDECL_LBL_ALLOC

//BRANCHES
#define SWD_UPLOAD_OPCODE_BRANCH_UNCONDITIONAL	0xA0		// goto label imm32
#define SWD_UPLOAD_OPCODE_BRANCH_UNSIGNED_GE	0xA1		// if (dstReg >= srcReg) goto label imm32
#define SWD_UPLOAD_OPCODE_BRANCH_UNSIGNED_GT	0xA2		// if (dstReg >  srcReg) goto label imm32
#define SWD_UPLOAD_OPCODE_BRANCH_UNSIGNED_LE	0xA3		// if (dstReg <= srcReg) goto label imm32
#define SWD_UPLOAD_OPCODE_BRANCH_UNSIGNED_LT	0xA4		// if (dstReg <  srcReg) goto label imm32
#define SWD_UPLOAD_OPCODE_BRANCH_EQ				0xA5		// if (dstReg == srcReg) goto label imm32
#define SWD_UPLOAD_OPCODE_BRANCH_NE				0xA6		// if (dstReg != srcReg) goto label imm32
#define SWD_UPLOAD_OPCODE_BRANCH_NEG			0xA7		// if (dstReg < 0) goto label imm32
#define SWD_UPLOAD_OPCODE_BRANCH_NOT_NEG		0xA8		// if (dstReg >= 0) goto label imm32
#define SWD_UPLOAD_OPCODE_BRANCH_ZERO			0xA9		// if (dstReg == 0) goto label imm32
#define SWD_UPLOAD_OPCODE_BRANCH_NOT_ZERO		0xAA		// if (dstReg != 0) goto label imm32
#define SWD_UPLOAD_OPCODE_BRANCH_EQ_IMM			0xAB		// if (dstReg == imm8) goto label imm32
#define SWD_UPLOAD_OPCODE_BRANCH_NOT_EQ_IMM		0xAC		// if (dstReg != imm8) goto label imm32




#pragma pack(push,1)
struct SwdUploadableCodeCtlPacket {
	uint8_t ctlCode;	//SWD_UPLOAD_CTL_CODE_*
	union {
		struct SwdUploadableCodeCtlPacketOpcode {
			uint8_t opcode;
			uint8_t dstReg;
			union {
				uint8_t srcReg;
				uint8_t imm8;
				uint8_t imm5;
			};
			uint32_t imm32;
			uint32_t imm32_2;
		} opcode;
		struct {
			uint32_t regs[4];
		}regs;
	};
};
#pragma pack(pop)
#endif
