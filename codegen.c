#include <string.h>
#include "codegen.h"
#include "plat.h"


#define LABEL_MASK_NORMAL				0x8000
#define LABEL_MASK_PREDECLARED			0x4000

/* opcodes:

	BL			000a aaaa aaaa aaaa		//absolute addr (not relative)
	B			001a aaaa aaaa aaaa		//absolute addr (not relative), encoding makes sure instr is never zero when valid
	SKcc_imm	010c crrr iiii iiii		//cc: EQ NE LT GE, skips next instre if condition is NOT met
	
	add_imm		0110 0rrr iiii iiii
	sub_imm		0110 1rrr iiii iiii
	mov_imm8	0111 0rrr iiii iiii
	mov_imm24	0111 1rrr iiii iiii IIII IIII IIII IIII
	
	mov			1000 0000 __RR Rrrr		//rrr = dest, RRR = src
	not			1000 0001 __RR Rrrr
	add_reg		1000 0010 __RR Rrrr
	sub_reg		1000 0011 __RR Rrrr
	and			1000 0100 __RR Rrrr
	orr			1000 0101 __RR Rrrr
	xor			1000 0110 __RR Rrrr
	lsl_reg		1000 0111 __RR Rrrr
	lsr_reg		1000 1000 __RR Rrrr
	SKcc_reg	1000 1001 ccRR Rrrr		//cc: EQ NE LT GE, skips next instre if condition is NOT met
	
	lsl_imm		1000 1010 rrri iiii
	lsr_imm		1000 1011 rrri iiii
	
	push		1000 1100 ____ _rrr
	pop			1000 1101 ____ _rrr
	ret			1000 1110 ____ ____
	mov_imm32	1000 1111 ____ _rrr		IIII IIII IIII IIII IIII IIII IIII IIII
	
	nativecall	1111 0___ nnnn nnnn		// nn = which native func?
	exit		1111 1___ iiii iiii		//on purpose so that FF FF is "exit 0xff"
*/

static uint16_t mOpcodes[MAX_OPCODES];
static uint16_t mOutputPosHead, mOutputPosTail;


uint8_t codegenRun(uint32_t *regsInOut)
{
	return codegenRunInternal(regsInOut, mOpcodes);
}

bool codegenInit(void)
{
	mOutputPosHead = 0;
	mOutputPosTail = MAX_OPCODES;
	
	memset(mOpcodes, 0xFF, sizeof(mOpcodes));
	
	return true;
}

static int32_t codegenAllocFront(uint32_t num)
{
	uint32_t ret;
	
	if (mOutputPosTail - mOutputPosHead < (int32_t)num)
		return -1;
	
	ret = mOutputPosHead;
	mOutputPosHead += num;
	
	return ret;
}

static int32_t codegenAllocRear(uint32_t num)
{
	if (mOutputPosTail - mOutputPosHead < (int32_t)num)
		return -1;
	
	mOutputPosTail -= num;
	return mOutputPosTail;
}

static void codegenEmitAt(uint32_t pos, uint32_t opcode)
{
	mOpcodes[pos] = opcode;
}

static uint32_t codegenReadEmittedAt(uint32_t pos)
{
	return mOpcodes[pos];
}

static bool codegenEmit(uint32_t opcode)
{
	int32_t pos = codegenAllocFront(1);
	
	if (pos < 0)
		return false;
	
	codegenEmitAt(pos, opcode);
	return true;
}

static int32_t codegenVerifyLabel(struct CodegenLabel* lblIn)	//return pos
{
	uint32_t lbl = (uint32_t)(uintptr_t)lblIn;
	
	if (!(lbl & LABEL_MASK_NORMAL))
		return -1;
	
	lbl &=~ LABEL_MASK_NORMAL;
	
	if (lbl <= mOutputPosHead)
		return lbl;
	
	if (lbl < mOutputPosTail)
		return -1;
	
	if (lbl >= MAX_OPCODES)
		return -1;
	
	return lbl;
}

