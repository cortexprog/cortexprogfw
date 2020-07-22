#include <stdbool.h>
#include <stdarg.h>
#include <alloca.h>
#include <stdint.h>
#include "swdCommsPacket.h"
#include "codegen.h"
#include "cortex.h"
#include "memap.h"
#include "wire.h"
#include "util.h"
#include "plat.h"
#include "swd.h"



#define SWD_APP_VER				0x01040000


bool __attribute__((used)) codegenExtNativeFunc(uint8_t nativeFuncIdx, uint32_t *regs)
{
	switch(nativeFuncIdx) {
		case SWD_UPLOAD_NATIVE_FUNC_RESET_CTL:
			if (!(platGetFlags() & SWD_FLAG_RESET_PIN))
				regs[0] = false;
			else {
				wireSetResetPinVal(!!regs[0]);
				regs[0] = true;
			}
			return true;
		
		case SWD_UPLOAD_NATIVE_FUNC_SWD_WRITE:
			regs[0] = swdWireBusWrite(regs[0], regs[1], regs[2]);
			return true;
		
		case SWD_UPLOAD_NATIVE_FUNC_SWD_READ:
			regs[0] = swdWireBusRead(regs[0], regs[1], WRAP_UNALIGNED_POINTER_32(&regs[1]));
			return true;
		
		case SWD_UPLOAD_NATIVE_FUNC_SWD_WRITE_BITS:
			llWireBusWriteBits(regs[0], regs[1]);
			return true;
			
		case SWD_UPLOAD_NATIVE_FUNC_SUPPLY_GET_V:
			regs[0] = (int32_t)platGetCurSupplyVoltage();
			return true;
		
		case SWD_UPLOAD_NATIVE_FUNC_SUPPLY_SET_V:
			if (platGetFlags() & PWR_FLAG_PWR_CTRL_SETTABLE)
				regs[0] = platPowerVaribleSet(regs[0]);
			else if (platGetFlags() & PWR_FLAG_PWR_CTRL_SETTABLE)
				regs[0] = (regs[0] == 0 || regs[0] == 3300) && platPowerOnOffSet(!!regs[0]);
			else
				regs[0] = 0;
			return true;
		
		default:
			return false;
	}
}

