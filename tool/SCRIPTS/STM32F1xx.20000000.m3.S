.syntax unified
.thumb
.section .text

//C-m3
//we have 20K of RAM at 0x20000000 or more
//we have 128K of flash at 0x08000000
//FMC is at 0x40022000
//flash size in kbytes is at 0x1FFFF7E0

//sp starts at 0x20000400
//we stage data at 0x20000400 too


//entry points (also we breakpoint this place and use it as debugger call)
.globl entrypts
entrypts:

	//syscall area
syscall:
1:
	nop
	b 1b

	//init stage 1
	bl init_stage_1
	
	//init stage 2
	.word 0
	
	//init stage 3
	.word 0
	
	//mass erase
	bl all_erase
	
	//erase at address (uint32_t addr)
	bl block_erase
	
	//write block  (uint32_t addr)
	bl block_write
	
	//cpuid func (none needed)
	.word 0


.globl init_stage_1
init_stage_1:

	ldr  sp, =0x20000400	// a good safe value
	ldr  r7, =0x40022000	// flash unit base

	//unlock flash regs and opt programming
	ldr  r0, =0x45670123
	ldr  r1, =0xCDEF89AB
	str  r0, [r7, #0x04]
	str  r1, [r7, #0x04]
	movs r0, #2
	str  r0, [r7, #0]		//lots of wait states

	//load flash size in kbytes
	ldr  r2, =0x1FFFF7E0
	ldrh r2, [r2]
	
	//create flash mem info
	mov  r6, sp				// dst for meminfo
	mov  r0, #1				// 1 area
	mov  r1, #0x08
	lsl  r1, #24			// base addr 0x08000000
	lsl  r2, #8
	orr  r2, #10			//write sz log is 10 (1K writes)
	lsl  r2, #8
	orr  r2, #10			//erase sz log is 10 (1K writes)
	stmia r6!, {r0-r2}		//save it
	
	//return meminfo ptr
	mov  r0, sp
	
	//names (none)
	movs  r1, #0
	
	//flags (none)
	movs  r2, #0
	bkpt


wait:
	ldr  r12, [r7, #0x0c]
	lsrs r12, #1
	bcs  wait
	bx   lr


.globl all_erase
all_erase:

	movs r0, 0x04
	str  r0, [r7, #0x10]
	movs r0, 0x44
	str  r0, [r7, #0x10]
	bl   wait
	mov  r0, #1
	bkpt
	
	
.globl block_erase	//assumes valid address passed
block_erase:

	movs r1, 0x02
	str  r1, [r7, #0x10]
	str  r0, [r7, #0x14]
	movs r1, 0x42
	str  r1, [r7, #0x10]
	bl   wait
	mov  r0, #1
	bkpt


.globl block_write
block_write:
	movs r5, #0
	ldr  r4, =0x20000400

1:
	movs r3, #1
	str  r3, [r7, #0x10]
	
	ldrh r3, [r4, r5]
	strh r3, [r0, r5]
	adds r5, #2
	bl   wait
	cmp  r5, #1024
	bne  1b
	

	movs r0, #0		//since write is so easy to do on accident, do not leave flash in that mode
	str  r0, [r7, #0x10]
	
	mov  r0, #1
	bkpt



.align 4
.section .text.2
.globl info
info:

//checkvals (none)

//now the fixed-length footer:
	//load address of this code
	.word 0x20000000

	//flash staging area in ram
	.word 0x20000400

	//number of checkvals above
	.word 0
	
	//tagetid on swd bus (most likely zero)
	.word 0
	
	//cpuid value masks from cpuid (8 of them)
	.word 0xffffffff
	.word 0xffffffff
	.word 0xffffffff
	.word 0xffffffff
	.word 0xffffffff
	.word 0xffffffff
	.word 0xffffffff
	.word 0xffffffff
	
	//cpuid match values from cpuid (8 of them)
	.word 0x00000000
	.word 0x00000000
	.word 0x00000000
	.word 0x00000000
	.word 0x00000010
	.word 0x00000004
	.word 0x0000000a
	.word 0x00000000
	
	//this word must be zero
	.word 0
	
	//human-friendly name for this cpu/script
	.ascii "STM32F1xx"
