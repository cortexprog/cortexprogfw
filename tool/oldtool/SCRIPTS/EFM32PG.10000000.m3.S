.syntax unified
.section .text


//ram at 0x10000000 is the same as ram at 0x20000000, so watch out for that
//we stage data at 0x20000400 and start our SP at the same address
//this code will live at 0x10000000


//entry points (also we breakpoint this place and use it as debugger call)
.globl entrypts
entrypts:
	//flash size (will be replaced by init code)
	flashsz:
	.word 0
	
	//flash block size for erase/write (will be replaced by init code)
	.word 0

	//flash base
	.word 0

	//syscall area
syscall:
	nop
	b syscall

	//init stage 1
	bl init_stage_1
	
	//init stage 2
	.word 0
	
	//init stage 3
	.word 0
	
	//mass erase
	bl mass_erase
	
	//erase at address (uint32_t addr)
	bl page_erase
	
	//write block  (uint32_t addr)
	bl page_write
	
	//custom command (...)
	.word 0
	


.globl init_stage_1
init_stage_1:

	ldr   sp, =0x20000400
	ldr   r0, =0x0FE081E6
	ldrb  r1, [r0, #0x01]	//page size (in abstract)
	ldrh  r4, [r0, #0x12]	//flash amount (in kb)
	movs  r6, #1			//convenient to have a 1 around
	lsls  r5, r6, #10
	lsls  r5, r1			//flash block size in r5 in bytes
	lsls  r4, #10			//flash size in bytes
	ldr   r1, =flashsz
	stmia r1!, {r4, r5}
	
	ldr   r7, =0x400e0000	//save the base addr in r7 for later
	ldr   r1,=0x1b71
	str   r1, [r7, #0x40]	//unlock regs
	str   r6, [r7, #0x74]	//power up flash
	str   r6, [r7, #0x08]	//enable flash writes
	
	bkpt

.globl flash_ready_wait
flash_ready_wait:
1:	//branch to GLOBAL label generates long branch instr
	ldr   r0, [r7, #0x1c]
	lsrs  r0, #1
	bcs   1b
	bx    lr

.globl mass_erase
mass_erase:
	bl    flash_ready_wait		//patience is a virtue
	ldr   r0, =0x631A
	str   r0, [r7, #0x54]		//unlock mass erase ability
	lsls  r0, r6, #8
	str   r0, [r7, #0x0c]		//initiate mass erase
	bl    flash_ready_wait		//patience is a virtue
	str   r6, [r7, #0x54]		//re-lock mass erase ability
	bkpt
	
.globl page_write
page_write:
	str   r0, [r7, #0x10]	//write address
	bl    flash_ready_wait
	str   r6, [r7, #0x0c]	//latch address
	ldr   r4, =0x20000400	//source addr
	adds  r3, r4, r5		//words per block
	movs  r1, #8
	
1:	//big loop
	ldr   r2, [r4]
	
2:	//wait for flash to be ready for more data
	ldr   r0, [r7, #0x1C]
	lsrs  r0, #4
	bcc   2b
	
	//write flash data
	str   r2, [r7, #0x18]
	
	//do the write
	str   r1, [r7, #0x0c]
	bl    flash_ready_wait
	
	//loop maintenance
	adds  r4, #4
	
	cmp   r4, r3
	bne   1b
	
	bkpt

.globl page_erase
page_erase:
	str   r0, [r7, #0x10]		//write address
	bl    flash_ready_wait		//patience is a virtue
	str   r6, [r7, #0x0c]		//latch address
	movs  r0, #2
	str   r0, [r7, #0x0c]
	bl    flash_ready_wait		//patience is a virtue
	bkpt


.align 4
.section .text.2
.globl info
info:

//numCheckvals field (none)


//now the fixed-length footer:
	//load address of this code
	.word 0x10000000

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
	.word 0x00000006
	.word 0x00000000
	.word 0x00000000
	.word 0x00000000
	.word 0x00000001
	.word 0x00000032
	.word 0x0000000f
	.word 0x00000020
	
	//this word must be zero
	.word 0
	
	//human-friendly name for this cpu/script
	.ascii "EFM32 Power Gecko"

	
	//only tested on EFM32PG1BxxxFxxxGMxx.m3.S