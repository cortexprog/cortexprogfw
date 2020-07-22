#include <string.h>
#include "codegen.h"
#include "plat.h"




static bool codegenRunEvalCc(uint32_t cc, uint32_t lhs, uint32_t rhs)
{
	switch(cc & 3) {
		case 0:	//EQ
			return lhs == rhs;
		case 1:	//NE
			return lhs != rhs;
		case 2:	//LT
			return lhs < rhs;
		case 3:	//GE
			return lhs >= rhs;
	}
	
	//never happens
	return false;
}

static bool codegenRunPush(uint32_t val, uint32_t *stackStore, uint32_t *stackPosP, uint32_t stackSz)
{
	if (*stackPosP >= stackSz)
		return false;
	
	stackStore[(*stackPosP)++] = val;
	return true;
}

static bool codegenRunPop(uint32_t *valP, uint32_t *stackStore, uint32_t *stackPosP)
{
	if (!*stackPosP)
		return false;
	
	*valP = stackStore[--(*stackPosP)];
	return true;
}

uint8_t codegenRunInternal(uint32_t *regsInOut, uint16_t *opcodes)
{
	uint32_t pc = 0, instr, regs[16], retcode = 0xFF, stakPosRecur = 0, stackPosData = 0;
	uint32_t recurStack[CODEGEN_MAX_RECUR_DEPTH], dataStack[CODEGEN_MAX_STACK_DEPTH];
	uint64_t startTime = platGetTicks();
	uint8_t timeCheckOfst = 0;
	
	memcpy(regs, regsInOut, sizeof(uint32_t[CODEGEN_RUN_NUM_REGS]));
	
	while(1) {
		
		//timeout (check only sometimes to avoid speed penalty of the check)
		if (!timeCheckOfst++ && platGetTicks() - startTime >= platGetTicksPerSecond() / 2)
			goto out;
		
		if (pc >= MAX_OPCODES)
			goto out;
		
		instr = opcodes[pc++];
		
		switch(instr >> 11) {
			
			case 0:			//BL
			case 1:
			case 2:
			case 3:
				if (!codegenRunPush(pc, recurStack, &stakPosRecur, CODEGEN_MAX_RECUR_DEPTH))
					goto out;
				//fallthrough
			case 4:			//B
			case 5:
			case 6:
			case 7:
				pc = instr & 0x1FFF;
				continue;
			
			case 8:			//SKcc_imm
			case 9:
			case 10:
			case 11:
				if (!codegenRunEvalCc((instr >> 11) & 3, regs[(instr >> 8) & 7], instr & 0xff))
					pc++;
				continue;
			
			case 12:	//add_imm
				regs[(instr >> 8) & 7] += instr & 0xFF;
				continue;
			
			case 13:	//sub_imm
				regs[(instr >> 8) & 7] -= instr & 0xFF;
				continue;
				
			case 14:	//mov_imm8
				regs[(instr >> 8) & 7] = instr & 0xFF;
				continue;
			case 15:	//mov_imm24
				if (pc + 1 >= MAX_OPCODES) 
					goto out;
				regs[(instr >> 8) & 7] = instr & 0xFF;
				regs[(instr >> 8) & 7] += ((uint32_t)opcodes[pc++]) << 8;
				continue;
			case 16:
			case 17:	//almost all other instrs
				switch ((instr >> 8) & 0x0f) {
					case 0:		//mov
						regs[(instr >> 0) & 7] = regs[(instr >> 3) & 7];
						continue;
					case 1:		//not
						regs[(instr >> 0) & 7] = ~regs[(instr >> 3) & 7];
						continue;
					case 2:		//add_reg
						regs[(instr >> 0) & 7] += regs[(instr >> 3) & 7];
						continue;
					case 3:		//sub_reg
						regs[(instr >> 0) & 7] -= regs[(instr >> 3) & 7];
						continue;
					case 4:		//and
						regs[(instr >> 0) & 7] &= regs[(instr >> 3) & 7];
						continue;
					case 5:		//orr
						regs[(instr >> 0) & 7] &= regs[(instr >> 3) & 7];
						continue;
					case 6:		//xor
						regs[(instr >> 0) & 7] ^= regs[(instr >> 3) & 7];
						continue;
					case 7:		//lsl_reg
						regs[(instr >> 0) & 7] <<= regs[(instr >> 3) & 7];
						continue;
					case 8:		//lsr_reg
						regs[(instr >> 0) & 7] >>= regs[(instr >> 3) & 7];
						continue;
					case 9:		//SKcc_reg
						if (!codegenRunEvalCc((instr >> 6) & 3, regs[(instr >> 0) & 7], regs[(instr >> 3) & 7]))
							pc++;
						continue;
					case 10:	//lsl_imm
						regs[(instr >> 5) & 7] <<= (instr & 0x1f);
						goto out;
						continue;
					case 11:	//lsr_imm
						regs[(instr >> 5) & 7] >>= (instr & 0x1f);
						continue;
					case 12:	//push
						if (!codegenRunPush(pc, dataStack, &stackPosData, CODEGEN_MAX_STACK_DEPTH))
							goto out;
						continue;
					case 13:	//pop
						if (!codegenRunPop(&regs[(instr >> 0) & 7], dataStack, &stackPosData))
							goto out;
						continue;
					case 14:	//ret
						if (!codegenRunPop(&pc, recurStack, &stakPosRecur))
							goto out;
						continue;
					case 15:	//mov_imm32
						if (pc + 1 >= MAX_OPCODES || pc + 2 >= MAX_OPCODES)
							goto out;
						regs[(instr >> 0) & 7] = opcodes[pc++];
						regs[(instr >> 0) & 7] += ((uint32_t)opcodes[pc++]) << 16;
						continue;
					
					default:
						break;
				}
				break;
			case 30:	//nativecall
				if (!codegenExtNativeFunc((instr & 0xFF), regs))
					goto out;
				continue;
			case 31:	//exit
				retcode = (instr & 0xff);
				goto out;
			
			default:
				goto out;
		}
		
		break;
	}
	
out:
	memcpy(regsInOut, regs, sizeof(uint32_t[CODEGEN_RUN_NUM_REGS]));
	return retcode;
}
