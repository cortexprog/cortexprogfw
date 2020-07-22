.syntax unified
.thumb
.section .text

//C-m3
//we have 32K of RAM at 0x20000000 or more
//we'll program 16K at a time

//we have variable amount of flash in variable arrangements at 0x08000000, u16 flash_size_in_kb is at 0x1FFF7A22
//FMC is at 0x40022000
//flash arrangements up to 512K always look like this (cut as needed to fill size)
//	4x16,64,3x128
//1MB arrangements come in single (F40x,F41x) and selectable single/dual bank (F42x, F43x, F46x, F47x), we force selectable ones into single bank mode, and they look thus:
//	4x16,64,7x128
//1.5MB as follows:
//	4x16,64,11x128
//2MB (dual bank) as follows:
//	4x16,64,7x128,4x16,64,7x128


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



get_flashsz_and_proper_areas_ptr:	//returns r1 = flash sz in B, r2 = proper areas pointer, does not clobber r0
	ldr   r1, =0x1FFF7A22	//size in Kbytes
	ldrh  r1, [r1]
	cmp   r1, #2048
	ite   eq
	ldreq r2, =areas_2mb
	ldrne r2, =areas_under_2_mb
	lsls  r1, #10			//size in bytes
	bx    lr

.globl init_stage_1
init_stage_1:

	//setup SP and r0
	ldr   r0, =0x20000400
	mov   sp, r0
	
	//get flash size (in bytes)find the right make a map
	bl    get_flashsz_and_proper_areas_ptr

	//loop through them and write as needed	(we do it inefficiently (set numIdentical) to zero always, but this is ok as we have a lot of ram)
	adds  r0, #4			//save space for num_areas
	mov   r3, #0x08000000	//addr
	