static bool codegenSwdCmdHandleOpcode(struct SwdUploadableCodeCtlPacketOpcode *opcode)
{
	switch (opcode->opcode) {
		case SWD_UPLOAD_OPCODE_CALL_NATIVE:
			return codegenEmitCallToNative(opcode->imm8);
		
		case SWD_UPLOAD_OPCODE_CALL_GENERATED:
			return codegenEmitCallToGenerated((struct CodegenLabel*)(uintptr_t)opcode->imm32);
		
		case SWD_UPLOAD_OPCODE_RETURN:
			return codegenEmitReturn();
		
		case SWD_UPLOAD_OPCODE_EXIT:
			return codegenEmitExit(opcode->imm8);
		
		case SWD_UPLOAD_OPCODE_MOV:
			return codegenEmitMov(opcode->dstReg, opcode->srcReg);
		
		case SWD_UPLOAD_OPCODE_NOT:
			return codegenEmitNot(opcode->dstReg, opcode->srcReg);
		
		case SWD_UPLOAD_OPCODE_ADD_REG:
			return codegenEmitAdd(opcode->dstReg, opcode->srcReg);
		
		case SWD_UPLOAD_OPCODE_SUB_REG:
			return codegenEmitSub(opcode->dstReg, opcode->srcReg);
		
		case SWD_UPLOAD_OPCODE_ADD_IMM:
			return codegenEmitAddImm8(opcode->dstReg, opcode->imm8);
		
		case SWD_UPLOAD_OPCODE_SUB_IMM:
			return codegenEmitSubImm8(opcode->dstReg, opcode->imm8);
		
		case SWD_UPLOAD_OPCODE_AND:
			return codegenEmitAnd(opcode->dstReg, opcode->srcReg);
		
		case SWD_UPLOAD_OPCODE_ORR:
			return codegenEmitOrr(opcode->dstReg, opcode->srcReg);
		
		case SWD_UPLOAD_OPCODE_XOR:
			return codegenEmitXor(opcode->dstReg, opcode->srcReg);
		
		case SWD_UPLOAD_OPCODE_LSL_REG:
			return codegenEmitLslReg(opcode->dstReg, opcode->srcReg);
		
		case SWD_UPLOAD_OPCODE_LSR_REG:
			return codegenEmitLsrReg(opcode->dstReg, opcode->srcReg);
		
		case SWD_UPLOAD_OPCODE_LSL_IMM:
			return codegenEmitLslImm(opcode->dstReg, opcode->imm5);
		
		case SWD_UPLOAD_OPCODE_LSR_IMM:
			return codegenEmitLsrImm(opcode->dstReg, opcode->imm5);
		
		case SWD_UPLOAD_OPCODE_LDR_IMM:
			return codegenEmitLoadImm(opcode->dstReg, opcode->imm32);
		
		case SWD_UPLOAD_OPCODE_PUSH:
			return codegenEmitStackPush(opcode->dstReg);
		
		case SWD_UPLOAD_OPCODE_POP:
			return codegenEmitStackPop(opcode->dstReg);
		
		case SWD_UPLOAD_OPCODE_LABEL_GET_CUR:
			opcode->imm32 = (uintptr_t)codegenLabelGetCur();
			return !!opcode->imm32;
		
		case SWD_UPLOAD_OPCODE_LABEL_FREE:
			return codegenLabelFree((struct CodegenLabel*)(uintptr_t)opcode->imm32);
		
		case SWD_UPLOAD_OPCODE_PREDECL_LBL_ALLOC:
			opcode->imm32 = (uintptr_t)codegenAllocPredeclaredLabel();
			return !!opcode->imm32;
		
		case SWD_UPLOAD_OPCODE_PREDECL_LBL_TO_LBL:
			opcode->imm32 = (uintptr_t)codegenGetPredeclaredLabelBranchTarget((struct CodegenPredeclaredLabel*)(uintptr_t)opcode->imm32);
			return !!opcode->imm32;
		
		case SWD_UPLOAD_OPCODE_PREDECL_LBL_FILL:
			return codegenGetPredeclaredLabelFillTarget((struct CodegenPredeclaredLabel*)(uintptr_t)opcode->imm32, (struct CodegenLabel*)(uintptr_t)opcode->imm32_2);
		
		case SWD_UPLOAD_OPCODE_PREDECL_LBL_FREE:
			return codegenGetPredeclaredLabelFree((struct CodegenPredeclaredLabel*)(uintptr_t)opcode->imm32);
		
		case SWD_UPLOAD_OPCODE_BRANCH_UNCONDITIONAL:
			return codegenEmitBranch((struct CodegenLabel*)(uintptr_t)opcode->imm32);
		
		case SWD_UPLOAD_OPCODE_BRANCH_UNSIGNED_GE:
			return codegenEmitBranchIfUnsignedGe(opcode->dstReg, opcode->srcReg, (struct CodegenLabel*)(uintptr_t)opcode->imm32);
		
		case SWD_UPLOAD_OPCODE_BRANCH_UNSIGNED_GT:
			return codegenEmitBranchIfUnsignedGt(opcode->dstReg, opcode->srcReg, (struct CodegenLabel*)(uintptr_t)opcode->imm32);
		
		case SWD_UPLOAD_OPCODE_BRANCH_UNSIGNED_LE:
			return codegenEmitBranchIfUnsignedLe(opcode->dstReg, opcode->srcReg, (struct CodegenLabel*)(uintptr_t)opcode->imm32);
		
		case SWD_UPLOAD_OPCODE_BRANCH_UNSIGNED_LT:
			return codegenEmitBranchIfUnsignedLt(opcode->dstReg, opcode->srcReg, (struct CodegenLabel*)(uintptr_t)opcode->imm32);
		
		case SWD_UPLOAD_OPCODE_BRANCH_EQ:
			return codegenEmitBranchIfEq(opcode->dstReg, opcode->srcReg, (struct CodegenLabel*)(uintptr_t)opcode->imm32);
		
		case SWD_UPLOAD_OPCODE_BRANCH_NE:
			return codegenEmitBranchIfNe(opcode->dstReg, opcode->srcReg, (struct CodegenLabel*)(uintptr_t)opcode->imm32);
		
		case SWD_UPLOAD_OPCODE_BRANCH_NEG:
			return codegenEmitBranchIfNeg(opcode->dstReg, (struct CodegenLabel*)(uintptr_t)opcode->imm32);
		
		case SWD_UPLOAD_OPCODE_BRANCH_NOT_NEG:
			return codegenEmitBranchIfNotNeg(opcode->dstReg, (struct CodegenLabel*)(uintptr_t)opcode->imm32);
		
		case SWD_UPLOAD_OPCODE_BRANCH_ZERO:
			return codegenEmitBranchIfZero(opcode->dstReg, (struct CodegenLabel*)(uintptr_t)opcode->imm32);
		
		case SWD_UPLOAD_OPCODE_BRANCH_NOT_ZERO:
			return codegenEmitBranchIfNonzero(opcode->dstReg, (struct CodegenLabel*)(uintptr_t)opcode->imm32);
		
		case SWD_UPLOAD_OPCODE_BRANCH_EQ_IMM:
			return codegenEmitBranchIfEqImm8(opcode->dstReg, opcode->imm8, (struct CodegenLabel*)(uintptr_t)opcode->imm32);
		
		case SWD_UPLOAD_OPCODE_BRANCH_NOT_EQ_IMM:
			return codegenEmitBranchIfNotEqImm8(opcode->dstReg, opcode->imm8, (struct CodegenLabel*)(uintptr_t)opcode->imm32);
		
		default:
			return false;
	}
}

