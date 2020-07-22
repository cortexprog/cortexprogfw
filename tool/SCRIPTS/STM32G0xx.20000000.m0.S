.syntax unified
.thumb
.section .text

//C-m0
//we have 8K of RAM at 0x20000000 or more
//we have 64K of flash at 0x08000000
//FLASH controller is at 0x40022000
//flash size in kbytes is at u16@0x1FFF75E0

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

	ldr  r0, =0x20000400	// a good safe value
	mov  sp, r0
	ldr  r7, =0x40022000	// flash unit base

	//unlock flash regs and opt programming
	ldr  r0, =0x45670123
	ldr  r1, =0xCDEF89AB
	str  r0, [r7, #0x08]
	str  r1, [r7, #0x08]
	movs r0, #2
	str  r0, [r7, #0]		//lots of wait states, icache off, prefetch off
	

	//load flash size in kbytes
	ldr  r2, =0x1FFF75E0
	ldrh r2, [r2]
	
	//create flash mem info
	mov  r6, sp				// dst for meminfo
	movs r0, #1				// 1 area
	movs r1, #0x08
	lsls r1, #24			// base addr 0x08000000
	lsrs r2, #1
	lsls r2, #8
	adds r2, #11			// write sz log is 11 (2K writes)
	lsls r2, #8
	adds r2, #11			// erase sz log is 11 (2K writes)
	stmia r6!, {r0-r2}		// save it
	
	//return meminfo ptr
	mov  r0, sp
	
	//names (none)
	movs  r1, #0
	
	//flags (none)
	movs  r2, #0
	bkpt


wait:
	ldr  r6, [r7, #0x0c]
	lsrs r6, #17
	bcs  wait
	bx   lr


.globl all_erase
all_erase:

	movs r0, 0x04			//MER = mass erase
	str  r0, [r7, #0x14]
	ldr  r1, =0x00010000	//start
	adds r0, r1
	str  r0, [r7, #0x14]
	bl   wait
	movs r0, #1
	bkpt
	
	
.globl block_erase	//assumes valid address passed
block_erase:

	lsrs r0, #11			//convert addr to page number
	uxtb r0, r0				//no more tahn 256 pages
	lsls r0, #3				//place into proper place
	adds r0, #2				//PER = page erase
	str  r0, [r7, #0x14]
	ldr  r1, =0x00010000	//start
	adds r0, r1
	str  r0, [r7, #0x14]
	bl   wait
	movs r0, #1
	bkpt


.globl block_write
block_write:
	movs  r5, #0xff
	adds  r5, #1		//make r5 = 0x100 (2K / 8)
	ldr   r4, =0x20000400

1:
	movs  r3, #1
	str   r3, [r7, #0x10]
	
	ldmia r4!, {r2, r3}
	stmia r0!, {r2}		//write order is required so we need to write one at a time
	stmia r0!, {r3}		//write order is required so we need to write one at a time

	bl    wait
	subs  r5, #1
	bne   1b

	movs  r0, #0		//since write is so easy to do on accident, do not leave flash in that mode
	str   r0, [r7, #0x10]
	
	movs  r0, #1
	bkpt



.align 4
.section .text.2
.globl info
info:

//checkvals (none)
	.word 0x1FFF7590	//wafer X & Y verify readable
	.word 0x00000000
	.word 0x00000000

	.word 0x1FFF7594	//lot num and wafer num. verify readable and lot num is ascii
	.word 0x80808000
	.word 0x00000000
	
	.word 0x1FFF7598	//lot num part 2. verify readable and lot num is ascii
	.word 0x80808080
	.word 0x00000000

//now the fixed-length footer:
	//load address of this code
	.word 0x20000000

	//flash staging area in ram
	.word 0x20000400

	//number of checkvals above
	.word 3
	
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
	.word 0x00000066
	.word 0x00000004
	.word 0x0000000a
	.word 0x00000000
	
	//this word must be zero
	.word 0
	
	//human-friendly name for this cpu/script
	.ascii "STM32G0xx"