static int32_t codegenVerifyPredeclaredLabel(struct CodegenPredeclaredLabel* lblIn)	//return pos
{
	uint32_t lbl = (uint32_t)(uintptr_t)lblIn;
	
	if (!(lbl & LABEL_MASK_PREDECLARED))
		return -1;
	
	lbl &=~ LABEL_MASK_PREDECLARED;
	
	if (lbl < mOutputPosTail)	//must be at end
		return -1;
	
	if (lbl >= MAX_OPCODES)
		return -1;
	
	return lbl;
}

static int32_t codegenInstrForJump(struct CodegenLabel* dst, bool withLink)
{
	int32_t pos = codegenVerifyLabel(dst);
	
	if (pos < 0)
		return -1;
	
	return (withLink ? 0x0000 : 0x2000) | pos;
}

bool codegenEmitBranchPossiblyWithLink(struct CodegenLabel* dst, bool withLink)
{
	int32_t instr = codegenInstrForJump(dst, withLink);
	
	return instr >=0 && codegenEmit(instr);
}

bool codegenEmitBranch(struct CodegenLabel* dst)
{
	return codegenEmitBranchPossiblyWithLink(dst, false);
}

static bool codegenEmitConditionalBranchTwoRegs(uint32_t lhsReg, uint32_t rhsReg, struct CodegenLabel* dst, uint32_t cc)
{
	return codegenEmit(0x8900 | (cc << 6) | (lhsReg << 0) | (rhsReg << 3)) && codegenEmitBranch(dst);
}

static bool codegenEmitConditionalBranchRegWithImm(uint32_t reg, uint32_t imm8, struct CodegenLabel* dst, uint32_t cc)
{
	return codegenEmit(0x4000 | (cc << 11) | (reg << 8) | (imm8 << 0)) && codegenEmitBranch(dst);
}

bool codegenEmitBranchIfUnsignedGe(uint32_t lhsReg, uint32_t rhsReg, struct CodegenLabel* dst)
{
	return codegenEmitConditionalBranchTwoRegs(lhsReg, rhsReg, dst, 3);	//GE
}

bool codegenEmitBranchIfUnsignedGt(uint32_t lhsReg, uint32_t rhsReg, struct CodegenLabel* dst)
{
	return codegenEmitConditionalBranchTwoRegs(rhsReg, lhsReg, dst, 2);	//LT, swapped
}

bool codegenEmitBranchIfUnsignedLe(uint32_t lhsReg, uint32_t rhsReg, struct CodegenLabel* dst)
{
	return codegenEmitConditionalBranchTwoRegs(rhsReg, lhsReg, dst, 3);	//GE, swapped
}

bool codegenEmitBranchIfUnsignedLt(uint32_t lhsReg, uint32_t rhsReg, struct CodegenLabel* dst)
{
	return codegenEmitConditionalBranchTwoRegs(lhsReg, rhsReg, dst, 2);	//LT
}

bool codegenEmitBranchIfEq(uint32_t lhsReg, uint32_t rhsReg, struct CodegenLabel* dst)
{
	return codegenEmitConditionalBranchTwoRegs(lhsReg, rhsReg, dst, 0);	//EQ
}

bool codegenEmitBranchIfNe(uint32_t lhsReg, uint32_t rhsReg, struct CodegenLabel* dst)
{
	return codegenEmitConditionalBranchTwoRegs(lhsReg, rhsReg, dst, 1);	//NE
}

bool codegenEmitBranchIfNeg(uint32_t reg, struct CodegenLabel* dst)
{
	return codegenEmitConditionalBranchRegWithImm(reg, 0, dst, 2);	//LT
}

bool codegenEmitBranchIfNotNeg(uint32_t reg, struct CodegenLabel* dst)
{
	return codegenEmitConditionalBranchRegWithImm(reg, 0, dst, 3);	//GE
}