static void codegenSwdCmdHandle(struct SwdUploadableCodeCtlPacket* pkt)
{
	uint32_t regs[4];
	uint32_t i;
	
	switch (pkt->ctlCode) {
		case SWD_UPLOAD_CTL_CODE_INIT:
			pkt->ctlCode = codegenInit() ? SWD_UPLOAD_CTL_CODE_RESP_OK : SWD_UPLOAD_CTL_CODE_RESP_FAIL;
			break;
		
		case SWD_UPLOAD_CTL_CODE_ADD_OPCODE:
			pkt->ctlCode = codegenSwdCmdHandleOpcode(&pkt->opcode) ? SWD_UPLOAD_CTL_CODE_RESP_OK : SWD_UPLOAD_CTL_CODE_RESP_FAIL;
			break;
		
		case SWD_UPLOAD_CTL_CODE_RUN:
			for (i = 0; i < CODEGEN_RUN_NUM_REGS; i++)
				regs[i] = pkt->regs.regs[i];
			pkt->ctlCode = codegenRun(regs) ? SWD_UPLOAD_CTL_CODE_RESP_FAIL : SWD_UPLOAD_CTL_CODE_RESP_OK;
			for (i = 0; i < CODEGEN_RUN_NUM_REGS; i++)
				pkt->regs.regs[i] = regs[i];
			break;
		
		default:
			pkt->ctlCode = SWD_UPLOAD_CTL_CODE_RESP_FAIL;
			break;
	}
}

