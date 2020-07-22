#ifndef _CODEGEN_H_
#define _CODEGEN_H_

#include <stdbool.h>
#include <stdint.h>

#define CODEGEN_MAX_RECUR_DEPTH		8	//in calls (must be under 256)
#define CODEGEN_MAX_STACK_DEPTH		16	//in words (must be under 256)

#define CODEGEN_RUN_NUM_REGS		4

//generates code for current machine incl calls to native funcs
//this machine we are emitting to should have 7 registers (not incl pc)
//calls must work such that regs 0..3 are params and return is in reg 0
//calls to native funcs may corrupt regs 0..3
//max call depth allowed is CODEGEN_MAX_RECUR_DEPTH, max stack usage is
//CODEGEN_MAX_STACK_DEPTH words
//any return of "FALSE" may currupt state irreverisbly and must be
//followed by a call to codegenInit()
//
//natve funcs are dispatched via codegenExtNativeFunc()

struct CodegenLabel;
struct CodegenPredeclaredLabel;

//general init
bool codegenInit(void);	//removes all codegen state

//run if valid
uint8_t codegenRun(uint32_t *regsInOut/* CODEGEN_RUN_NUM_REGS regs */);	//return code returned (or oxFF in case of invalid instrs or such)

//calls and returns
bool codegenEmitCallToNative(uint8_t funcNo);				//call native func
bool codegenEmitCallToGenerated(struct CodegenLabel* dst);
bool codegenEmitReturn(void);							//only from your own funcs
bool codegenEmitExit(uint8_t retcode);					//from entire emitted script

//basic data processing
bool codegenEmitMov(uint32_t dstReg, uint32_t srcReg);
bool codegenEmitNot(uint32_t dstReg, uint32_t srcReg);
bool codegenEmitAdd(uint32_t dstReg, uint32_t srcReg);
bool codegenEmitSub(uint32_t dstReg, uint32_t srcReg);
bool codegenEmitAnd(uint32_t dstReg, uint32_t srcReg);
bool codegenEmitOrr(uint32_t dstReg, uint32_t srcReg);
bool codegenEmitXor(uint32_t dstReg, uint32_t srcReg);
bool codegenEmitLslReg(uint32_t dstReg, uint32_t regBy);
bool codegenEmitLsrReg(uint32_t dstReg, uint32_t regBy);
bool codegenEmitLslImm(uint32_t dstReg, uint32_t imm);
bool codegenEmitLsrImm(uint32_t dstReg, uint32_t imm);
bool codegenEmitLoadImm(uint32_t dstReg, uint32_t val);
bool codegenEmitAddImm8(uint32_t dstReg, uint32_t imm8);
bool codegenEmitSubImm8(uint32_t dstReg, uint32_t imm8);

//stack ops
bool codegenEmitStackPush(uint32_t reg);
bool codegenEmitStackPop(uint32_t reg);

//labels
struct CodegenLabel* codegenLabelGetCur(void);		//return abstract pointer to a struct representing current addr (for a branch)
bool codegenLabelFree(struct CodegenLabel* lbl);	//should be called after label pointer no longer needed (branches to it remain valid)

//predeclared labels are different
struct CodegenPredeclaredLabel* codegenAllocPredeclaredLabel(void);
struct CodegenLabel* codegenGetPredeclaredLabelBranchTarget(struct CodegenPredeclaredLabel* predeclaredLbl);
bool codegenGetPredeclaredLabelFillTarget(struct CodegenPredeclaredLabel* predeclaredLbl, struct CodegenLabel* target);
bool codegenGetPredeclaredLabelFree(struct CodegenPredeclaredLabel* predeclaredLbl);

//branches
bool codegenEmitBranchIfUnsignedGe(uint32_t lhsReg, uint32_t rhsReg, struct CodegenLabel* dst);
bool codegenEmitBranchIfUnsignedGt(uint32_t lhsReg, uint32_t rhsReg, struct CodegenLabel* dst);
bool codegenEmitBranchIfUnsignedLe(uint32_t lhsReg, uint32_t rhsReg, struct CodegenLabel* dst);
bool codegenEmitBranchIfUnsignedLt(uint32_t lhsReg, uint32_t rhsReg, struct CodegenLabel* dst);
bool codegenEmitBranchIfEq(uint32_t lhsReg, uint32_t rhsReg, struct CodegenLabel* dst);
bool codegenEmitBranchIfNe(uint32_t lhsReg, uint32_t rhsReg, struct CodegenLabel* dst);
bool codegenEmitBranchIfNeg(uint32_t reg, struct CodegenLabel* dst);
bool codegenEmitBranchIfNotNeg(uint32_t reg, struct CodegenLabel* dst);
bool codegenEmitBranchIfZero(uint32_t reg, struct CodegenLabel* dst);
bool codegenEmitBranchIfNonzero(uint32_t reg, struct CodegenLabel* dst);
bool codegenEmitBranchIfEqImm8(uint32_t reg, uint32_t imm8, struct CodegenLabel* dst);
bool codegenEmitBranchIfNotEqImm8(uint32_t reg, uint32_t imm8, struct CodegenLabel* dst);
bool codegenEmitBranch(struct CodegenLabel* dst);


//EXTERNALLY REQUIRED
bool codegenExtNativeFunc(uint8_t funcNo, uint32_t *regs);




//INTERNAL ONLY - do not use
#define MAX_OPCODES						256
uint8_t codegenRunInternal(uint32_t *regsInOut, uint16_t *opcodes);


#endif