bool codegenEmitBranchIfEqImm8(uint32_t reg, uint32_t imm8, struct CodegenLabel* dst)
{
	return codegenEmitConditionalBranchRegWithImm(reg, imm8, dst, 0);	//EQ
}

bool codegenEmitBranchIfNotEqImm8(uint32_t reg, uint32_t imm8, struct CodegenLabel* dst)
{
	return codegenEmitConditionalBranchRegWithImm(reg, imm8, dst, 1);	//NE
}

bool codegenEmitBranchIfZero(uint32_t reg, struct CodegenLabel* dst)
{
	return codegenEmitBranchIfEqImm8(reg, 0, dst);
}

bool codegenEmitBranchIfNonzero(uint32_t reg, struct CodegenLabel* dst)
{
	return codegenEmitBranchIfNotEqImm8(reg, 0, dst);
}

static bool codegenEmitDpTwoReg(uint32_t lhsReg, uint32_t rhsReg, uint32_t op)
{
	return codegenEmit(op | (lhsReg << 0) | (rhsReg << 3));
}

bool codegenEmitMov(uint32_t dstReg, uint32_t srcReg)
{
	return codegenEmitDpTwoReg(dstReg, srcReg, 0x8000);
}

bool codegenEmitNot(uint32_t dstReg, uint32_t srcReg)
{
	return codegenEmitDpTwoReg(dstReg, srcReg, 0x8100);
}

bool codegenEmitAdd(uint32_t dstReg, uint32_t srcReg)
{
	return codegenEmitDpTwoReg(dstReg, srcReg, 0x8200);
}

bool codegenEmitSub(uint32_t dstReg, uint32_t srcReg)
{
	return codegenEmitDpTwoReg(dstReg, srcReg, 0x8300);
}

bool codegenEmitAnd(uint32_t dstReg, uint32_t srcReg)
{
	return codegenEmitDpTwoReg(dstReg, srcReg, 0x8400);
}

bool codegenEmitOrr(uint32_t dstReg, uint32_t srcReg)
{
	return codegenEmitDpTwoReg(dstReg, srcReg, 0x8500);
}

bool codegenEmitXor(uint32_t dstReg, uint32_t srcReg)
{
	return codegenEmitDpTwoReg(dstReg, srcReg, 0x8600);
}

bool codegenEmitLslReg(uint32_t dstReg, uint32_t regBy)
{
	return codegenEmitDpTwoReg(dstReg, regBy, 0x8700);
}

bool codegenEmitLsrReg(uint32_t dstReg, uint32_t regBy)
{
	return codegenEmitDpTwoReg(dstReg, regBy, 0x8800);
}

static bool codegenEmitDpRegWithImm5(uint32_t reg, uint32_t imm5, uint32_t opcode)
{
	return codegenEmit(opcode | (reg << 5) | imm5);
}

static bool codegenEmitDpRegWithImm8(uint32_t reg, uint32_t imm8, uint32_t opcode)
{
	return codegenEmit(opcode | (reg << 8) | imm8);
}

bool codegenEmitLslImm(uint32_t dstReg, uint32_t imm)
{
	return codegenEmitDpRegWithImm5(dstReg, imm, 0x8a00);
}

bool codegenEmitLsrImm(uint32_t dstReg, uint32_t imm)
{
	return codegenEmitDpRegWithImm5(dstReg, imm, 0x8b00);
}

bool codegenEmitAddImm8(uint32_t dstReg, uint32_t imm8)
{
	return codegenEmitDpRegWithImm8(dstReg, imm8, 0x6000);
}

bool codegenEmitSubImm8(uint32_t dstReg, uint32_t imm8)
{
	return codegenEmitDpRegWithImm8(dstReg, imm8, 0x6800);
}

static bool codegenEmitLoadImm8(uint32_t dstReg, uint32_t imm8)
{
	return codegenEmit(0x7000 | (dstReg << 8) | imm8);
}