void main(void)
{
	packet_sz_t maxPkt, maxWordsTxRx;
	uint8_t blFlags, blVer = 0;
	
	//get caps
	getUsbCaps(&maxPkt, &blFlags);
	if (blFlags & BL_CAP_BL_VERSION_REPORTING)
		blVer = blGetBlVersion();
	
	platInit(blFlags & BL_CAP_FLAGS_CDC_SUPPORTED, blVer);
	maxWordsTxRx = (packet_sz_t)(maxPkt - sizeof(struct CommsPacket) - sizeof(struct SwdCommsMemPacket)) / sizeof(uint32_t);

	wireInit();



	while (1) {
		packet_sz_t replyPayloadLen = PACKET_RX_FAIL, cmdPayloadLen;
		uint_fast8_t i, j = 0;
		struct CommsPacket *cp;
		
		platWork();
		usbWork();

		if (packetCanSend() && (cmdPayloadLen = packetRx(&cp)) != PACKET_RX_FAIL) {
			
			switch (cp->cmd) {
			
				case SWD_COMMS_CMD_VER_INFO: {
					struct SwdCommsVerInfoRespPacketV5* rsp = (struct SwdCommsVerInfoRespPacketV5*)cp->payload;
					
					if (cmdPayloadLen)
						break;
					
					rsp->swdAppVer = SWD_APP_VER;
					rsp->flags = platGetFlags();
					rsp->maxXferBytes = maxPkt;
					rsp->hwType = HW_TYPE;
					rsp->hwVer = platGetHwVerForComms();
					if (platGetFlags() & (PWR_FLAG_PWR_CTRL_SETTABLE | PWR_FLAG_PWR_CTRL_ON_OFF)) {
						
						uint16_t millivoltsMin, millivoltsMax, milliampsMax;	//canot pass pointer ot rsp since it mightbe unaligned
						
						platGetSupplyAbilities(&millivoltsMin, &millivoltsMax, &milliampsMax);
						rsp->millivoltsMin = millivoltsMin;
						rsp->millivoltsMax = millivoltsMax;
						rsp->milliampsMax = milliampsMax;
					}
					if (platGetFlags() & SWD_FLAG_CLOCK_SPEED_SETTABLE)
						rsp->maxClockRate = platGetSwdMaxClkSpeed();
					replyPayloadLen = sizeof(*rsp);
					
					if (blFlags & BL_CAP_FLAGS_NEED_PADDING)
						rsp->flags |= USB_FLAGS_NEED_PACKET_PADDING;
					break;
				}

				case SWD_COMMS_CMD_ATTACH: {
					
					uint8_t err, apIdx = 0, ahbEnumState = 0;
					bool found = false;
					
					if (cmdPayloadLen)
						break;
					
					wireSwdSendKey();
					err = swdDapInit();

					if (platGetFlags() & SWD_FLAG_MULTICORE_SUPPORT) {
						struct SwdCommsAttachRespPacketV2* rpp = (struct SwdCommsAttachRespPacketV2*)cp->payload;
						uint8_t lastCpuErr = 0, numCores = 0, cpuState;
						
						rpp->error = 0;
						
						if (err != SWD_OK)
							rpp->error = ERR_FLAG_TYPE_SWD | err;
						else {
							
							while(numCores < SWD_COMMS_MAX_CORES && (err = swdAttachAndEnumApsWithAhbs(&ahbEnumState)) == SWD_OK) {	//grab last ap
								uint8_t flags = 0;
								
								apIdx = swdStateToApIdx(ahbEnumState);
								
								err = memapInit(apIdx, WRAP_UNALIGNED_POINTER_32(&rpp->cores[numCores].romTableBase));
								if (err != MEMAP_OK) {
									numCores = 0;
									rpp->error = ERR_FLAG_TYPE_MEMAP | err;
									break;
								}
								lastCpuErr = cortexInit(WRAP_UNALIGNED_POINTER_16(&rpp->cores[numCores].cortexType), &cpuState);
								if (lastCpuErr != CORTEX_INIT_OK)	//no cpu?
									continue;
								
								if (cortexHaveFpu())
									flags |= SWD_FLAG_HAS_FPU;
								rpp->cores[numCores].identifier = (((uint16_t)cpuState) << 8) + apIdx;
								rpp->cores[numCores].flags = flags;
								numCores++;
							}
							
							//we must reinit memap since swdEnum clobbered SELECT regs
							memapReselect(apIdx);
							
							if (!numCores) {
								if (lastCpuErr)
									rpp->error = ERR_FLAG_TYPE_CORTEX | lastCpuErr;
								else
									rpp->error = ERR_FLAG_TYPE_SWD | err;		//from swdAttachAndEnumApsWithAhbs
							}
						}
						
						replyPayloadLen = (uint8_t)sizeof(struct SwdCommsAttachRespPacketV2) + (uint8_t)sizeof(struct SwdCommsAttachRespCoreInfo) * numCores;
					}
					else {
						struct SwdCommsAttachRespPacketV1* rpp = (struct SwdCommsAttachRespPacketV1*)cp->payload;
						
						rpp->cortexType = 0;
						rpp->flags = 0;
						rpp->error = 0;
						rpp->targetid = 0;	//always, because we don't need it
						
						if (err != SWD_OK)
							rpp->error = ERR_FLAG_TYPE_SWD | err;
						else {
							
							while(swdAttachAndEnumApsWithAhbs(&ahbEnumState) == SWD_OK) {	//grab last ap
								apIdx = swdStateToApIdx(ahbEnumState);
								found = true;
							}
						
							if(err == SWD_ATTACH_AHB_NOT_FOUND && found)
								err = SWD_OK;
							
							if (err != SWD_OK)
								rpp->error = ERR_FLAG_TYPE_SWD | err;
							else {
								err = memapInit(apIdx, WRAP_UNALIGNED_POINTER_32(&rpp->romTableBase));
								if (err != MEMAP_OK)
									rpp->error = ERR_FLAG_TYPE_MEMAP | err;
								else {
									err = cortexInit(WRAP_UNALIGNED_POINTER_16(&rpp->cortexType), NULL);
									if (err != CORTEX_INIT_OK) {
										rpp->cortexType = 0;
										rpp->error = ERR_FLAG_TYPE_CORTEX | err;
									}
									else {
										if (cortexHaveFpu())
											rpp->flags |= SWD_FLAG_HAS_FPU;
									}
								}
							}
						}
						replyPayloadLen = sizeof(struct SwdCommsAttachRespPacketV1);
					}
					
					break;
				}
				
				case SWD_COMMS_CMD_MEM_READ: {
					struct SwdCommsMemPacket* rpp = (struct SwdCommsMemPacket*)cp->payload;
					uint32_t numWords = UNALIGNED_16(&rpp->numWords);
					
					if (cmdPayloadLen != sizeof(struct SwdCommsMemPacket))
						break;
					
					replyPayloadLen = sizeof(struct SwdCommsMemPacket);
					if (numWords && (numWords <= maxWordsTxRx) && memapSetAddr(UNALIGNED_32(&rpp->addr)) && memapReadMultiple(WRAP_UNALIGNED_POINTER_32(rpp->words), numWords))
						replyPayloadLen += rpp->numWords * sizeof(uint32_t);
					else
						UNALIGNED_16(&rpp->numWords) = SWD_MEM_NUM_WORDS_ERROR;
					break;
				}

				case SWD_COMMS_CMD_MEM_WRITE: {
					struct SwdCommsMemPacket* rpp = (struct SwdCommsMemPacket*)cp->payload;
					uint32_t numWords = UNALIGNED_16(&rpp->numWords);
					
					if (cmdPayloadLen < sizeof(struct SwdCommsMemPacket))
						break;
					
					
					replyPayloadLen = sizeof(struct SwdCommsMemPacket);
					if (numWords == SWD_COMMS_MAX_XFER_WORDS_NO_ACK){
						replyPayloadLen = PACKET_RX_FAIL;
						numWords = maxWordsTxRx;
					}
					
					if (!numWords || numWords > maxWordsTxRx)
						numWords = SWD_MEM_NUM_WORDS_ERROR;
					else if (cmdPayloadLen != sizeof(struct SwdCommsMemPacket) + sizeof(uint32_t) * numWords)
						numWords = SWD_MEM_NUM_WORDS_ERROR;
					else if (!memapSetAddr(UNALIGNED_32(&rpp->addr)) || !memapWriteMultiple(WRAP_UNALIGNED_POINTER_32(rpp->words), numWords))
						numWords = SWD_MEM_NUM_WORDS_ERROR;

					UNALIGNED_16(&rpp->numWords) = numWords;
					break;
				}

				case SWD_COMMS_CMD_REGS_READ: {
					struct SwdCommsRegsPacket* rpp = (struct SwdCommsRegsPacket*)cp->payload;
					
					if (cmdPayloadLen != (int8_t)sizeof(struct SwdCommsRegsPacket))
						break;
					
					replyPayloadLen = sizeof(struct SwdCommsRegsPacket) + sizeof(uint32_t) * NUM_REGS;
					switch (rpp->regSet) {
						case SWD_COMMS_REG_SET_BASE:
							for (i = 0; i < NUM_REGS; i++)
								if (!cortexRegRead(i, WRAP_UNALIGNED_POINTER_32(rpp->regs + SWD_REGS_NUM_Rx(i) % NUM_REGS)))
									rpp->regSet = SWD_COMMS_REG_SET_ERROR;
							break;

						case SWD_COMMS_REG_SET_CTRL:
							if (!cortexRegRead(CORTEX_REG_XPSR, WRAP_UNALIGNED_POINTER_32(rpp->regs + SWD_REGS_NUM_XPSR % NUM_REGS)) ||
									!cortexRegRead(CORTEX_REG_MSP, WRAP_UNALIGNED_POINTER_32(rpp->regs + SWD_REGS_NUM_MSP % NUM_REGS)) ||
									!cortexRegRead(CORTEX_REG_PSP, WRAP_UNALIGNED_POINTER_32(rpp->regs + SWD_REGS_NUM_PSP % NUM_REGS)) ||
									!cortexRegRead(CORTEX_REG_CFBP, WRAP_UNALIGNED_POINTER_32(rpp->regs + SWD_REGS_NUM_CFBP % NUM_REGS)) ||
									!cortexRegRead(CORTEX_REG_FPCSR, WRAP_UNALIGNED_POINTER_32(rpp->regs + SWD_REGS_NUM_FPCSR % NUM_REGS)))
								rpp->regSet = SWD_COMMS_REG_SET_ERROR;
							break;
						
						case SWD_COMMS_REG_SET_FP1:
							j = 16;
							//fallthrough
							
						case SWD_COMMS_REG_SET_FP0:
							for (i = 0; i < NUM_REGS; i++)
								if (!cortexRegRead(CORTEX_REG_Sx(j + i), WRAP_UNALIGNED_POINTER_32(rpp->regs + SWD_REGS_NUM_Sx(i) % NUM_REGS)))
									rpp->regSet = SWD_COMMS_REG_SET_ERROR;
							break;
						default:
							rpp->regSet = SWD_COMMS_REG_SET_ERROR;
							break;
					}
					break;
				}

				case SWD_COMMS_CMD_REGS_WRITE: {
					struct SwdCommsRegsPacket* rpp = (struct SwdCommsRegsPacket*)cp->payload;
					
					if (cmdPayloadLen != (int8_t)sizeof(struct SwdCommsRegsPacket) + (int8_t)sizeof(uint32_t) * NUM_REGS)
						break;
					
					replyPayloadLen = sizeof(struct SwdCommsRegsPacket);
					switch (rpp->regSet) {
						case SWD_COMMS_REG_SET_BASE:
							for (i = 0; i < NUM_REGS; i++)
								if (!cortexRegWrite(i, UNALIGNED_32(&rpp->regs[i])))
									rpp->regSet = SWD_COMMS_REG_SET_ERROR;
							break;
						
						case SWD_COMMS_REG_SET_CTRL:
							if (!cortexRegWrite(CORTEX_REG_XPSR, UNALIGNED_32(&rpp->regs[0])) ||
									!cortexRegWrite(CORTEX_REG_MSP, UNALIGNED_32(&rpp->regs[1])) ||
									!cortexRegWrite(CORTEX_REG_PSP, UNALIGNED_32(&rpp->regs[2])) ||
									!cortexRegWrite(CORTEX_REG_CFBP, UNALIGNED_32(&rpp->regs[3])) ||
									!cortexRegWrite(CORTEX_REG_FPCSR, UNALIGNED_32(&rpp->regs[4])))
								rpp->regSet = SWD_COMMS_REG_SET_ERROR;
							break;
						
						case SWD_COMMS_REG_SET_FP1:
							j = 16;
							//fallthrough
							
						case SWD_COMMS_REG_SET_FP0:
							for (i = 0; i < NUM_REGS; i++)
								if (!cortexRegWrite(CORTEX_REG_Sx(j + i), UNALIGNED_32(&rpp->regs[i])))
									rpp->regSet = SWD_COMMS_REG_SET_ERROR;
							break;
						default:
							rpp->regSet = SWD_COMMS_REG_SET_ERROR;
					}
					break;
				}

				case SWD_COMMS_CMD_GO: {
					if (cmdPayloadLen)
						break;
						
					replyPayloadLen = 1;
					
					cp->payload[0] = cortexGo();
					break;
				}
				
				case SWD_COMMS_CMD_RESET: {
					if (cmdPayloadLen)
						break;
					
					replyPayloadLen = 1;
					
					cp->payload[0] = cortexReset();
					cortexGetStopReason();	//just to clear the bits
					break;
				}
				
				case SWD_COMMS_CMD_STOP:{
					if (cmdPayloadLen)
						break;
					
					replyPayloadLen = 1;
					
					if (cortexStop())
						goto send_stop_reason;
					
					cp->payload[0] = CORTEX_W_FAIL;
					break;
				}
				
				case SWD_COMMS_CMD_IS_STOPPED: {
					if (cmdPayloadLen)
						break;
						
					replyPayloadLen = 1;
					
			send_stop_reason:
					cp->payload[0] = cortexGetStopReason();
					break;
				}
				
				case SWD_COMMS_CMD_SINGLE_STEP: {
					if (cmdPayloadLen)
						break;
					
					replyPayloadLen = 1;
					
					if (cortexStep())
						goto send_stop_reason;
					cp->payload[0] = CORTEX_W_FAIL;
					break;
				}
				
				case SWD_COMMS_CMD_SELECT_CPU: {
					if (platGetFlags() & SWD_FLAG_MULTICORE_SUPPORT) {
						uint16_t cpuIdent;
						
						if (cmdPayloadLen != 2)
							break;
						cpuIdent = UNALIGNED_16(cp->payload);
						cp->payload[0] = memapReselect((uint8_t)cpuIdent) && cortexFastCpuSwitch(cpuIdent >> 8);
						replyPayloadLen = 1;
					}
					break;
				};
				
				case SWD_COMMS_SWD_WIRE_BUS_R: {
					struct SwdCommsWireBusPacket* rpp = (struct SwdCommsWireBusPacket*)cp->payload;
					
					
					if (cmdPayloadLen != sizeof(struct SwdCommsWireBusPacket))
						break;
					
					rpp->returnVal = llWireBusRead(rpp->ap, rpp->a23, WRAP_UNALIGNED_POINTER_32(&rpp->val));
					replyPayloadLen = cmdPayloadLen;
					break;
				}
				
				case SWD_COMMS_SWD_WIRE_BUS_W: {
					struct SwdCommsWireBusPacket* rpp = (struct SwdCommsWireBusPacket*)cp->payload;
					
					
					if (cmdPayloadLen != sizeof(struct SwdCommsWireBusPacket))
						break;
					
					rpp->returnVal = llWireBusWrite(rpp->ap, rpp->a23, UNALIGNED_32(&rpp->val));
					replyPayloadLen = cmdPayloadLen;
					break;
				}
								
				case SWD_TRACE_LOG_READ: {
					uint32_t val, addr = UNALIGNED_32(cp->payload);
					uint32_t pos = 0;
					
					if (cmdPayloadLen != sizeof(uint32_t))
						break;
					
					while(pos < maxPkt - sizeof(struct CommsPacket) && memapReadAddr(addr, WRAP_UNALIGNED_POINTER_32(&val)) && (val >> 31)) {
						memapWriteAddr(addr, 0);
						cp->payload[pos++] = val;
					}
					
					replyPayloadLen = pos;
					break;
				}
	
				case SWD_POWER_CTRL: {
					
					if (cmdPayloadLen == 1) {	//simple-style power control
						cp->payload[0] = (platGetFlags() & PWR_FLAG_PWR_CTRL_ON_OFF) && platPowerOnOffSet(!!cp->payload[0]);
					}
					else if (cmdPayloadLen == 2) {	//new-style varible power control
						
						uint32_t mv = UNALIGNED_16(cp->payload);
						
						cp->payload[0] = (platGetFlags() & PWR_FLAG_PWR_CTRL_SETTABLE) && platPowerVaribleSet(mv);
					}
					else {
						
						//error - some kind of power contorl we do not support...
						break;
					}
					
					replyPayloadLen = 1;
					break;
				}
	
				case SWD_COMMS_CMD_SET_CLOCK: {
					if (platGetFlags() & SWD_FLAG_CLOCK_SPEED_SETTABLE) {
						if (cmdPayloadLen != 4)
							break;
						UNALIGNED_32(cp->payload) = platSetSwdClockSpeed(UNALIGNED_32(cp->payload));
						replyPayloadLen = 4;
					}
					break;
				}
				
				case SWD_COMMS_UPLOAD_CODE_CTL: {
					if (platGetFlags() & SWD_FLAG_UPLOADABLE_CODE) {
						if (cmdPayloadLen != sizeof(struct SwdUploadableCodeCtlPacket))
							break;
						codegenSwdCmdHandle((struct SwdUploadableCodeCtlPacket*)cp->payload);
						replyPayloadLen = sizeof(struct SwdUploadableCodeCtlPacket);
					}
					break;
				}
				
				case SWD_COMMS_RESET_PIN_CTL: {
					if (platGetFlags() & SWD_FLAG_RESET_PIN) {
						if (cmdPayloadLen != sizeof(uint8_t))
							break;
						
						wireSetResetPinVal(!!cp->payload[0]);
						cp->payload[0] = true;
						replyPayloadLen = sizeof(uint8_t);
					}
					break;
				}
				
				//if we get a "boot app", reply to help upload tool feel better
				case COMMS_CMD_BOOT_APP:
					cp->payload[0] = 1;
					replyPayloadLen = 1;
					break;

				//if we get a "get bl info packet", reboot to bootloader to allow easier update
				case COMMS_CMD_GET_INFO:
					platDeinit();
					bootloader(true);
					break;

				default:
					//ps(PSTR("PKT 0x"));
					//px(cp->cmd, 2, true);
					break;
			}
			
			//order may matter here (avr has opposite order)
			if (replyPayloadLen != PACKET_RX_FAIL)
				packetSend(cp, replyPayloadLen);
			packetRxRelease();
		}
	}


	while(1);
}

