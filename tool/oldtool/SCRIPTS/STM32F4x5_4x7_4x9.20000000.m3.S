.syntax unified
.thumb
.section .text


//we have 128K of RAM at 0x20000000 or more

//sp starts at 0x20000400
//we stage data at 0x20000400 too


//entry points (also we breakpoint this place and use it as debugger call)
.globl entrypts
entrypts:

	//flash size (will be replaced by init code)
	flashsz:
	.word 65536	//we actually have a lot more but block sizes vary and i have no time for that shit
	
	//flash block size for erase/write
	.word 16384

	//flash base
	.word 0

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
	
	//custom command (...)
	.word 0


.globl init_stage_1
init_stage_1:

	ldr sp, =0x20000400	// a good safe value
	ldr r7, =0x40023C00	// flash unit base

	//unlock flash regs
	ldr r0, =0x45670123
	ldr r1, =0xCDEF89AB
	str r0, [r7, #0x04]
	str r1, [r7, #0x04]
	mov r0, #15
	str r0,[r7, #0]		//lots of wait states
	bkpt


wait:
	ldr  r12, [r7, #0x0c]
	lsrs r12, #17
	bcs  wait
	bx   lr


.globl all_erase
all_erase:

	ldr  r0, =0x10004
	str  r0, [r7, #0x10]
	bl   wait
	bkpt
	
	
.globl block_erase	//assumes valid address passed
block_erase:

	ldr  r1, =0x10002
	lsrs r0, #11 //move address to "SNB" bit range
	orrs r1, r0
	str  r1, [r7, #0x10]
	bl   wait
	bkpt


.globl block_write
block_write:
	movs r5, #0
	ldr  r4, =0x20000400

1:
	movs r3, #1
	str  r3, [r7, #0x10]
	
	ldrb r3, [r4, r5]
	strb r3, [r0, r5]
	adds r5, #1
	bl   wait
	cmp  r5, #16384
	bne  1b
	

	movs r0, #0		//since write is so easy to do on accident, do not leave flash in that mode
	str  r0, [r7, #0x10]
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
	.word 0x00000011
	.word 0x00000004
	.word 0x0000000a
	.word 0x00000000
	
	//this word must be zero
	.word 0
	
	//human-friendly name for this cpu/script
	.ascii "STM32F4xx"