bool codegenEmitLoadImm(uint32_t dstReg, uint32_t val)
{
	uint32_t i;
	
	//if it fits into 8 bits, emit that
	if (!(val >> 8))
		return codegenEmitLoadImm8(dstReg, val);
	
	//if it fits into 24 bits, emit that
	if (!(val >> 24))
		return codegenEmit(0x7800 | (dstReg << 8) | (val & 0xFF)) && codegenEmit(val >> 8);
	
	//maybe it is a value that is an inverse of an 8-bit value?
	if (!((~val) >> 8))
		return codegenEmitLoadImm8(dstReg, ~val) && codegenEmitNot(dstReg, dstReg);

	//maybe it can be an 8-bit value shifted left into place?
	for (i = 0; i < 32; i++) {
		
		//verify shift does not destroy value (if it does, further shifts also will, so break out
		if (((val >> i) << i) != val)
			break;
		
		//see if shift produced a valid 8-bit immediate, if so emit a mov and an lsl
		if (!((val >> i) >> 8))
			return codegenEmitLoadImm8(dstReg, val >> i) && codegenEmitLslImm(dstReg, i);
	}
	
	//emit a full 32-bit load
	return codegenEmit(0x8F00 | dstReg) && codegenEmit(val & 0xFFFF) && codegenEmit(val >> 16);
}

bool codegenEmitStackPush(uint32_t reg)
{
	return codegenEmit(0x8C00 | reg);
}

bool codegenEmitStackPop(uint32_t reg)
{
	return codegenEmit(0x8D00 | reg);
}

bool codegenEmitReturn(void)
{
	return codegenEmit(0x8E00);
}

bool codegenEmitExit(uint8_t retcode)
{
	return codegenEmit(0xF800 | retcode);
}

bool codegenEmitCallToGenerated(struct CodegenLabel* dst)
{
	return codegenEmitBranchPossiblyWithLink(dst, true);
}

struct CodegenLabel* codegenLabelGetCur(void)
{
	return (struct CodegenLabel*)(((uintptr_t)mOutputPosHead) | LABEL_MASK_NORMAL);
}

bool codegenLabelFree(struct CodegenLabel* lbl)
{
	//nothing to do, but verify the label
	return codegenVerifyLabel(lbl) >= 0;
}

struct CodegenPredeclaredLabel* codegenAllocPredeclaredLabel(void)
{
	int32_t pos = codegenAllocRear(1);
	
	if (pos < 0)
		return false;
	
	codegenEmitAt(pos, 0);
	return (struct CodegenPredeclaredLabel*)(((uintptr_t)pos) | LABEL_MASK_PREDECLARED);
}

struct CodegenLabel* codegenGetPredeclaredLabelBranchTarget(struct CodegenPredeclaredLabel* predeclaredLbl)
{
	int32_t pos = codegenVerifyPredeclaredLabel(predeclaredLbl);
	
	return (pos >= 0) ? (struct CodegenLabel*)(LABEL_MASK_NORMAL | pos) : NULL;
}

bool codegenGetPredeclaredLabelFillTarget(struct CodegenPredeclaredLabel* predeclaredLbl, struct CodegenLabel* target)
{
	int32_t lbl = codegenVerifyPredeclaredLabel(predeclaredLbl);
	int32_t jumpToDstInstr = codegenInstrForJump(target, false);
	
	if ((lbl < 0) || (jumpToDstInstr < 0) || codegenReadEmittedAt(lbl))
		return false;
		
	codegenEmitAt(lbl, jumpToDstInstr);
	return true;
}

bool codegenGetPredeclaredLabelFree(struct CodegenPredeclaredLabel* predeclaredLbl)
{
	//nothing to do, but verify the label
	return codegenVerifyPredeclaredLabel(predeclaredLbl) >= 0;
}

bool codegenEmitCallToNative(uint8_t funcNo)
{
	return codegenEmit(0xf000 | funcNo);
}