1:
	ldrb  r4, [r2], #1		//log2 (size of next chunk)
	
	//write area info (always one area, write 16K, erase as needed)
	add   r5, r4, #0x10e00
	stmia r0!, {r3, r5}
	
	//housekeeping
	movs  r5, #1
	lsls  r5, r4
	adds  r3, r5
	subs  r1, r5
	bne   1b

	//count up the areas
	movs  r1, r0
	mov   r0, sp
	subs  r1, r0
	lsrs  r1, #3
	str   r1, [r0]

	//all done, set up registers
	ldr   r7, =0x40023C00	// flash unit base
	
	//unlock flash regs if needed
	ldr   r1, [r7, #0x10]
	lsls  r1, #1
	itttt cs
	ldrcs r1, =0x45670123
	addcs r2, r1, 0x88888888	// == 0xCDEF89AB
	strcs r1, [r7, #0x04]
	strcs r2, [r7, #0x04]
	
	//unlock flash opt regs if needed
	ldr   r1, [r7, #0x14]
	lsrs  r1, #1
	itttt cs
	ldrcs r1, =0x08192A3B
	addcs r2, r1, 0x44444444	// == 0x4C5D6E7F
	strcs r1, [r7, #0x08]
	strcs r2, [r7, #0x08]
	
	//disable dual-banking if it is enabled (it can only be enabled in chips that support it)
	ldr   r1, [r7, #0x14]
	bic   r1, #0x40000000
	str   r1, [r7, #0x14]
	
	//set PSIZE for 8-bits-at-a-time (safest at low voltages)
	ldr   r1, [r7, #0x10]
	bic   r1, #0x300
	str   r1, [r7, #0x10]
	
	//names (none)
	movs  r1, #0
	
	//flags (none)
	movs  r2, #0
	bkpt

areas_2mb:			//(bytes in order, represeing log sizes in bytes)
	.word 0x0e0e0e0e
	.word 0x11111110
	.word 0x11111111
	//falls through to next list on purpose

areas_under_2_mb:	//(bytes in order)
	.word 0x0e0e0e0e
	.word 0x11111110
	.word 0x11111111
	.word 0x11111111


wait:
	ldr   r12, [r7, #0x0c]
	lsrs  r12, #17
	bcs   wait
	bx    lr


.globl all_erase
all_erase:

	ldr   r1, [r7, #0x10]
	bic   r1, #0x8000
	bic   r1, #0x0007
	orrs  r1, #0x04				//set MER
	str   r1, [r7, #0x10]
	orrs  r1, #0x10000			//set STRT
	str   r1, [r7, #0x10]
	bl    wait
	
	//if the device is a 2MB part, erase bank2
	cmp   r7, #2048
	bne   1f
	ldr   r1, [r7, #0x10]
	bic   r1, #0x8000
	bic   r1, #0x0007
	orrs  r1, #0x8000			//set MER1
	str   r1, [r7, #0x10]
	orrs  r1, #0x10000			//set STRT
	str   r1, [r7, #0x10]
	bl    wait
	
1:
	movs  r0, #1
	bkpt
	
	
.globl block_erase	//assumes valid address passed
block_erase:

	bl    get_flashsz_and_proper_areas_ptr
	sub   r0, #0x08000000	//addr
	
	//loop through them and write as needed	(we do it inefficiently (set numIdentical) to zero always, but this is ok as we have a lot of ram)
	mov   r5, #0
1:
	cmp   r0, #0
	beq   2f
	adds  r5, #1
	ldrb  r4, [r2], #1		//log2 (size of next chunk)
	movs  r3, #1
	lsls  r3, r4			//size of this chunk
	
	//housekeeping
	subs  r0, r3
	b     1b
	
2:
	
	//we found the block number and it is in r5
	ldr   r1, [r7, #0x10]
	
	cmp   r5, #12
	it    ge
	addge r5, #4
	
	lsls  r5, #3
	bic   r1, #0x8000
	bic   r1, #0x00FF
	orrs  r1, #0x02				//set SER
	orrs  r1, r5
	
	str   r1, [r7, #0x10]
	orrs  r1, #0x10000			//set STRT
	str   r1, [r7, #0x10]
	
	bl    wait
	movs  r0, #1
	bkpt


.globl block_write
block_write:
	movs r6, #16384			//byte counter
	ldr  r5, =0x20000400	//source reg
	mov  r4, r0				//destrination address

1:
	
	ldr   r1, [r7, #0x10]
	bic   r1, #0x8000
	bic   r1, #0x0007
	orrs  r1, #0x01				//set PG
	str   r1, [r7, #0x10]

	ldrb  r0, [r5], #1
	strb  r0, [r4], #1
	
	bl    wait
	subs  r6, #1
	bne   1b
	
	mov   r0, #1
	bkpt



.align 2
.section .text.2
.globl info
info:

//checkvals (at least something)
	.word 0x1fff0000	//make sure the ROM is where it should be
	.word 0x00000000
	.word 0x00000000
	
	.word 0x08000000	//make sure the flash is where it should be
	.word 0x00000000
	.word 0x00000000
	
	.word 0x1FFF7A10	//make sure device ID is where it should be
	.word 0x00000000
	.word 0x00000000
	
	.word 0x1FFF7A14	//make sure device ID word 2 is ascii as it should be
	.word 0x80808080
	.word 0x00000000
	
	.word 0x1FFF7A18	//make sure device ID word 3 is ascii as it should be
	.word 0x80808080
	.word 0x00000000
	

//now the fixed-length footer:
	//load address of this code
	.word 0x20000000

	//flash staging area in ram
	.word 0x20000400

	//number of checkvals above
	.word 5
	
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
	.word 0x00000000	//TODO (all of these)
	.word 0x00000000
	.word 0x00000011
	.word 0x00000004
	.word 0x0000000a
	.word 0x00000000
	
	//this word must be zero
	.word 0
	
	//human-friendly name for this cpu/script
	.ascii "STM32F4-series"



/*
	always 32KB or more of RAM
	
	
	flash size in KB: u16 at 0x1FFF7A22
	
	
	base: 0x08000000
		STM32F401	:
		STM32F411	:
		STM32F446	:
						4x16,64,3x128
		STM32F413	:
		STM32F423	:
						4x16,64,11x128
		STM32F410	:
						4x16,64
		STM32F405
		STM32F407
		STM32F415
		STM32F417	:
						4x16,64,7x128
		STM32F427
		STM32F429
		STM32F439
		STM32F469
		STM32F479	:
						if (512K):
								4x16,64,3x128
						if (2MB):
								4x16,64,7x128,4x16,64,7x128
						if (1MB):
								(we must set DB1M=0)
								4x16,64,7x128
	*/