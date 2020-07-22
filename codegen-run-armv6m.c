#include <string.h>
#include "codegen.h"
#include "plat.h"



static bool __attribute__((used)) codegenRunPush(uint32_t val, uint32_t *stackStore, uint32_t *stackPosP, uint32_t stackSz)
{
	if (*stackPosP >= stackSz)
		return false;
	
	stackStore[(*stackPosP)++] = val;
	return true;
}

static bool __attribute__((used)) codegenRunPop(uint32_t *valP, uint32_t *stackStore, uint32_t *stackPosP)
{
	if (!*stackPosP)
		return false;
	
	*valP = stackStore[--(*stackPosP)];
	return true;
}

uint8_t __attribute__((naked)) codegenRunInternal(uint32_t *regsInOut, uint16_t *opcodes)
{
	//stack laoyt:
	//	regs[8]
	//	uint32_t recurStackOfst
	//	uint32_t dataStackOfst
	//	recurStack[CODEGEN_MAX_RECUR_DEPTH];
	//	dataStack[CODEGEN_MAX_STACK_DEPTH]
	//
	//regs:
	//	r5 = opcodes
	//	r4 = pc
	//	r6 = instr
	//	r7 = same as arm's SP (faster to have it there)
	
	asm volatile(
	
		"	push	{r0, r4-r7, lr}			\n\t"
		"	sub		sp, %0					\n\t"	//make space on stack for all except lower 4 regs
		"	ldmia	r0!, {r2, r3, r5, r6}	\n\t"	//get input regs
		"	push	{r2, r3, r5, r6}		\n\t"	//stash them in place for r0..r3
		"	mov		r4, #0					\n\t"
		"	mov		r5, r1					\n\t"
		"	str		r4, [sp, %2]			\n\t"	//recurStackOfst = 0
		"	str		r4, [sp, %3]			\n\t"	//dataStackOfst = 0
		"	mov		r7, sp					\n\t"	//set r7 to point to sp
		"instr:								\n\t"
		"	lsr		r0, r4, #8				\n\t"	//verify PC is safe
		"	bne		exit_fail				\n\t"
		"	lsl		r0, r4, #1				\n\t"	//get opcode
		"	add		r4, #1					\n\t"	//increment pc
		"	ldrh	r6, [r5, r0]			\n\t"
		"	lsr		r0, r6, #10				\n\t"
		"	add		pc, r0					\n\t"	//conveniently, lower bit is ignored so this works well
		"	nop								\n\t"	//unused
		"	b		instr_bl				\n\t"	//0b00000
		"	b		instr_bl				\n\t"	//0b00001
		"	b		instr_bl				\n\t"	//0b00010
		"	b		instr_bl				\n\t"	//0b00011
		"	b		instr_b					\n\t"	//0b00100
		"	b		instr_b					\n\t"	//0b00101
		"	b		instr_b					\n\t"	//0b00110
		"	b		instr_b					\n\t"	//0b00111
		"	b		instr_SKcc_imm			\n\t"	//0b01000
		"	b		instr_SKcc_imm			\n\t"	//0b01001
		"	b		instr_SKcc_imm			\n\t"	//0b01010
		"	b		instr_SKcc_imm			\n\t"	//0b01011
		"	b		instr_add_imm			\n\t"	//0b01100
		"	b		instr_sub_imm			\n\t"	//0b01101
		"	b		instr_mov_imm8			\n\t"	//0b01110
		"	b		instr_mov_imm24			\n\t"	//0b01111
		"	b		instr_misc_reg_reg		\n\t"	//0b10000
		"	b		instr_misc_misc			\n\t"	//0b10001
		"	b		exit_fail				\n\t"	//0b10010
		"	b		exit_fail				\n\t"	//0b10011
		"	b		exit_fail				\n\t"	//0b10100
		"	b		exit_fail				\n\t"	//0b10101
		"	b		exit_fail				\n\t"	//0b10110
		"	b		exit_fail				\n\t"	//0b10111
		"	b		exit_fail				\n\t"	//0b11000
		"	b		exit_fail				\n\t"	//0b11001
		"	b		exit_fail				\n\t"	//0b11010
		"	b		exit_fail				\n\t"	//0b11011
		"	b		exit_fail				\n\t"	//0b11100
		"	b		exit_fail				\n\t"	//0b11101
		"	b		instr_nativecall		\n\t"	//0b11110
		"	b		instr_exit				\n\t"	//0b11111
		"									\t\t"
		"instr_bl:							\n\t"
		"	mov		r0, r4					\n\t"
		"	add		r1, sp, %1				\n\t"
		"	add		r2, sp, %2				\n\t"
		"	mov		r3, %4					\n\t"
		"	bl		codegenRunPush			\n\t"
		"	cmp		r0, #0					\n\t"
		"	beq		exit_fail				\n\t"
		"	/*fallthrough*/					\n\t"
		"									\n\t"
		"instr_b:							\n\t"
		"	lsl		r4, r6, #19				\n\t"
		"	lsr		r4, r4, #19				\n\t"
		"	b		instr					\n\t"
		"									\n\t"
		"instr_SKcc_imm:					\n\t"
		"	lsl		r0, r6, #21				\n\t"	//get reg idx for LHS into r0
		"	lsr		r0, r0, #29				\n\t"
		"	lsl		r0, #2					\n\t"	//get LHS reg ofst form stcak
		"	ldr		r0, [r7, r0]			\n\t"	//get the LHS reg
		"	uxtb	r1, r6					\n\t"	//get imm8 for RHS
		"	lsl		r2, r6, #19				\n\t"	//get (cc << 2)
		"	lsr		r2, r2, #30				\n\t"
		"instr_SKcc_common:					\n\t"	//expects r0 = LHS, r1 = RHS, r2 = cc
		"	lsl		r2, r2, #2				\n\t"
		"	cmp		r0, r1					\n\t"	//do the comparison
		"	add		pc, r2					\n\t"	//jump
		"	nop								\n\t"	//unused
		"/*EQ*/								\n\t"
		"	bne		instr_SKcc_imm_no		\n\t"
		"	b		instr					\n\t"
		"/*NE*/								\n\t"
		"	beq		instr_SKcc_imm_no		\n\t"
		"	b		instr					\n\t"
		"/*LT*/								\n\t"
		"	bcs		instr_SKcc_imm_no		\n\t"
		"	b		instr					\n\t"
		"/*GE*/								\n\t"
		"	bcc		instr_SKcc_imm_no		\n\t"
		"	b		instr					\n\t"
		"instr_SKcc_imm_no	:				\n\t"
		"	add		r4, #1					\n\t"
		"	b		instr					\n\t"
		"									\n\t"
		"instr_add_imm:						\n\t"
		"	uxtb	r1, r6					\n\t"	//get imm
		"	lsl		r0, r6, #21				\n\t"	//get reg num
		"	lsr		r0, r0, #29				\n\t"
		"	lsl		r0, r0, #2				\n\t"	//get reg ofst
		"	ldr		r2, [r7, r0]			\n\t"	//get reg
		"	add		r2, r1					\n\t"	//add
		"	str		r2, [r7, r0]			\n\t"	//save reg
		"	b		instr					\n\t"
		"									\n\t"
		"instr_sub_imm:						\n\t"
		"	uxtb	r1, r6					\n\t"	//get imm
		"	lsl		r0, r6, #21				\n\t"	//get reg num
		"	lsr		r0, r0, #29				\n\t"
		"	lsl		r0, r0, #2				\n\t"	//get reg ofst
		"	ldr		r2, [r7, r0]			\n\t"	//get reg
		"	sub		r2, r1					\n\t"	//sub
		"	str		r2, [r7, r0]			\n\t"	//save reg
		"	b		instr					\n\t"
		"									\n\t"
		"instr_mov_imm8:					\n\t"
		"	uxtb	r1, r6					\n\t"	//get imm
		"	lsl		r0, r6, #21				\n\t"	//get reg num
		"	lsr		r0, r0, #29				\n\t"
		"	lsl		r0, r0, #2				\n\t"	//get reg ofst
		"	str		r1, [r7, r0]			\n\t"	//save reg
		"	b		instr					\n\t"
		"									\n\t"
		"instr_mov_imm24:					\n\t"
		"	lsr		r0, r4, #8				\n\t"	//verify PC is safe
		"	bne		exit_fail				\n\t"
		"	lsl		r0, r4, #1				\n\t"	//get opcode
		"	add		r4, #1					\n\t"	//increment pc
		"	ldrh	r1, [r5, r0]			\n\t"	//imm_higher_16 
		"	lsl		r1, r1, #8				\n\t"	//imm_higher_16 << 8
		"	uxtb	r2, r6					\n\t"	//get imm_lower_8
		"	orr		r1, r2					\n\t"	//imm24
		"	lsl		r0, r6, #21				\n\t"	//get reg num
		"	lsr		r0, r0, #29				\n\t"
		"	lsl		r0, r0, #2				\n\t"	//get reg ofst
		"	str		r1, [r7, r0]			\n\t"	//save reg
		"	b		instr					\n\t"
		"									\n\t"
		"instr_nativecall:					\n\t"
		"	uxtb	r0, r6					\n\t"	//get instr idx
		"	mov		r1, sp					\n\t"	//regs
		"	bl		codegenExtNativeFunc	\n\t"
		"	cmp		r0, #0					\n\t"
		"	beq		exit_fail				\n\t"
		"	b		instr					\n\t"
		"									\n\t"
		"instr_exit:						\n\t"
		"	uxtb	r0, r6					\n\t"	//get ret_code
		"	b		exit_code_set			\n\t"
		"									\n\t"
		"exit_fail:							\n\t"
		"	mov 	r0, #0xff				\n\t"
		"									\n\t"
		"exit_code_set:						\n\t"
		"	pop		{r2, r3, r5, r6}		\n\t"	//get low regs
		"	add		sp, %0					\n\t"	//undo stack allow we had made
		"	pop		{r1}					\n\t"	//get previously stashed r0 -> "regs"
		"	stmia	r1!, {r2, r3, r5, r6}	\n\t"	//save regs to caller's array
		"	pop		{r4-r7, pc}				\n\t"	//and ... we're done
		"									\n\t"
		"instr_misc_reg_reg:				\n\t"	//all of these are reg-reg so we factor out the reg code
		"	mov		r3, #0x1C				\n\t"	//reg ofst mask (faster this way as we need it twice)
		"	lsr		r1, r6, #1				\n\t"	//reg RHS offset
		"	and		r1, r3					\n\t"
		"	ldr		r1, [r7, r1]			\n\t"	//get RHS val
		"	lsl		r0, r6, #2				\n\t"	//reg LHS offset
		"	and		r0, r3					\n\t"
		"	lsl		r3, r6, #21				\n\t"	//get ((bits 9..11) << 3) for dispatch
		"	lsr		r3, r3, #29				\n\t"
		"	lsl		r3, r3, #3				\n\t"
		"	add		pc, r3					\n\t"	//jump
		"	nop								\n\t"	//unused
		"/*MOV*/							\n\t"
		"	str		r1, [r7, r0]			\n\t"
		"	b		instr					\n\t"
		"	nop								\n\t"
		"	nop								\n\t"
		"/*NOT*/							\n\t"
		"	mvn		r1, r1					\n\t"
		"	str		r1, [r7, r0]			\n\t"
		"	b		instr					\n\t"
		"	nop								\n\t"
		"/*ADD*/							\n\t"
		"	ldr		r2, [r7, r0]			\n\t"
		"	add		r2, r1					\n\t"
		"	str		r2, [r7, r0]			\n\t"
		"	b		instr					\n\t"
		"/*SUB*/							\n\t"
		"	ldr		r2, [r7, r0]			\n\t"
		"	sub		r2, r1					\n\t"
		"	str		r2, [r7, r0]			\n\t"
		"	b		instr					\n\t"
		"/*AND*/							\n\t"
		"	ldr		r2, [r7, r0]			\n\t"
		"	and		r2, r1					\n\t"
		"	str		r2, [r7, r0]			\n\t"
		"	b		instr					\n\t"
		"/*ORR*/							\n\t"
		"	ldr		r2, [r7, r0]			\n\t"
		"	orr		r2, r1					\n\t"
		"	str		r2, [r7, r0]			\n\t"
		"	b		instr					\n\t"
		"/*XOR*/							\n\t"
		"	ldr		r2, [r7, r0]			\n\t"
		"	eor		r2, r1					\n\t"
		"	str		r2, [r7, r0]			\n\t"
		"	b		instr					\n\t"
		"/*LSL*/							\n\t"
		"	ldr		r2, [r7, r0]			\n\t"
		"	lsl		r2, r1					\n\t"
		"	str		r2, [r7, r0]			\n\t"
		"	b		instr					\n\t"
		"									\n\t"
		"instr_misc_misc:					\n\t"
		"	mov		r3, #0x1C				\n\t"	//reg ofst mask (faster this way as we need it often)
		"	lsl		r2, r6, #21				\n\t"	//get ((bits 9..11) << 1) for dispatch. low bit is ignored so we leave it be
		"	lsr		r2, r2, #28				\n\t"
		"	add		pc, r2					\n\t"	//jump
		"	nop								\n\t"	//unused
		"	b		instr_lsr_reg			\n\t"
		"	b		instr_SKcc_reg			\n\t"
		"	b		instr_lsl_imm			\n\t"
		"	b		instr_lsr_imm			\n\t"
		"	b		instr_instr_push		\n\t"
		"	b		instr_instr_pop			\n\t"
		"	b		instr_instr_ret			\n\t"
		"	b		instr_instr_mov_imm32	\n\t"
		"									\n\t"
		"instr_lsr_reg:						\n\t"
		"	lsr		r1, r6, #1				\n\t"	//reg RHS offset
		"	and		r1, r3					\n\t"
		"	ldr		r1, [r7, r1]			\n\t"	//get RHS val
		"	lsl		r0, r6, #2				\n\t"	//reg LHS offset
		"	and		r0, r3					\n\t"
		"	ldr		r2, [r7, r0]			\n\t"	//get LHS val
		"	lsl		r2, r1					\n\t"
		"	str		r2, [r7, r0]			\n\t"
		"	b		instr					\n\t"
		"									\n\t"
		"instr_SKcc_reg:					\n\t"
		"	lsr		r1, r6, #1				\n\t"	//reg RHS offset
		"	and		r1, r3					\n\t"
		"	ldr		r1, [r7, r1]			\n\t"	//get RHS val
		"	lsl		r0, r6, #2				\n\t"	//reg LHS offset
		"	and		r0, r3					\n\t"
		"	ldr		r0, [r7, r0]			\n\t"	//get LHS val
		"	lsl		r2, r6, #24				\n\t"	//get cc
		"	lsr		r2, r2, #30				\n\t"
		"	b		instr_SKcc_common		\n\t"	//expects r0 = LHS, r1 = RHS, r2 = cc
		"									\n\t"
		"instr_lsl_imm:						\n\t"
		"	lsr		r0, r6, #3				\n\t"	//get ofst for reg
		"	and		r0, r3					\n\t"
		"	mov		r3, #0x1f				\n\t"	//get imm
		"	and		r3, r6					\n\t"
		"	ldr		r2, [r7, r0]			\n\t"	//get val
		"	lsl		r2, r3					\n\t"	//lsl
		"	str		r2, [r7, r0]			\n\t"	//save val
		"	b		instr					\n\t"
		"									\n\t"
		"instr_lsr_imm:						\n\t"
		"	lsr		r0, r6, #3				\n\t"	//get ofst for reg
		"	and		r0, r3					\n\t"
		"	mov		r3, #0x1f				\n\t"	//get imm
		"	and		r3, r6					\n\t"
		"	ldr		r2, [r7, r0]			\n\t"	//get val
		"	lsr		r2, r3					\n\t"	//lsr
		"	str		r2, [r7, r0]			\n\t"	//save val
		"	b		instr					\n\t"
		"									\n\t"
		"instr_instr_mov_imm32:				\n\t"
		"	cmp		r4, #255				\n\t"
		"	bcs		exit_fail				\n\t"	//is there space?
		"	lsl		r0, r4, #1				\n\t"	//r0 = &opcodes[pc]
		"	add		r0, r5					\n\t"
		"	add		r4, #2					\n\t"	//pc += 2
		"	ldrh	r1, [r0, #0]			\n\t"	//get low
		"	ldrh	r0, [r0, #2]			\n\t"	//get high
		"	lsl		r0, r0, #16				\n\t"	//cac imm32
		"	add		r0, r1					\n\t"
		"	lsl		r1, r6, #2				\n\t"	//get reg pointer
		"	and		r1, r3					\n\t"
		"	str		r0, [r7, r1]			\n\t"	//store it
		"	b		instr					\n\t"
		"									\n\t"
		"instr_instr_ret:					\n\t"
		"	push	{r4}					\n\t"	//easiest way to reliably get a pointer to "pc"
		"	mov		r0, sp					\n\t"
		"	add		r1, sp, %1				\n\t"
		"	add		r2, sp, %2				\n\t"
		"	bl		codegenRunPop			\n\t"
		"	pop		{r4}					\n\t"
		"exit_on_false_common:				\n\t"	//if r0 == 0, goto fail, else go to next instr
		"	cmp		r0, #0					\n\t"
		"	beq		exit_fail				\n\t"
		"	b		instr					\n\t"
		"									\n\t"
		"instr_instr_push:					\n\t"
		"	lsl		r1, r6, #2				\n\t"	//get reg pointer
		"	and		r1, r3					\n\t"
		"	ldr		r0, [r7, r1]			\n\t"	//grab it
		"	add		r1, sp, %5				\n\t"
		"	add		r2, sp, %3				\n\t"
		"	mov		r3, %6					\n\t"
		"	bl		codegenRunPush			\n\t"
		"	b		exit_on_false_common	\n\t"
		"									\n\t"
		"instr_instr_pop:					\n\t"
		"	lsl		r0, r6, #2				\n\t"	//get reg pointer
		"	and		r0, r3					\n\t"
		"	add		r1, sp, %5				\n\t"
		"	add		r2, sp, %3				\n\t"
		"	bl		codegenRunPop			\n\t"
		"	b		exit_on_false_common	\n\t"

		:
		:
			/* %0 -> total stack space to allocate minus the 4 regs we push manually	*/ "O"((CODEGEN_MAX_RECUR_DEPTH + CODEGEN_MAX_STACK_DEPTH + 2 + 4) * 4),
			/* %1 -> stack offset to recur stack										*/ "O"((2 + 8) * 4),
			/* %2 -> stack offset of recurStackOfst										*/ "O"((0 + 8) * 4),
			/* %3 -> stack offset of dataStackOfst										*/ "O"((1 + 8) * 4),
			/* %4 -> num words in recur stack											*/ "I"(CODEGEN_MAX_RECUR_DEPTH),
			/* %5 -> stack offset to data stack											*/ "O"((CODEGEN_MAX_RECUR_DEPTH + 2 + 8) * 4),
			/* %6 -> num words in data stack											*/ "I"(CODEGEN_MAX_STACK_DEPTH)

	//stack laoyt:
	//	regs[8]
	//	uint32_t recurStackOfst
	//	uint32_t dataStackOfst
	//	recurStack[CODEGEN_MAX_RECUR_DEPTH];
	//	dataStack[CODEGEN_MAX_STACK_DEPTH]	
			
		:"memory"
	);
	
	//unused
	(void)regsInOut;
	(void)opcodes;
	return 0;
}


#if MAX_OPCODES != 256
	#error "codegen-arm-v6m assumes MAX_OPCODES is 256!"
#endif